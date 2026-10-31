#pragma once

/* Packet exchanged with the STM32WLE5 RX over SPI.
 * MUST match Core/Inc/spi_link.h in TX_TEST1_STM32TRX. */

#include <stdint.h>

#define SPI_LINK_PACKET_SIZE        96U
#define SPI_LINK_MAGIC              0xAAU

#define SPI_LINK_CMD_POLL           0x00U

#define SPI_LINK_STATUS_RX_FRAME    0x0AU
#define SPI_LINK_STATUS_RX_IDLE     0x0CU

#define SPI_LINK_FROM_LEN           10U
#define SPI_LINK_MSG_LEN            59U

typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  status;
    uint16_t seq;
    uint32_t uptime_ms;
    uint32_t rx_ok;
    uint32_t rx_bad;
    uint32_t rx_errors;
    int16_t  rssi_dbm;
    int8_t   cfo_khz;
    uint8_t  fcs_ok;
    uint8_t  bytes;
    uint8_t  msg_len;
    char     from[SPI_LINK_FROM_LEN];
    char     msg[SPI_LINK_MSG_LEN];
    uint8_t  checksum;
} spi_link_packet_t;

_Static_assert(sizeof(spi_link_packet_t) == SPI_LINK_PACKET_SIZE,
               "spi_link_packet_t must be SPI_LINK_PACKET_SIZE bytes");
