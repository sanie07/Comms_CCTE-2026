/**
 * @file    app.h
 * @brief   Shared configuration for ESP32 SPI Master — Test10 Dual Test.
 *
 *  TEST_MODE 0: ESP32 sends hardcoded coords to STM32 via SPI for DRA818V TX.
 *  TEST_MODE 1: ESP32 reads raw coords from STM32 via SPI and displays them.
 *
 *  IMPORTANT: The SpiPacket_t struct and all constants MUST match
 *  the STM32 Core/Inc/app.h definitions byte-for-byte.
 */

#ifndef APP_H
#define APP_H

#include <stdint.h>

/* ================================================================
 * TEST MODE SELECTION — Must match STM32 app.h setting.
 * ================================================================
 *  0 = ESP32 coords -> STM32 -> DRA818V RF TX  (Test 1)
 *  1 = STM32 raw coords -> ESP32 Serial Display (Test 2)
 */
#define TEST_MODE_ESP_TO_STM32_DRA   0
#define TEST_MODE_STM32_TO_ESP_RAW   1

#define TEST_MODE                    TEST_MODE_ESP_TO_STM32_DRA

/* ================================================================
 * HARDCODED COORDINATES — Manually assign the coordinates here.
 * ================================================================
 * Used by TEST_MODE 0 (ESP32 -> STM32 DRA818V TX).
 * Latitude  in degrees * 1,000,000  (e.g. -25286700 = -25.286700 deg)
 * Longitude in degrees * 1,000,000  (e.g. -57647000 = -57.647000 deg)
 * Altitude  in centimeters          (e.g.     15000 = 150.00 m)
 */
#define ESP32_HARDCODED_LAT_E6       (-25335433L) 
#define ESP32_HARDCODED_LON_E6       (-57513295L)
#define ESP32_HARDCODED_ALT_CM       (15000L)

/* ================================================================
 * SPI Packet Protocol (32 bytes, shared with STM32)
 * ================================================================ */
#define SPI_PACKET_SIZE       32U
#define SPI_PACKET_MAGIC      0xAAU

/* Packet command / type IDs */
#define SPI_CMD_POLL_STATUS   0x00U  /* ESP32 requests status only           */
#define SPI_CMD_SEND_COORDS   0x01U  /* ESP32 -> STM32 (Test 1: coords)      */
#define SPI_CMD_RAW_TELEMETRY 0x02U  /* STM32 -> ESP32 (Test 2: raw coords)  */

/* SPI Status Codes (from STM32) */
#define SPI_STATUS_INIT          0x00U
#define SPI_STATUS_DRA_READY     0x01U
#define SPI_STATUS_DRA_ERR       0x02U
#define SPI_STATUS_TX_ACTIVE     0x03U
#define SPI_STATUS_TX_DONE       0x04U
#define SPI_STATUS_COORD_RCVD    0x05U
#define SPI_STATUS_READY         0x06U

typedef struct __attribute__((packed)) {
    uint8_t  magic;          /* 0xAA start delimiter                     */
    uint8_t  cmd;            /* Packet type / command                    */
    uint16_t seq_num;        /* Rolling packet counter                   */
    int32_t  lat_e6;         /* Latitude  * 1,000,000                    */
    int32_t  lon_e6;         /* Longitude * 1,000,000                    */
    int32_t  alt_cm;         /* Altitude in centimeters                  */
    uint32_t timestamp_ms;   /* Node uptime / timestamp                  */
    char     comment[10];    /* Text tag (null-terminated, max 9 chars)  */
    uint8_t  status;         /* STM32 status code                       */
    uint8_t  checksum;       /* XOR checksum of bytes 0..30              */
} SpiPacket_t;

#endif /* APP_H */
