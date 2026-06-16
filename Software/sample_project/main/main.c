/**
 * @file    main.c
 * @brief   ESP32 SPI Master — reads GPS NMEA data from an STM32 slave
 *
 * The STM32 receives GPS NMEA sentences over UART2 and forwards them
 * byte-by-byte over SPI1 (slave). This firmware clocks out those bytes
 * as SPI master, reassembles complete NMEA sentences, and logs them.
 *
 * ─── Pinout ────────────────────────────────────────────────────────────────
 *   ESP32 GPIO   Signal   STM32 SPI1 pin
 *   ─────────── ──────── ──────────────
 *   GPIO 6       SCLK     PA1  (SPI1_SCK)
 *   GPIO 2       MISO     PB4  (SPI1_MISO)  ← GPS data flows here
 *   GPIO 7       MOSI     PB5  (SPI1_MOSI)  [not used for GPS, kept for completeness]
 *   GPIO 10      CS       NSS  (active-low)
 *
 * ─── SPI parameters ────────────────────────────────────────────────────────
 *   Mode   : Master
 *   Clock  : 2 MHz
 *   Phase  : CPOL=0, CPHA=0 (Mode 0 — matches STM32 default)
 *   Width  : 8 bits per transfer
 *
 * ─── Protocol ──────────────────────────────────────────────────────────────
 *   The ESP32 continuously polls the STM32 by asserting CS and clocking out
 *   blocks of dummy bytes (0xFF). The STM32 shifts out GPS data on MISO.
 *   Idle bytes (0x00 / 0xFF) are discarded. NMEA sentences starting with '$'
 *   are accumulated and printed once a line terminator (\r or \n) is received.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ─── Logging tag ──────────────────────────────────────────────────────── */
static const char *TAG = "GPS_SPI";

/* ─── SPI GPIO pins ────────────────────────────────────────────────────── */
#define PIN_SCLK    6
#define PIN_MOSI    7
#define PIN_MISO    2
#define PIN_CS      10

/* ─── SPI bus / device config ──────────────────────────────────────────── */
#define SPI_HOST_DEV    SPI2_HOST
#define SPI_CLOCK_HZ    (2 * 1000 * 1000)   /* 2 MHz */

/* ─── Transfer & line buffer sizes ─────────────────────────────────────── */
#define RX_BUF_SIZE     256
#define NMEA_LINE_MAX   128

/* ─── NMEA accumulator ──────────────────────────────────────────────────── */
static char nmea_line[NMEA_LINE_MAX];
static int  nmea_len = 0;

/**
 * @brief Process one received byte.
 *        Ignores idle bytes, accumulates NMEA characters, and logs
 *        each complete sentence to the serial monitor.
 */
static void process_byte(uint8_t byte)
{
    /* Discard idle/padding bytes sent when the slave has nothing to transmit */
    if (byte == 0x00 || byte == 0xFF) {
        return;
    }

    if (byte == '\r' || byte == '\n') {
        if (nmea_len > 0) {
            nmea_line[nmea_len] = '\0';
            if (nmea_line[0] == '$') {
                ESP_LOGI(TAG, "%s", nmea_line);
            }
            nmea_len = 0;
        }
        return;
    }

    if (nmea_len < (NMEA_LINE_MAX - 1)) {
        nmea_line[nmea_len++] = (char)byte;
    } else {
        /* Overflow — discard and restart */
        ESP_LOGW(TAG, "NMEA line overflow, discarding");
        nmea_len = 0;
    }
}

/**
 * @brief GPS SPI polling task.
 *        Runs forever: clocks the STM32 slave, collects bytes, and
 *        feeds them to process_byte().
 */
static void gps_spi_task(void *arg)
{
    spi_device_handle_t spi = (spi_device_handle_t)arg;

    /* Static DMA-capable buffers */
    static uint8_t tx_buf[RX_BUF_SIZE];
    static uint8_t rx_buf[RX_BUF_SIZE];
    memset(tx_buf, 0xFF, sizeof(tx_buf));   /* send dummy 0xFF while receiving */

    ESP_LOGI(TAG, "GPS SPI reader started — polling STM32 at 2 MHz on MISO GPIO%d", PIN_MISO);

    spi_transaction_t t = {
        .length    = RX_BUF_SIZE * 8,   /* length in bits */
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    while (1) {
        memset(rx_buf, 0, sizeof(rx_buf));

        esp_err_t ret = spi_device_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        for (int i = 0; i < RX_BUF_SIZE; i++) {
            process_byte(rx_buf[i]);
        }

        /*
         * 10 ms yield — prevents hammering the bus when the STM32 has no
         * new GPS data ready. Decrease for lower latency at the cost of
         * higher CPU load.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ─── Entry point ───────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "GPS SPI Master initialising...");
    ESP_LOGI(TAG, "  SCLK -> GPIO%d  MISO -> GPIO%d  MOSI -> GPIO%d  CS -> GPIO%d",
             PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);
    ESP_LOGI(TAG, "  Clock: %d Hz  Mode: 0 (CPOL=0 CPHA=0)", SPI_CLOCK_HZ);

    /* ── 1. Initialise the SPI bus ──────────────────────────────────────── */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = PIN_MISO,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = RX_BUF_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_DEV, &bus_cfg, SPI_DMA_CH_AUTO));

    /* ── 2. Add the STM32 as a slave device ─────────────────────────────── */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,   /* 2 MHz                          */
        .mode           = 0,              /* CPOL=0, CPHA=0 — STM32 default */
        .spics_io_num   = PIN_CS,
        .queue_size     = 1,
        .pre_cb         = NULL,
        .post_cb        = NULL,
    };

    spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_DEV, &dev_cfg, &spi));

    ESP_LOGI(TAG, "SPI bus ready. Starting polling task...");

    /* ── 3. Start the polling task ──────────────────────────────────────── */
    xTaskCreate(gps_spi_task, "gps_spi", 4096, (void *)spi, 5, NULL);
}
