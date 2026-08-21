#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sx1278_fsk.h"

/* Same pins as former Arduino sketch (now ESP-IDF only). */
#define PIN_SCK     9
#define PIN_MISO    10
#define PIN_MOSI    11
#define PIN_CS      12
#define PIN_DIO0    14
#define PIN_RST     13
#define PIN_LED     21

/* Match STM32 RF_FREQUENCY in subghz_phy_app.h */
#define RF_FREQUENCY_HZ     433018893u
#define FSK_BITRATE_BPS     1200u
#define FSK_FDEV_HZ         5000u
#define FSK_RX_BW_HZ        20000u
#define FSK_PREAMBLE_BYTES  8

static SemaphoreHandle_t s_rx_sem;

static void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
    fflush(stdout);
}

static void on_dio0(void *arg)
{
    BaseType_t hp = pdFALSE;
    (void)arg;
    xSemaphoreGiveFromISR(s_rx_sem, &hp);
    if (hp == pdTRUE) {
        portYIELD_FROM_ISR();
    }
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

static void print_callsign(const uint8_t *addr)
{
    for (int i = 0; i < 6; i++) {
        char c = (char)(addr[i] >> 1);
        if (c != ' ') {
            putchar(c);
        }
    }
    printf("-%u", (unsigned)((addr[6] >> 1) & 0x0F));
}

static void parse_ax25_ui(const uint8_t *data, int len)
{
    int start = 0;
    int end = len;
    while (start < end && data[start] == 0x7E) {
        start++;
    }
    while (end > start && data[end - 1] == 0x7E) {
        end--;
    }
    int frame_len = end - start;
    if (frame_len < 16) {
        log_line("  AX.25: frame too short (%d)", frame_len);
        return;
    }

    uint16_t got_fcs = (uint16_t)data[start + frame_len - 2] |
                       ((uint16_t)data[start + frame_len - 1] << 8);
    uint16_t exp_fcs = ax25_crc16(&data[start], (size_t)(frame_len - 2));
    bool crc_ok = (got_fcs == exp_fcs);

    printf("  Dest: ");
    print_callsign(&data[start]);
    printf("  Src: ");
    print_callsign(&data[start + 7]);
    printf("\r\n");

    uint8_t ctrl = data[start + 14];
    uint8_t pid = data[start + 15];
    printf("  Ctrl=0x%02X PID=0x%02X", ctrl, pid);
    if ((ctrl & 0xEF) != 0x03) {
        printf(" (not UI)");
    }
    if (pid != 0xF0) {
        printf(" (PID != 0xF0)");
    }
    printf("\r\n");
    log_line("  FCS %s", crc_ok ? "OK" : "FAIL");

    int info_len = frame_len - 18;
    if (info_len > 0) {
        printf("  Info: ");
        for (int i = 0; i < info_len; i++) {
            char c = (char)data[start + 16 + i];
            putchar(isprint((unsigned char)c) ? c : '.');
        }
        printf("\r\n");
    }
    fflush(stdout);
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
    vTaskDelay(pdMS_TO_TICKS(80));
    gpio_set_level((gpio_num_t)PIN_LED, 0);
}

static void handle_packet(void)
{
    uint8_t buf[SX1278_FIFO_SIZE];
    size_t len = 0;
    esp_err_t err = sx1278_read_packet(buf, sizeof(buf), &len);
    if (err == ESP_OK) {
        log_line("");
        log_line("======== PACKET RECEIVED ========");
        log_line("  Length: %u  (expect ~56 bytes Path A)", (unsigned)len);
        log_line("  RSSI:   %.1f dBm", sx1278_get_rssi_dbm());
        log_line("  FreqErr: %.0f Hz  (trim STM32 XTAL if large)",
                 sx1278_get_freq_error_hz());
        log_line("  Hex:");
        hex_dump(buf, (int)len);

        printf("  ASCII: ");
        for (size_t i = 0; i < len; i++) {
            putchar(isprint(buf[i]) ? (char)buf[i] : '.');
        }
        printf("\r\n");

        parse_ax25_ui(buf, (int)len);
        log_line("================================");
        pulse_led();
    } else {
        log_line("[ERR] read_packet: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(sx1278_start_receive());
}

void app_main(void)
{
    /* USB-JTAG re-enumerates after reset; give the monitor time to attach. */
    vTaskDelay(pdMS_TO_TICKS(1500));

    log_line("=========================================");
    log_line(" SX1278 FSK AX.25 Receiver (Path A)");
    log_line(" ESP-IDF  USB Serial/JTAG");
    log_line("=========================================");
    log_line("NOTE: Path A = Semtech GFSK packet (sync C1 94 C1).");
    log_line("      SoundModem/Direwolf need Path B (AFSK audio).");

    s_rx_sem = xSemaphoreCreateBinary();
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

    ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)PIN_DIO0, on_dio0, NULL));

    log_line("[2] startReceive...");
    ESP_ERROR_CHECK(sx1278_start_receive());
    log_line("OK");
    log_line("");
    log_line("Listening (STM32 TX every 5s). Heartbeat '.' every 2s.");
    log_line("Status line every ~10s: RSSI + preamble/sync/payload flags.");
    log_line("");

    unsigned beat = 0;
    while (1) {
        /* DIO0 ISR or SPI poll of PayloadReady (covers missing DIO0 wire). */
        bool got = (xSemaphoreTake(s_rx_sem, pdMS_TO_TICKS(200)) == pdTRUE) ||
                   sx1278_payload_ready();
        if (got) {
            handle_packet();
            continue;
        }

        beat++;
        /* 200 ms * 10 = 2 s heartbeat */
        if ((beat % 10U) == 0U) {
            printf(".");
            fflush(stdout);
        }
        /* ~10 s: dump modem status so RF/sync problems are visible */
        if ((beat % 50U) == 0U) {
            uint8_t irq1 = 0;
            uint8_t irq2 = 0;
            float rssi = 0.0f;
            sx1278_read_status(&irq1, &irq2, &rssi);
            log_line("");
            log_line("[stat] RSSI=%.1f dBm  irq1=0x%02X (pre=%u sync=%u)  irq2=0x%02X (pay=%u ov=%u)",
                     rssi, irq1,
                     (irq1 & 0x02) ? 1 : 0,
                     (irq1 & 0x01) ? 1 : 0,
                     irq2,
                     (irq2 & 0x04) ? 1 : 0,
                     (irq2 & 0x10) ? 1 : 0);
        }
    }
}
