#include "sx1278_fsk.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "sx1278_tx";

/* =========================================================================
 * SX1278 Register Definitions (FSK / GFSK Mode)
 * ========================================================================= */
#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_BITRATE_MSB         0x02
#define REG_BITRATE_LSB         0x03
#define REG_FDEV_MSB            0x04
#define REG_FDEV_LSB            0x05
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
#define REG_PA_CONFIG           0x09
#define REG_PA_RAMP             0x0A
#define REG_OCP                 0x0B
#define REG_RX_CONFIG           0x0D
#define REG_RSSI_CONFIG         0x0E
#define REG_RSSI_THRESH         0x10
#define REG_RSSI_VALUE          0x11
#define REG_RX_BW               0x12
#define REG_AFC_BW              0x13
#define REG_FEI_MSB             0x1D
#define REG_FEI_LSB             0x1E
#define REG_PREAMBLE_DETECT     0x1F
#define REG_RX_TIMEOUT_1        0x20
#define REG_RX_TIMEOUT_2        0x21
#define REG_RX_TIMEOUT_3        0x22
#define REG_PREAMBLE_MSB        0x25
#define REG_PREAMBLE_LSB        0x26
#define REG_SYNC_CONFIG         0x27
#define REG_SYNC_VALUE_1        0x28
#define REG_PACKET_CONFIG_1     0x30
#define REG_PACKET_CONFIG_2     0x31
#define REG_PAYLOAD_LENGTH      0x32
#define REG_FIFO_THRESH         0x35
#define REG_IRQ_FLAGS_1         0x3E
#define REG_IRQ_FLAGS_2         0x3F
#define REG_DIO_MAPPING_1       0x40
#define REG_VERSION             0x42
#define REG_PA_DAC              0x4D
#define REG_BITRATE_FRAC        0x5D

/* Operating Modes */
#define OP_SLEEP                0x00
#define OP_STANDBY              0x01
#define OP_FSTX                 0x02
#define OP_TX                   0x03
#define OP_FSRX                 0x04
#define OP_RX                   0x05
#define MODEM_FSK               0x00

/* Frequency & Timing Constants */
#define FXOSC_HZ                32000000u
#define FSTEP_HZ                (FXOSC_HZ / 524288.0f)

/* Supported Chip Silicon Revisions */
#define CHIP_VER_SX1278         0x12
#define CHIP_VER_ALT            0x13
#define CHIP_VER_RFM9X          0x11

/* Filter & Modulation Settings */
#define RX_BW_20_8KHZ           0x14   /* Mant=24, Exp=4 */
#define PA_RAMP_GAUSS_BT_05     0x40   /* Gaussian filter BT = 0.5 */
#define SYNC_ON                 0x10
#define PREAMBLE_POL_55         0x20
#define PACKET_VARIABLE         0x80   /* Variable length packet format */
#define DATA_MODE_PACKET        0x40
#define CRC_ON                  0x10
#define AFC_AUTO_ON             0x10
#define AGC_AUTO_ON             0x08
#define RX_TRIG_PREAMBLE        0x06
#define PREAMBLE_DET_ON         0x80
#define PREAMBLE_DET_2B         0x20
#define PREAMBLE_DET_TOL        0x0A
#define TX_START_FIFO_NE        0x80   /* Start TX when at least 1 byte in FIFO */
#define FIFO_THRESH_DEFAULT     0x1F
#define OCP_ON                  0x20

/* IRQ Flags (Register 0x3F) */
#define FLAG_FIFO_FULL          0x80
#define FLAG_FIFO_EMPTY         0x40
#define FLAG_FIFO_LEVEL         0x20
#define FLAG_FIFO_OVERRUN       0x10
#define FLAG_PACKET_SENT        0x08   /* Set when packet TX finishes */
#define FLAG_PAYLOAD_READY      0x04
#define FLAG_CRC_OK             0x02
#define FLAG_LOW_BAT            0x01

/* PA Power Settings */
#define PA_BOOST_PIN            0x80
#define PA_DAC_HIGH_POWER       0x87
#define PA_DAC_DEFAULT_POWER    0x84

static spi_device_handle_t s_spi;
static sx1278_pins_t s_pins;
static uint8_t s_chip_ver = 0;
static SemaphoreHandle_t s_tx_done_sem = NULL;

/* =========================================================================
 * Low-Level SPI & Register Access
 * ========================================================================= */
static esp_err_t spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = (uint32_t)(len * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t write_reg(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(addr | 0x80u), val };
    return spi_xfer(tx, NULL, 2);
}

static esp_err_t read_reg(uint8_t addr, uint8_t *val)
{
    uint8_t tx[2] = { (uint8_t)(addr & 0x7Fu), 0x00 };
    uint8_t rx[2] = { 0 };
    esp_err_t err = spi_xfer(tx, rx, 2);
    if (err == ESP_OK) {
        *val = rx[1];
    }
    return err;
}

static esp_err_t write_burst(uint8_t addr, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + SX1278_FIFO_SIZE];
    if (len > SX1278_FIFO_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = (uint8_t)(addr | 0x80u);
    memcpy(&buf[1], data, len);
    return spi_xfer(buf, NULL, 1 + len);
}

static esp_err_t read_burst(uint8_t addr, uint8_t *data, size_t len)
{
    uint8_t tx[1 + SX1278_FIFO_SIZE] = { (uint8_t)(addr & 0x7Fu) };
    uint8_t rx[1 + SX1278_FIFO_SIZE] = { 0 };
    if (len > SX1278_FIFO_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = spi_xfer(tx, rx, 1 + len);
    if (err == ESP_OK) {
        memcpy(data, &rx[1], len);
    }
    return err;
}

static void hw_reset(void)
{
    gpio_set_level((gpio_num_t)s_pins.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level((gpio_num_t)s_pins.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t set_mode(uint8_t op_mode)
{
    uint8_t reg = 0;
    ESP_ERROR_CHECK(read_reg(REG_OP_MODE, &reg));
    reg = (uint8_t)((reg & ~0x07u) | (op_mode & 0x07u));
    return write_reg(REG_OP_MODE, reg);
}

static esp_err_t set_frequency(uint32_t freq_hz)
{
    uint64_t frf = ((uint64_t)freq_hz << 19) / FXOSC_HZ;
    ESP_ERROR_CHECK(write_reg(REG_FRF_MSB, (uint8_t)((frf >> 16) & 0xFFu)));
    ESP_ERROR_CHECK(write_reg(REG_FRF_MID, (uint8_t)((frf >> 8) & 0xFFu)));
    ESP_ERROR_CHECK(write_reg(REG_FRF_LSB, (uint8_t)(frf & 0xFFu)));
    return ESP_OK;
}

static esp_err_t set_bitrate(uint32_t bps)
{
    float br_kbps = (float)bps / 1000.0f;
    uint16_t raw = (uint16_t)((32000.0f) / br_kbps);
    float rem = (32000.0f / br_kbps) - (float)raw;
    uint8_t frac = (uint8_t)(rem * 16.0f);
    ESP_ERROR_CHECK(write_reg(REG_BITRATE_MSB, (uint8_t)(raw >> 8)));
    ESP_ERROR_CHECK(write_reg(REG_BITRATE_LSB, (uint8_t)raw));
    ESP_ERROR_CHECK(write_reg(REG_BITRATE_FRAC, frac));
    return ESP_OK;
}

static esp_err_t set_fdev(uint32_t fdev_hz)
{
    uint16_t fdev = (uint16_t)(((uint64_t)fdev_hz << 19) / FXOSC_HZ);
    ESP_ERROR_CHECK(write_reg(REG_FDEV_MSB, (uint8_t)((fdev >> 8) & 0x3Fu)));
    ESP_ERROR_CHECK(write_reg(REG_FDEV_LSB, (uint8_t)fdev));
    return ESP_OK;
}

static void IRAM_ATTR sx1278_dio0_isr_handler(void *arg)
{
    BaseType_t hp = pdFALSE;
    if (s_tx_done_sem != NULL) {
        xSemaphoreGiveFromISR(s_tx_done_sem, &hp);
        if (hp == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static esp_err_t find_chip(void)
{
    for (int i = 0; i < 10; i++) {
        hw_reset();
        ESP_ERROR_CHECK(read_reg(REG_VERSION, &s_chip_ver));
        if (s_chip_ver == CHIP_VER_SX1278 || s_chip_ver == CHIP_VER_ALT ||
            s_chip_ver == CHIP_VER_RFM9X) {
            ESP_LOGI(TAG, "SX1278 silicon detected (VERSION=0x%02X)", s_chip_ver);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "No SX1278 responded (try %d/10) VERSION=0x%02X", i + 1, s_chip_ver);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_NOT_FOUND;
}

/* =========================================================================
 * Public Driver Implementation
 * ========================================================================= */

esp_err_t sx1278_set_tx_power(int8_t power_dbm)
{
    if (power_dbm < 2) power_dbm = 2;
    if (power_dbm > 17) power_dbm = 17;

    /* PA_BOOST pin mode: Pout = 17 - (15 - OutputPower) => OutputPower = power - 2 */
    uint8_t out_power = (uint8_t)(power_dbm - 2);
    uint8_t pa_config = (uint8_t)(PA_BOOST_PIN | 0x70u | (out_power & 0x0Fu));

    ESP_ERROR_CHECK(write_reg(REG_PA_CONFIG, pa_config));
    ESP_ERROR_CHECK(write_reg(REG_PA_DAC, PA_DAC_DEFAULT_POWER));
    ESP_LOGI(TAG, "PA Configured: PA_BOOST @ +%d dBm (reg=0x%02X)", power_dbm, pa_config);
    return ESP_OK;
}

esp_err_t sx1278_init(const sx1278_pins_t *pins, const sx1278_fsk_cfg_t *cfg)
{
    if (pins == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins = *pins;

    if (s_tx_done_sem == NULL) {
        s_tx_done_sem = xSemaphoreCreateBinary();
    }

    /* Configure Reset and LED GPIOs */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pins->rst) | (1ULL << pins->led),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level((gpio_num_t)pins->rst, 1);
    gpio_set_level((gpio_num_t)pins->led, 0);

    /* Configure DIO0 input with interrupt */
    io.pin_bit_mask = (1ULL << pins->dio0);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLDOWN_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));

    /* Initialize SPI Master on ESP32-C6 SPI2 Host */
    spi_bus_config_t bus = {
        .mosi_io_num = pins->mosi,
        .miso_io_num = pins->miso,
        .sclk_io_num = pins->sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1 + SX1278_FIFO_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = SPI_MASTER_FREQ_8M,
        .mode = 0,
        .spics_io_num = pins->cs,
        .queue_size = 1,
        .flags = 0,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    /* Detect Chip Silicon */
    ESP_ERROR_CHECK(find_chip());

    /* Hook DIO0 ISR */
    gpio_isr_handler_add((gpio_num_t)pins->dio0, sx1278_dio0_isr_handler, NULL);

    /* Put into Sleep to configure FSK Mode */
    ESP_ERROR_CHECK(set_mode(OP_SLEEP));
    ESP_ERROR_CHECK(write_reg(REG_OP_MODE, MODEM_FSK | OP_SLEEP));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));

    /* FSK RF parameters matching STM32WLE5 SubGHz PHY */
    ESP_ERROR_CHECK(set_frequency(cfg->frequency_hz));
    ESP_ERROR_CHECK(set_bitrate(cfg->bitrate_bps));
    ESP_ERROR_CHECK(set_fdev(cfg->fdev_hz));

    /* RX Bandwidth settings (used during receive) */
    ESP_ERROR_CHECK(write_reg(REG_RX_BW, RX_BW_20_8KHZ));
    ESP_ERROR_CHECK(write_reg(REG_AFC_BW, RX_BW_20_8KHZ));

    uint8_t rx_cfg = (uint8_t)(RX_TRIG_PREAMBLE | AGC_AUTO_ON);
    if (cfg->afc_on) {
        rx_cfg |= AFC_AUTO_ON;
    }
    ESP_ERROR_CHECK(write_reg(REG_RX_CONFIG, rx_cfg));

    /* Overcurrent Protection (OCP): 100 mA (raw = 9) */
    ESP_ERROR_CHECK(write_reg(REG_OCP, (uint8_t)(OCP_ON | 9)));

    /* Preamble Configuration (8 bytes of 0x55) */
    ESP_ERROR_CHECK(write_reg(REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble_bytes >> 8)));
    ESP_ERROR_CHECK(write_reg(REG_PREAMBLE_LSB, (uint8_t)cfg->preamble_bytes));

    /* Sync Word Configuration (3 bytes: 0xC1, 0x94, 0xC1) */
    uint8_t sync_cfg = (uint8_t)(PREAMBLE_POL_55 | SYNC_ON | (cfg->sync_len - 1u));
    ESP_ERROR_CHECK(write_reg(REG_SYNC_CONFIG, sync_cfg));
    ESP_ERROR_CHECK(write_burst(REG_SYNC_VALUE_1, cfg->sync_word, cfg->sync_len));

    /* Packet Format: Variable length, NRZ, no hardware CRC (handled in AX.25) */
    uint8_t pkt1 = PACKET_VARIABLE;
    if (cfg->crc_on) {
        pkt1 |= CRC_ON;
    }
    ESP_ERROR_CHECK(write_reg(REG_PACKET_CONFIG_1, pkt1));
    ESP_ERROR_CHECK(write_reg(REG_PACKET_CONFIG_2, DATA_MODE_PACKET));
    ESP_ERROR_CHECK(write_reg(REG_PAYLOAD_LENGTH, cfg->max_payload));

    /* FIFO Threshold: start TX as soon as first byte is written */
    ESP_ERROR_CHECK(write_reg(REG_FIFO_THRESH, (uint8_t)(TX_START_FIFO_NE | FIFO_THRESH_DEFAULT)));

    /* Gaussian Shaping Filter (BT = 0.5) */
    if (cfg->gaussian_bt_05) {
        uint8_t ramp = 0;
        ESP_ERROR_CHECK(read_reg(REG_PA_RAMP, &ramp));
        ramp = (uint8_t)((ramp & 0x9Fu) | PA_RAMP_GAUSS_BT_05);
        ESP_ERROR_CHECK(write_reg(REG_PA_RAMP, ramp));
    }

    /* Configure Power Amplifier */
    int8_t pwr = (cfg->tx_power_dbm > 0) ? cfg->tx_power_dbm : 14;
    ESP_ERROR_CHECK(sx1278_set_tx_power(pwr));

    /* Clear any pending IRQ flags */
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_1, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, 0xFF));

    ESP_LOGI(TAG, "SX1278 TX Initialized: %lu Hz, %lu bps, FDEV=%lu Hz, BT=0.5, Pwr=+%d dBm",
             (unsigned long)cfg->frequency_hz,
             (unsigned long)cfg->bitrate_bps,
             (unsigned long)cfg->fdev_hz,
             pwr);

    return ESP_OK;
}

esp_err_t sx1278_send_packet(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > SX1278_FSK_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Put radio in Standby mode */
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));

    /* 2. Configure DIO0 for PacketSent (bits 7:6 = 00 in TX mode) */
    uint8_t map = 0;
    ESP_ERROR_CHECK(read_reg(REG_DIO_MAPPING_1, &map));
    map = (uint8_t)(map & 0x3Fu);
    ESP_ERROR_CHECK(write_reg(REG_DIO_MAPPING_1, map));

    /* 3. Clear IRQ flags */
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_1, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, 0xFF));

    /* Drain semaphore */
    xSemaphoreTake(s_tx_done_sem, 0);

    /* 4. Write payload length byte + payload data to FIFO */
    uint8_t tx_buf[1 + SX1278_FIFO_SIZE];
    tx_buf[0] = (uint8_t)len;              /* Variable length packet header */
    memcpy(&tx_buf[1], data, len);
    ESP_ERROR_CHECK(write_burst(REG_FIFO, tx_buf, 1 + len));

    /* 5. Switch to TX mode to trigger transmission */
    ESP_ERROR_CHECK(set_mode(OP_TX));

    /* 6. Wait for DIO0 interrupt (PacketSent) or poll IRQ flags */
    /* At 1200 bps: ~8.3 ms per byte. 64 bytes takes ~530 ms. Max timeout = 2500 ms */
    TickType_t wait_ticks = pdMS_TO_TICKS(2500);
    bool sent = false;

    if (xSemaphoreTake(s_tx_done_sem, wait_ticks) == pdTRUE) {
        sent = true;
    } else {
        /* Fallback poll in case DIO0 line is not connected */
        for (int i = 0; i < 50; i++) {
            if (sx1278_packet_sent()) {
                sent = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    /* 7. Clear IRQ flags and return to Standby */
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_1, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, 0xFF));
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));

    if (!sent) {
        ESP_LOGE(TAG, "TX Timeout! Packet did not complete transmission.");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t sx1278_standby(void)
{
    return set_mode(OP_STANDBY);
}

esp_err_t sx1278_sleep(void)
{
    return set_mode(OP_SLEEP);
}

bool sx1278_packet_sent(void)
{
    uint8_t flags2 = 0;
    if (read_reg(REG_IRQ_FLAGS_2, &flags2) != ESP_OK) {
        return false;
    }
    return (flags2 & FLAG_PACKET_SENT) != 0;
}

esp_err_t sx1278_start_receive(void)
{
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));
    uint8_t map = 0;
    ESP_ERROR_CHECK(read_reg(REG_DIO_MAPPING_1, &map));
    map = (uint8_t)(map & 0x3Fu); /* DIO0 = PayloadReady */
    ESP_ERROR_CHECK(write_reg(REG_DIO_MAPPING_1, map));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_1, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, 0xFF));
    ESP_ERROR_CHECK(set_mode(OP_RX));
    return ESP_OK;
}

esp_err_t sx1278_read_packet(uint8_t *data, size_t max_len, size_t *len)
{
    if (data == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t pkt_len = 0;
    ESP_ERROR_CHECK(read_reg(REG_FIFO, &pkt_len));
    if (pkt_len > SX1278_FSK_MAX_PAYLOAD) {
        pkt_len = SX1278_FSK_MAX_PAYLOAD;
    }
    size_t n = (pkt_len < max_len) ? pkt_len : max_len;
    if (n > 0) {
        ESP_ERROR_CHECK(read_burst(REG_FIFO, data, n));
    }
    *len = n;
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_1, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, 0xFF));
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));
    return ESP_OK;
}

esp_err_t sx1278_read_status(uint8_t *irq1, uint8_t *irq2, float *rssi_dbm)
{
    uint8_t i1 = 0, i2 = 0, rssi_raw = 0;
    ESP_ERROR_CHECK(read_reg(REG_IRQ_FLAGS_1, &i1));
    ESP_ERROR_CHECK(read_reg(REG_IRQ_FLAGS_2, &i2));
    ESP_ERROR_CHECK(read_reg(REG_RSSI_VALUE, &rssi_raw));
    if (irq1) *irq1 = i1;
    if (irq2) *irq2 = i2;
    if (rssi_dbm) *rssi_dbm = -(float)rssi_raw / 2.0f;
    return ESP_OK;
}

uint8_t sx1278_chip_version(void)
{
    return s_chip_ver;
}
