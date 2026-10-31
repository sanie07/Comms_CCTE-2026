#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_link_proto.h"

/* SPI master pins, same numbering as Test10_DRA_SPI (ESP32 master).
 * Wire to the STM32 board SPI1: SCK->PA1, MOSI->PB5, MISO<-PB4, CS->PA0, GND-GND. */
#define PIN_MISO    4
#define PIN_MOSI    5
#define PIN_SCK     6
#define PIN_CS      7

/* Same bus timing as Test10: Mode 0, 1 MHz, 100 us after CS, 20 us per byte
 * so the STM32 IT-driven slave keeps up. */
#define SPI_CLOCK_HZ        1000000
#define CS_SETUP_US         100
#define BYTE_GAP_US         20

#define POLL_PERIOD_MS      250u
#define HEARTBEAT_MS        5000u

static spi_device_handle_t s_spi;

static void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
    fflush(stdout);
}

static uint8_t checksum(const uint8_t *data, size_t len)
{
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) {
        cs ^= data[i];
    }
    return cs;
}

static void link_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_CS,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level((gpio_num_t)PIN_CS, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    /* CS driven by hand to insert the setup delay and byte gaps. */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));
}

static void link_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    ESP_ERROR_CHECK(spi_device_acquire_bus(s_spi, portMAX_DELAY));
    gpio_set_level((gpio_num_t)PIN_CS, 0);
    esp_rom_delay_us(CS_SETUP_US);
    for (size_t i = 0; i < len; i++) {
        spi_transaction_t t = {
            .length = 8,
            .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
            .tx_data = { tx[i] },
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
        rx[i] = t.rx_data[0];
        esp_rom_delay_us(BYTE_GAP_US);
    }
    gpio_set_level((gpio_num_t)PIN_CS, 1);
    spi_device_release_bus(s_spi);
}

static bool all_bytes_equal(const uint8_t *data, size_t len, uint8_t value)
{
    for (size_t i = 0; i < len; i++) {
        if (data[i] != value) {
            return false;
        }
    }
    return true;
}

static void print_message(const spi_link_packet_t *p)
{
    log_line("");
    log_line("======== MENSAJE #%u (STM32 por SPI) ========", (unsigned)p->seq);
    if (p->fcs_ok) {
        log_line("  El STM32 esta recibiendo datos de %s", p->from);
    } else {
        log_line("  El STM32 recibio una trama con errores");
    }
    log_line("  Mensaje : %s", p->msg);
    log_line("  De      : %s", p->from);
    log_line("  Bytes   : %u en aire (%u de mensaje)", (unsigned)p->bytes, (unsigned)p->msg_len);
    log_line("  RSSI    : %d dBm   CFO: %d kHz   FCS: %s",
             (int)p->rssi_dbm, (int)p->cfo_khz, p->fcs_ok ? "OK" : "FALLO");
    log_line("  Totales : %lu OK, %lu con error, %lu errores RX",
             (unsigned long)p->rx_ok, (unsigned long)p->rx_bad, (unsigned long)p->rx_errors);
    log_line("  Uptime STM32: %lu.%lu s",
             (unsigned long)(p->uptime_ms / 1000u), (unsigned long)((p->uptime_ms % 1000u) / 100u));
    log_line("=============================================");
}

void app_main(void)
{
    /* USB-JTAG re-enumerates after reset; give the monitor time to attach. */
    vTaskDelay(pdMS_TO_TICKS(1500));

    log_line("=========================================");
    log_line(" ESP32-C6 SPI master - visor del STM32 RX");
    log_line(" SPI Mode 0  %d Hz  paquetes de %u bytes", SPI_CLOCK_HZ, (unsigned)SPI_LINK_PACKET_SIZE);
    log_line(" SCK=%d MOSI=%d MISO=%d CS=%d", PIN_SCK, PIN_MOSI, PIN_MISO, PIN_CS);
    log_line("=========================================");

    link_init();

    uint8_t tx[SPI_LINK_PACKET_SIZE] = { 0 };
    tx[0] = SPI_LINK_MAGIC;
    tx[1] = SPI_LINK_CMD_POLL;
    tx[SPI_LINK_PACKET_SIZE - 1] = checksum(tx, SPI_LINK_PACKET_SIZE - 1);

    spi_link_packet_t pkt;
    int32_t last_seq = -1;
    unsigned polls = 0;
    unsigned good = 0;
    unsigned bad = 0;
    bool link_up = false;
    uint8_t last_status = 0;
    TickType_t last_beat = xTaskGetTickCount();
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        uint8_t *rx = (uint8_t *)&pkt;
        link_transfer(tx, rx, SPI_LINK_PACKET_SIZE);
        polls++;

        bool valid = (pkt.magic == SPI_LINK_MAGIC) &&
                     (checksum(rx, SPI_LINK_PACKET_SIZE - 1) == pkt.checksum);
        if (valid) {
            good++;
            last_status = pkt.status;
            if (!link_up) {
                link_up = true;
                log_line("[spi] Enlace OK con el STM32 (status 0x%02X)", pkt.status);
            }
            pkt.from[SPI_LINK_FROM_LEN - 1] = '\0';
            pkt.msg[SPI_LINK_MSG_LEN - 1] = '\0';
            if ((pkt.status == SPI_LINK_STATUS_RX_FRAME) && ((int32_t)pkt.seq != last_seq)) {
                if ((last_seq >= 0) && ((uint16_t)(pkt.seq - (uint16_t)last_seq) > 1u)) {
                    log_line("[spi] (se saltaron %u mensajes entre lecturas)",
                             (unsigned)((uint16_t)(pkt.seq - (uint16_t)last_seq) - 1u));
                }
                last_seq = pkt.seq;
                print_message(&pkt);
            }
        } else {
            bad++;
            if (link_up) {
                link_up = false;
                log_line("[spi] Paquete invalido, reintentando...");
            }
        }

        if ((xTaskGetTickCount() - last_beat) >= pdMS_TO_TICKS(HEARTBEAT_MS)) {
            last_beat = xTaskGetTickCount();
            if (link_up) {
                log_line("[spi] OK  lecturas=%u validas=%u invalidas=%u  STM32: %s",
                         polls, good, bad,
                         (last_status == SPI_LINK_STATUS_RX_FRAME) ? "recibiendo" : "escuchando, sin mensajes aun");
            } else if (all_bytes_equal(rx, SPI_LINK_PACKET_SIZE, 0xFF) ||
                       all_bytes_equal(rx, SPI_LINK_PACKET_SIZE, 0x00)) {
                log_line("[spi] Sin respuesta (MISO fijo en 0x%02X): revisa cableado, GND y que el STM32 este corriendo",
                         rx[0]);
            } else {
                log_line("[spi] Datos corruptos (byte0=0x%02X): revisa SCK/MOSI/MISO y la longitud de cables",
                         rx[0]);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}
