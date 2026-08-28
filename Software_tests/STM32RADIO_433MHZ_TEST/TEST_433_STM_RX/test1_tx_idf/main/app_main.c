#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sx1278_fsk.h"

/* =========================================================================
 * ESP32-S3 GPIO Pin Mapping for SX1278 / RFM98 Transceiver
 * ========================================================================= */
#define PIN_SCK     9
#define PIN_MISO    10
#define PIN_MOSI    11
#define PIN_CS      12
#define PIN_DIO0    14
#define PIN_RST     13
#define PIN_LED     21


/* =========================================================================
 * RF Physical Layer Configuration (Matches STM32WLE5 SubGHz PHY)
 * ========================================================================= */
#define RF_FREQUENCY_HZ     433018893u   /* 433 MHz + 18.893 kHz crystal offset */
#define FSK_BITRATE_BPS     1200u        /* 1200 bps */
#define FSK_FDEV_HZ         5000u        /* 5.0 kHz deviation */
#define FSK_RX_BW_HZ        20000u       /* 20.0 kHz filter bandwidth */
#define FSK_PREAMBLE_BYTES  8            /* 8 bytes of 0x55 */
#define TX_POWER_DBM        17           /* +17 dBm output power via PA_BOOST */
#define TX_INTERVAL_MS      5000         /* Transmit beacon every 5 seconds */

/* =========================================================================
 * AX.25 UI Frame Settings
 * ========================================================================= */
#define AX25_DEST_CALL      "FIUNA1"
#define AX25_DEST_SSID      1
#define AX25_SRC_CALL       "CCTE"
#define AX25_SRC_SSID       0
#define AX25_CTRL_UI        0x03
#define AX25_PID_NOL3       0xF0

static uint32_t s_tx_seq = 0;

static void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
    fflush(stdout);
}

/**
 * @brief Calculate AX.25 / CCITT-16 reflected Frame Check Sequence (FCS).
 * Polynomial: 0x8408 (reflected 0x1021), initial value: 0xFFFF.
 */
static uint16_t ax25_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1U) {
                crc = (uint16_t)((crc >> 1U) ^ 0x8408U);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }
    return (uint16_t)(~crc);
}

/**
 * @brief Encode a callsign and SSID into AX.25 address format (7 bytes).
 */
static void encode_callsign(uint8_t *out, const char *call, uint8_t ssid, bool is_last)
{
    size_t len = strlen(call);
    for (int i = 0; i < 6; i++) {
        char c = (i < (int)len) ? (char)toupper((unsigned char)call[i]) : ' ';
        out[i] = (uint8_t)(c << 1);
    }
    /* 7th byte: SSID with reserved bits (0x60) + HDLC extension bit (bit 0) */
    out[6] = (uint8_t)((ssid << 1) | 0x60 | (is_last ? 0x01 : 0x00));
}

/**
 * @brief Build a complete AX.25 UI Frame.
 *
 * Layout: [Dest 7B][Src 7B][Ctrl 1B][PID 1B][Info NB][FCS 2B]
 *
 * @return Total frame length in bytes (or 0 on error).
 */
static size_t build_ax25_ui_frame(uint8_t *out, size_t max_len,
                                  const char *dest, uint8_t dest_ssid,
                                  const char *src, uint8_t src_ssid,
                                  const char *info)
{
    size_t info_len = strlen(info);
    size_t total_len = 7 + 7 + 1 + 1 + info_len + 2;

    if (total_len > max_len || total_len > SX1278_FSK_MAX_PAYLOAD) {
        log_line("[ERR] Frame size %u exceeds buffer limit %u", (unsigned)total_len, (unsigned)max_len);
        return 0;
    }

    size_t pos = 0;
    encode_callsign(&out[pos], dest, dest_ssid, false);
    pos += 7;

    encode_callsign(&out[pos], src, src_ssid, true);
    pos += 7;

    out[pos++] = AX25_CTRL_UI;
    out[pos++] = AX25_PID_NOL3;

    memcpy(&out[pos], info, info_len);
    pos += info_len;

    /* Calculate FCS CRC over [Dest..Info] */
    uint16_t fcs = ax25_crc16(out, pos);
    out[pos++] = (uint8_t)(fcs & 0xFF);         /* LSB first */
    out[pos++] = (uint8_t)((fcs >> 8) & 0xFF);  /* MSB second */

    return pos;
}

static void hex_dump(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        if (i % 16 == 0) {
            if (i > 0) {
                printf("\r\n");
            }
            printf("  %02X: ", i);
        }
        printf("%02X ", data[i]);
    }
    printf("\r\n");
}

static void pulse_led(void)
{
    gpio_set_level((gpio_num_t)PIN_LED, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
    gpio_set_level((gpio_num_t)PIN_LED, 0);
}

static void transmit_beacon(void)
{
    char info_str[48];
    snprintf(info_str, sizeof(info_str), "GPS:-25.330243,-57.517492,100.0,SAT:4 #%lu", (unsigned long)s_tx_seq);

    uint8_t frame_buf[SX1278_FIFO_SIZE];
    size_t frame_len = build_ax25_ui_frame(frame_buf, sizeof(frame_buf),
                                           AX25_DEST_CALL, AX25_DEST_SSID,
                                           AX25_SRC_CALL, AX25_SRC_SSID,
                                           info_str);
    if (frame_len == 0) {
        return;
    }

    log_line("");
    log_line("--------------------------------------------------");
    log_line(">>> TRANSMITTING PACKET #%lu (%u bytes) >>>", (unsigned long)s_tx_seq, (unsigned)frame_len);
    log_line("  Dest: %s-%u", AX25_DEST_CALL, AX25_DEST_SSID);
    log_line("  Src:  %s-%u", AX25_SRC_CALL, AX25_SRC_SSID);
    log_line("  Info: %s", info_str);
    log_line("  Hex Payload:");
    hex_dump(frame_buf, (int)frame_len);

    int64_t t_start = esp_timer_get_time();
    gpio_set_level((gpio_num_t)PIN_LED, 1);

    esp_err_t err = sx1278_send_packet(frame_buf, frame_len);

    gpio_set_level((gpio_num_t)PIN_LED, 0);
    int64_t t_elapsed_ms = (esp_timer_get_time() - t_start) / 1000;

    if (err == ESP_OK) {
        log_line(">>> TX SUCCESS in %lld ms (Rate: 1200 bps) <<<", t_elapsed_ms);
        s_tx_seq++;
    } else {
        log_line("[ERR] Transmission failed: %s", esp_err_to_name(err));
    }
    log_line("--------------------------------------------------");
}

void app_main(void)
{
    /* Allow USB Serial/JTAG monitor time to attach after boot */
    vTaskDelay(pdMS_TO_TICKS(1500));

    log_line("==================================================");
    log_line(" ESP32-S3 Sub-GHz 433 MHz Transmitter (test1_tx_idf)");
    log_line(" SX1278 / RFM98 FSK AX.25 Telemetry Transmitter");
    log_line("==================================================");
    log_line("Target:       ESP32-S3 (Xtensa Dual-Core @ 240 MHz)");
    log_line("Frequency:    %lu Hz (433.018 MHz)", (unsigned long)RF_FREQUENCY_HZ);
    log_line("Modulation:   FSK 1200 bps, FDEV=5.0 kHz, BT=0.5");
    log_line("Sync Word:    0xC1 0x94 0xC1 (3 bytes)");
    log_line("TX Power:     +%d dBm (PA_BOOST)", TX_POWER_DBM);
    log_line("TX Interval:  %d ms", TX_INTERVAL_MS);
    log_line("Pinout:       SCK=%d MISO=%d MOSI=%d CS=%d RST=%d DIO0=%d LED=%d",
             PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS, PIN_RST, PIN_DIO0, PIN_LED);
    log_line("==================================================");


    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    const sx1278_pins_t pins = {
        .sck = PIN_SCK,
        .miso = PIN_MISO,
        .mosi = PIN_MOSI,
        .cs = PIN_CS,
        .rst = PIN_RST,
        .dio0 = PIN_DIO0,
        .led = PIN_LED,
    };

    const uint8_t sync[] = { 0xC1, 0x94, 0xC1 };
    sx1278_fsk_cfg_t cfg = {
        .frequency_hz = RF_FREQUENCY_HZ,
        .bitrate_bps = FSK_BITRATE_BPS,
        .fdev_hz = FSK_FDEV_HZ,
        .rx_bw_hz = FSK_RX_BW_HZ,
        .preamble_bytes = FSK_PREAMBLE_BYTES,
        .sync_len = sizeof(sync),
        .max_payload = SX1278_FSK_MAX_PAYLOAD,
        .tx_power_dbm = TX_POWER_DBM,
        .crc_on = false,
        .afc_on = true,
        .gaussian_bt_05 = true,
    };
    memcpy(cfg.sync_word, sync, sizeof(sync));

    log_line("[1] Initializing SX1278 radio...");
    esp_err_t err = sx1278_init(&pins, &cfg);
    if (err != ESP_OK) {
        log_line("FAIL: %s (Check 3.3V power and SPI wiring)", esp_err_to_name(err));
        while (1) {
            pulse_led();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    log_line("[2] SX1278 Ready! Chip Version: 0x%02X", sx1278_chip_version());
    log_line("Starting periodic AX.25 telemetry transmission loop...");

    /* Main Transmission Loop */
    while (1) {
        transmit_beacon();
        vTaskDelay(pdMS_TO_TICKS(TX_INTERVAL_MS));
    }
}
