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

typedef struct {
    int sck;
    int miso;
    int mosi;
    int cs;
    int rst;
    int dio0;
    int led;
} sx1278_pins_t;

typedef struct {
    uint32_t frequency_hz;
    uint32_t bitrate_bps;
    uint32_t fdev_hz;
    uint32_t rx_bw_hz;
    uint16_t preamble_bytes;
    uint8_t  sync_word[8];
    uint8_t  sync_len;
    uint8_t  max_payload;
    bool     crc_on;
    bool     afc_on;
    bool     gaussian_bt_05;
} sx1278_fsk_cfg_t;

esp_err_t sx1278_init(const sx1278_pins_t *pins, const sx1278_fsk_cfg_t *cfg);
esp_err_t sx1278_start_receive(void);
esp_err_t sx1278_read_packet(uint8_t *data, size_t max_len, size_t *len);
float     sx1278_get_rssi_dbm(void);
float     sx1278_get_freq_error_hz(void);
uint8_t   sx1278_chip_version(void);

#ifdef __cplusplus
}
#endif
