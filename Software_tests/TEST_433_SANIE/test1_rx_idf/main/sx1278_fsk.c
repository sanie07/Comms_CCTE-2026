#include "sx1278_fsk.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sx1278";

#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_BITRATE_MSB         0x02
#define REG_BITRATE_LSB         0x03
#define REG_FDEV_MSB            0x04
#define REG_FDEV_LSB            0x05
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
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
#define REG_BITRATE_FRAC        0x5D

#define OP_SLEEP                0x00
#define OP_STANDBY              0x01
#define OP_RX                   0x05
#define MODEM_FSK               0x00

#define FXOSC_HZ                32000000u
#define FSTEP_HZ                (FXOSC_HZ / 524288.0f)

#define CHIP_VER_SX1278         0x12
#define CHIP_VER_ALT            0x13
#define CHIP_VER_RFM9X          0x11

#define RX_BW_20_8KHZ           0x14   /* Mant=24, Exp=4 */
#define PA_RAMP_GAUSS_BT_05     0x40
#define SYNC_ON                 0x10
#define PREAMBLE_POL_55         0x20
#define PACKET_VARIABLE         0x80
#define DATA_MODE_PACKET        0x40
#define CRC_ON                  0x10
#define AFC_AUTO_ON             0x10
#define AGC_AUTO_ON             0x08
#define RX_TRIG_PREAMBLE        0x06   /* RadioLib RX_TRIGGER_PREAMBLE_DETECT */
#define PREAMBLE_DET_ON         0x80
#define PREAMBLE_DET_2B         0x20
#define PREAMBLE_DET_TOL        0x0A
#define TX_START_FIFO_NE        0x80
#define FIFO_THRESH_DEFAULT     0x1F
#define FLAG_FIFO_OVERRUN       0x10
#define FLAG_PAYLOAD_READY      0x04
#define FLAG_PREAMBLE_DETECT    0x02
#define FLAG_SYNC_ADDRESS_MATCH 0x01
#define OCP_ON                  0x20

static spi_device_handle_t s_spi;
static sx1278_pins_t s_pins;
static uint8_t s_chip_ver;
static float s_last_rssi_dbm;
static float s_last_fei_hz;

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
    return spi_xfer(buf, NULL, len + 1);
}

static esp_err_t read_burst(uint8_t addr, uint8_t *data, size_t len)
{
    uint8_t tx[1 + SX1278_FIFO_SIZE] = { 0 };
    uint8_t rx[1 + SX1278_FIFO_SIZE] = { 0 };
    if (len > SX1278_FIFO_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    tx[0] = (uint8_t)(addr & 0x7Fu);
    esp_err_t err = spi_xfer(tx, rx, len + 1);
    if (err == ESP_OK) {
        memcpy(data, &rx[1], len);
    }
    return err;
}

static esp_err_t set_mode(uint8_t mode)
{
    uint8_t op = 0;
    ESP_ERROR_CHECK(read_reg(REG_OP_MODE, &op));
    op = (uint8_t)((op & 0xF8u) | (mode & 0x07u));
    ESP_ERROR_CHECK(write_reg(REG_OP_MODE, op));
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

static void hw_reset(void)
{
    gpio_set_level((gpio_num_t)s_pins.rst, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level((gpio_num_t)s_pins.rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t set_frequency(uint32_t freq_hz)
{
    uint32_t frf = (uint32_t)(((uint64_t)freq_hz << 19) / FXOSC_HZ);
    ESP_ERROR_CHECK(write_reg(REG_FRF_MSB, (uint8_t)(frf >> 16)));
    ESP_ERROR_CHECK(write_reg(REG_FRF_MID, (uint8_t)(frf >> 8)));
    ESP_ERROR_CHECK(write_reg(REG_FRF_LSB, (uint8_t)frf));
    return ESP_OK;
}

static esp_err_t set_bitrate(uint32_t bps)
{
    /* Match RadioLib: BitRate = (32 MHz / 1000) / (kbps) */
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

static esp_err_t find_chip(void)
{
    for (int i = 0; i < 10; i++) {
        hw_reset();
        ESP_ERROR_CHECK(read_reg(REG_VERSION, &s_chip_ver));
        if (s_chip_ver == CHIP_VER_SX1278 || s_chip_ver == CHIP_VER_ALT ||
            s_chip_ver == CHIP_VER_RFM9X) {
            ESP_LOGI(TAG, "chip version 0x%02X", s_chip_ver);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "no SX1278 (try %d) VERSION=0x%02X", i + 1, s_chip_ver);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sx1278_init(const sx1278_pins_t *pins, const sx1278_fsk_cfg_t *cfg)
{
    if (pins == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins = *pins;

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

    io.pin_bit_mask = (1ULL << pins->dio0);
    io.mode = GPIO_MODE_INPUT;
    io.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));

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

    ESP_ERROR_CHECK(find_chip());
    ESP_ERROR_CHECK(set_mode(OP_SLEEP));
    ESP_ERROR_CHECK(write_reg(REG_OP_MODE, MODEM_FSK | OP_SLEEP));
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));

    /* FSK packet config matching RadioLib beginFSK + Path A overrides. */
    ESP_ERROR_CHECK(set_frequency(cfg->frequency_hz));
    ESP_ERROR_CHECK(set_bitrate(cfg->bitrate_bps));
    ESP_ERROR_CHECK(set_fdev(cfg->fdev_hz));

    ESP_ERROR_CHECK(write_reg(REG_RX_BW, RX_BW_20_8KHZ));
    ESP_ERROR_CHECK(write_reg(REG_AFC_BW, RX_BW_20_8KHZ));

    /* Preamble trigger matches RadioLib setAFCAGCTrigger(PREAMBLE); AFC optional. */
    uint8_t rx_cfg = (uint8_t)(RX_TRIG_PREAMBLE | AGC_AUTO_ON);
    if (cfg->afc_on) {
        rx_cfg |= AFC_AUTO_ON;
    }
    ESP_ERROR_CHECK(write_reg(REG_RX_CONFIG, rx_cfg));

    /* OCP 60 mA: raw = (60-45)/5 = 3 */
    ESP_ERROR_CHECK(write_reg(REG_OCP, (uint8_t)(OCP_ON | 3)));

    ESP_ERROR_CHECK(write_reg(REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble_bytes >> 8)));
    ESP_ERROR_CHECK(write_reg(REG_PREAMBLE_LSB, (uint8_t)cfg->preamble_bytes));

    uint8_t sync_cfg = (uint8_t)(PREAMBLE_POL_55 | SYNC_ON | (cfg->sync_len - 1u));
    ESP_ERROR_CHECK(write_reg(REG_SYNC_CONFIG, sync_cfg));
    ESP_ERROR_CHECK(write_burst(REG_SYNC_VALUE_1, cfg->sync_word, cfg->sync_len));

    ESP_ERROR_CHECK(write_reg(REG_RSSI_THRESH, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, FLAG_FIFO_OVERRUN));

    uint8_t pkt1 = PACKET_VARIABLE; /* NRZ, CRC off/on, no addr filter, CCITT */
    if (cfg->crc_on) {
        pkt1 |= CRC_ON;
    }
    ESP_ERROR_CHECK(write_reg(REG_PACKET_CONFIG_1, pkt1));
    ESP_ERROR_CHECK(write_reg(REG_PACKET_CONFIG_2, DATA_MODE_PACKET));
    ESP_ERROR_CHECK(write_reg(REG_PAYLOAD_LENGTH, cfg->max_payload));
    ESP_ERROR_CHECK(write_reg(REG_FIFO_THRESH, (uint8_t)(TX_START_FIFO_NE | FIFO_THRESH_DEFAULT)));

    ESP_ERROR_CHECK(write_reg(REG_RX_TIMEOUT_1, 0x00));
    ESP_ERROR_CHECK(write_reg(REG_RX_TIMEOUT_2, 0x00));
    ESP_ERROR_CHECK(write_reg(REG_RX_TIMEOUT_3, 0x00));
    ESP_ERROR_CHECK(write_reg(REG_PREAMBLE_DETECT,
                              (uint8_t)(PREAMBLE_DET_ON | PREAMBLE_DET_2B | PREAMBLE_DET_TOL)));

    /* RSSI smoothing samples = 8 (RadioLib setRSSIConfig(2)) */
    ESP_ERROR_CHECK(write_reg(REG_RSSI_CONFIG, 0x02));

    if (cfg->gaussian_bt_05) {
        uint8_t ramp = 0;
        ESP_ERROR_CHECK(read_reg(REG_PA_RAMP, &ramp));
        ramp = (uint8_t)((ramp & 0x9Fu) | PA_RAMP_GAUSS_BT_05);
        ESP_ERROR_CHECK(write_reg(REG_PA_RAMP, ramp));
    }

    ESP_LOGI(TAG, "FSK %lu Hz  %lu bps  fdev %lu Hz  BT=0.5  sync %u bytes",
             (unsigned long)cfg->frequency_hz,
             (unsigned long)cfg->bitrate_bps,
             (unsigned long)cfg->fdev_hz,
             (unsigned)cfg->sync_len);
    return ESP_OK;
}

esp_err_t sx1278_start_receive(void)
{
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));
    /* DIO0 = PayloadReady in packet mode (bits 7:6 = 00) */
    uint8_t map = 0;
    ESP_ERROR_CHECK(read_reg(REG_DIO_MAPPING_1, &map));
    map = (uint8_t)(map & 0x3Fu);
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

    uint8_t flags2 = 0;
    uint8_t rssi_raw = 0;
    uint8_t fei_msb = 0;
    uint8_t fei_lsb = 0;
    ESP_ERROR_CHECK(read_reg(REG_IRQ_FLAGS_2, &flags2));
    ESP_ERROR_CHECK(read_reg(REG_RSSI_VALUE, &rssi_raw));
    ESP_ERROR_CHECK(read_reg(REG_FEI_MSB, &fei_msb));
    ESP_ERROR_CHECK(read_reg(REG_FEI_LSB, &fei_lsb));
    s_last_rssi_dbm = -(float)rssi_raw / 2.0f;
    s_last_fei_hz = (float)(int16_t)((uint16_t)fei_msb << 8 | fei_lsb) * FSTEP_HZ;

    uint8_t pkt_len = 0;
    ESP_ERROR_CHECK(read_reg(REG_FIFO, &pkt_len));
    // #region agent log
    ESP_LOGI(TAG, "DBG irq2=0x%02X payload_ready=%d overrun=%d fifo_len=%u",
             flags2, (flags2 & FLAG_PAYLOAD_READY) ? 1 : 0,
             (flags2 & FLAG_FIFO_OVERRUN) ? 1 : 0, (unsigned)pkt_len);
    // #endregion
    if (pkt_len > SX1278_FSK_MAX_PAYLOAD) {
        pkt_len = SX1278_FSK_MAX_PAYLOAD;
    }
    size_t n = pkt_len;
    if (n > max_len) {
        n = max_len;
    }
    if (n > 0) {
        ESP_ERROR_CHECK(read_burst(REG_FIFO, data, n));
    }
    *len = n;

    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_1, 0xFF));
    ESP_ERROR_CHECK(write_reg(REG_IRQ_FLAGS_2, 0xFF));
    ESP_ERROR_CHECK(set_mode(OP_STANDBY));

    if (flags2 & FLAG_FIFO_OVERRUN) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t sx1278_read_status(uint8_t *irq1, uint8_t *irq2, float *rssi_dbm)
{
    uint8_t i1 = 0;
    uint8_t i2 = 0;
    uint8_t rssi_raw = 0;
    ESP_ERROR_CHECK(read_reg(REG_IRQ_FLAGS_1, &i1));
    ESP_ERROR_CHECK(read_reg(REG_IRQ_FLAGS_2, &i2));
    ESP_ERROR_CHECK(read_reg(REG_RSSI_VALUE, &rssi_raw));
    if (irq1) {
        *irq1 = i1;
    }
    if (irq2) {
        *irq2 = i2;
    }
    if (rssi_dbm) {
        *rssi_dbm = -(float)rssi_raw / 2.0f;
    }
    return ESP_OK;
}

bool sx1278_payload_ready(void)
{
    uint8_t flags2 = 0;
    if (read_reg(REG_IRQ_FLAGS_2, &flags2) != ESP_OK) {
        return false;
    }
    return (flags2 & FLAG_PAYLOAD_READY) != 0;
}

float sx1278_get_rssi_dbm(void)
{
    return s_last_rssi_dbm;
}

float sx1278_get_freq_error_hz(void)
{
    return s_last_fei_hz;
}

uint8_t sx1278_chip_version(void)
{
    return s_chip_ver;
}
