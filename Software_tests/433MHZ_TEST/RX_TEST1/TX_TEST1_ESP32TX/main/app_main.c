#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sx1278_fsk.h"

/* Same pins as TX_TEST1_ESP32RX. */
#define PIN_SCK     9
#define PIN_MISO    10
#define PIN_MOSI    11
#define PIN_CS      12
#define PIN_DIO0    14
#define PIN_RST     13
#define PIN_LED     21

/* Match STM32 RF_FREQUENCY / FSK_* in TX_TEST1_STM32TRX subghz_phy_app.h */
#define RF_FREQUENCY_HZ     433018893u
#define FSK_BITRATE_BPS     1200u
#define FSK_FDEV_HZ         5000u
#define FSK_RX_BW_HZ        20000u
#define FSK_PREAMBLE_BYTES  8
#define TX_POWER_DBM        16      /* same as STM32 TX_OUTPUT_POWER in TX_TEST1 */
#define TX_TIMEOUT_MS       3000u   /* same as STM32 TX_TIMEOUT_VALUE */
#define TRANSMIT_PERIOD_MS  5000u   /* same as STM32 TRANSMIT_PERIOD_MS */

/* AX.25 UI frame, Path A (same format as AX25SG_BuildUIFrame on the STM32):
 * raw AX.25 bytes as GFSK payload, no NRZI / bit-stuffing, 2x 0x7E at the end.
 * Roles swapped versus TX_TEST1: ground station -> payload. */
#define AX25_SRC_CALLSIGN   "FIUNA1"
#define AX25_SRC_SSID       1
#define AX25_DEST_CALLSIGN  "CCTE"
#define AX25_DEST_SSID      0
#define AX25_FLAG           0x7E
#define AX25_TRAIL_FLAGS    2
#define AX25_CTRL_UI        0x03
#define AX25_PID_NO_L3      0xF0

static void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
    fflush(stdout);
}

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

static void build_addr(const char *callsign, uint8_t ssid, bool is_last,
                       bool is_command, uint8_t *out)
{
    size_t clen = strlen(callsign);
    for (size_t i = 0; i < 6; i++) {
        char c = (i < clen) ? callsign[i] : ' ';
        out[i] = (uint8_t)((uint8_t)c << 1);
    }
    uint8_t sb = 0x60; /* reserved bits */
    if (is_command) {
        sb |= 0x80;
    }
    sb |= (uint8_t)((ssid & 0x0F) << 1);
    if (is_last) {
        sb |= 0x01;
    }
    out[6] = sb;
}

/* Returns frame length, 0 if it does not fit in max_len. */
static size_t ax25_build_ui(const char *info, uint8_t *out, size_t max_len)
{
    size_t info_len = strlen(info);
    size_t needed = 14 + 2 + info_len + 2 + AX25_TRAIL_FLAGS;
    if (needed > max_len) {
        return 0;
    }

    size_t pos = 0;
    build_addr(AX25_DEST_CALLSIGN, AX25_DEST_SSID, false, true, &out[pos]);
    pos += 7;
    build_addr(AX25_SRC_CALLSIGN, AX25_SRC_SSID, true, false, &out[pos]);
    pos += 7;
    out[pos++] = AX25_CTRL_UI;
    out[pos++] = AX25_PID_NO_L3;
    memcpy(&out[pos], info, info_len);
    pos += info_len;

    uint16_t fcs = ax25_crc16(out, pos);
    out[pos++] = (uint8_t)(fcs & 0xFF);        /* low byte first */
    out[pos++] = (uint8_t)((fcs >> 8) & 0xFF);
    for (int i = 0; i < AX25_TRAIL_FLAGS; i++) {
        out[pos++] = AX25_FLAG;
    }
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

void app_main(void)
{
    /* USB-JTAG re-enumerates after reset; give the monitor time to attach. */
    vTaskDelay(pdMS_TO_TICKS(1500));

    log_line("=========================================");
    log_line(" SX1278 FSK AX.25 Transmitter (Path A)");
    log_line(" ESP-IDF  USB Serial/JTAG");
    log_line("=========================================");
    log_line("Peer: STM32WLE5 RX (TX_TEST1_STM32TRX), sync C1 94 C1.");

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
        .crc_on = false,
        .afc_on = true,
        .gaussian_bt_05 = true,
    };
    memcpy(cfg.sync_word, sync, sizeof(sync));

    log_line("[1] sx1278_init...");
    esp_err_t err = sx1278_init(&pins, &cfg);
    if (err != ESP_OK) {
        log_line("FAIL: %s  (check SPI wiring / chip select)", esp_err_to_name(err));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    log_line("OK  VERSION=0x%02X", sx1278_chip_version());

    log_line("[2] set TX power %d dBm...", TX_POWER_DBM);
    ESP_ERROR_CHECK(sx1278_set_tx_power(TX_POWER_DBM));
    log_line("OK");
    log_line("");
    log_line("Transmitting every %u ms.", (unsigned)TRANSMIT_PERIOD_MS);
    log_line("");

    unsigned seq = 0;
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        /* Plain ASCII (no accents): max 43 chars so the frame fits 63 bytes */
        char info[48];
        uint8_t frame[SX1278_FSK_MAX_PAYLOAD];
        snprintf(info, sizeof(info), "Hola STM32! Soy el ESP32, msg #%05u", seq % 100000U);
        size_t len = ax25_build_ui(info, frame, sizeof(frame));
        if (len == 0) {
            log_line("[ERR] AX.25 frame exceeds SX1278 FSK limit %u", (unsigned)SX1278_FSK_MAX_PAYLOAD);
        } else {
            log_line("");
            log_line("======== ENVIANDO MENSAJE #%u ========", seq);
            log_line("  Mensaje : %s", info);
            log_line("  De      : %s-%d  ->  Para: %s-%d",
                     AX25_SRC_CALLSIGN, AX25_SRC_SSID,
                     AX25_DEST_CALLSIGN, AX25_DEST_SSID);
            log_line("  Bytes   : %u en aire (%u de mensaje)",
                     (unsigned)len, (unsigned)strlen(info));
            log_line("  Hex:");
            hex_dump(frame, (int)len);

            gpio_set_level((gpio_num_t)PIN_LED, 1);
            TickType_t t0 = xTaskGetTickCount();
            err = sx1278_send_packet(frame, len, TX_TIMEOUT_MS);
            TickType_t dt = xTaskGetTickCount() - t0;
            gpio_set_level((gpio_num_t)PIN_LED, 0);

            if (err == ESP_OK) {
                log_line("  Enviado : OK (%u ms en aire)", (unsigned)pdTICKS_TO_MS(dt));
            } else {
                log_line("  Enviado : FALLO (%s)", esp_err_to_name(err));
            }
        }
        seq++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TRANSMIT_PERIOD_MS));
    }
}
