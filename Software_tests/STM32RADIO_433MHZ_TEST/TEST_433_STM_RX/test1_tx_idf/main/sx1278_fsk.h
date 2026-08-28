#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SX1278_FIFO_SIZE           64
#define SX1278_FSK_MAX_PAYLOAD     63

/**
 * @brief GPIO Pin configuration for ESP32-C6 <-> SX1278 / RFM98 connection.
 */
typedef struct {
    int sck;    /*!< SPI Clock pin */
    int miso;   /*!< SPI MISO pin */
    int mosi;   /*!< SPI MOSI pin */
    int cs;     /*!< SPI Chip Select pin (NSS) */
    int rst;    /*!< Hardware Reset pin (Active LOW) */
    int dio0;   /*!< DIO0 interrupt pin (PacketSent / PayloadReady) */
    int led;    /*!< Status / TX indicator LED pin */
} sx1278_pins_t;

/**
 * @brief RF & Modulation configuration for FSK / GFSK packet mode.
 */
typedef struct {
    uint32_t frequency_hz;      /*!< RF Center frequency in Hz (e.g. 433018893 Hz) */
    uint32_t bitrate_bps;       /*!< Bitrate in bps (e.g. 1200 bps) */
    uint32_t fdev_hz;           /*!< Frequency deviation in Hz (e.g. 5000 Hz) */
    uint32_t rx_bw_hz;          /*!< Receiver filter bandwidth in Hz (e.g. 20000 Hz) */
    uint16_t preamble_bytes;    /*!< Preamble length in bytes (e.g. 8 bytes of 0x55) */
    uint8_t  sync_word[8];      /*!< Sync word bytes (e.g. 0xC1, 0x94, 0xC1) */
    uint8_t  sync_len;          /*!< Length of sync word (1..8 bytes) */
    uint8_t  max_payload;       /*!< Maximum payload length (1..63 bytes) */
    int8_t   tx_power_dbm;      /*!< Output power in dBm (+2 to +17 dBm, PA_BOOST) */
    bool     crc_on;            /*!< Enable SX1278 hardware CRC */
    bool     afc_on;            /*!< Enable automatic frequency correction */
    bool     gaussian_bt_05;    /*!< Apply Gaussian filter BT=0.5 shaping */
} sx1278_fsk_cfg_t;

/**
 * @brief Initialize the SX1278 transceiver in FSK Packet Mode.
 *
 * @param pins Pointer to GPIO pin assignments.
 * @param cfg  Pointer to FSK configuration parameters.
 * @return esp_err_t ESP_OK on success, or error code on failure.
 */
esp_err_t sx1278_init(const sx1278_pins_t *pins, const sx1278_fsk_cfg_t *cfg);

/**
 * @brief Transmit a variable-length data packet.
 *
 * Puts the radio into Standby, writes length and payload bytes to FIFO,
 * switches to TX mode, and waits for DIO0 PacketSent interrupt/flag.
 * Returns to Standby upon completion.
 *
 * @param data Pointer to payload data buffer.
 * @param len  Length of payload (1 to 63 bytes).
 * @return esp_err_t ESP_OK if transmitted successfully, ESP_ERR_TIMEOUT if timeout occurred.
 */
esp_err_t sx1278_send_packet(const uint8_t *data, size_t len);

/**
 * @brief Set the RF output power.
 *
 * @param power_dbm Power level in dBm (+2 to +17 dBm using PA_BOOST).
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t sx1278_set_tx_power(int8_t power_dbm);

/**
 * @brief Switch the radio to Standby mode.
 */
esp_err_t sx1278_standby(void);

/**
 * @brief Switch the radio to Sleep mode (ultra-low power).
 */
esp_err_t sx1278_sleep(void);

/**
 * @brief Check if the PacketSent flag is set in IRQ flags register.
 */
bool sx1278_packet_sent(void);

/**
 * @brief Start continuous receiver mode (optional / bidirectional).
 */
esp_err_t sx1278_start_receive(void);

/**
 * @brief Read a received packet from FIFO (optional / bidirectional).
 */
esp_err_t sx1278_read_packet(uint8_t *data, size_t max_len, size_t *len);

/**
 * @brief Read modem IRQ flags and current RSSI level.
 */
esp_err_t sx1278_read_status(uint8_t *irq1, uint8_t *irq2, float *rssi_dbm);

/**
 * @brief Get the silicon revision version byte (expected 0x12 for SX1278, 0x11 for RFM98).
 */
uint8_t sx1278_chip_version(void);

#ifdef __cplusplus
}
#endif
