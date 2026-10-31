/**
  ******************************************************************************
  * @file    spi_link.h
  * @brief   SPI1 slave link: exposes the last 433 MHz message to an external
  *          SPI master (ESP32-C6 viewer, RX_TEST1_ESP32C6_SPI).
  *
  *  Bus: SPI1 slave, Mode 0 (CPOL=0, CPHA=0), MSB first, <= 1 MHz,
  *       PA1 SCK, PB5 MOSI, PB4 MISO, PA0 CS (EXTI, active LOW).
  *  Every transaction is exactly SPI_LINK_PACKET_SIZE bytes, full duplex:
  *    master -> STM32 : poll packet (magic + cmd, rest ignored)
  *    STM32 -> master : SpiLinkPacket_t with the last received message
  *  Master: CS low, wait >= 100 us, clock bytes with ~20 us gaps, CS high
  *  (same timing as Test10_DRA_SPI).
  *
  *  The packet layout MUST match main/spi_link_proto.h on the ESP32-C6.
  ******************************************************************************
  */
#ifndef SPI_LINK_H
#define SPI_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_LINK_PACKET_SIZE        96U
#define SPI_LINK_MAGIC              0xAAU

/* Master -> slave command */
#define SPI_LINK_CMD_POLL           0x00U

/* Slave status (codes from the board SPI protocol table in CONTEXT.md) */
#define SPI_LINK_STATUS_RX_FRAME    0x0AU  /* at least one message received */
#define SPI_LINK_STATUS_RX_IDLE     0x0CU  /* listening, nothing received yet */

#define SPI_LINK_FROM_LEN           10U
#define SPI_LINK_MSG_LEN            59U

typedef struct __attribute__((packed))
{
  uint8_t  magic;                     /*  0: 0xAA                                 */
  uint8_t  status;                    /*  1: SPI_LINK_STATUS_*                    */
  uint16_t seq;                       /*  2: packets received, changes per packet */
  uint32_t uptime_ms;                 /*  4: STM32 HAL_GetTick() at arm time      */
  uint32_t rx_ok;                     /*  8: packets with FCS OK                  */
  uint32_t rx_bad;                    /* 12: FCS fail / too short                 */
  uint32_t rx_errors;                 /* 16: radio RX timeout / error events      */
  int16_t  rssi_dbm;                  /* 20: last packet RSSI                     */
  int8_t   cfo_khz;                   /* 22: last packet carrier offset           */
  uint8_t  fcs_ok;                    /* 23: last packet FCS (1 = OK)             */
  uint8_t  bytes;                     /* 24: last packet bytes on air             */
  uint8_t  msg_len;                   /* 25: last packet AX.25 info length        */
  char     from[SPI_LINK_FROM_LEN];   /* 26: source "CALL-SSID", NUL terminated   */
  char     msg[SPI_LINK_MSG_LEN];     /* 36: info text (printable), NUL term.     */
  uint8_t  checksum;                  /* 95: XOR of bytes 0..94                   */
} SpiLinkPacket_t;

/**
  * @brief  Enable EXTI0 (CS) and arm the first SPI1 slave transfer.
  *         MX_SPI1_Init() must have run before.
  */
void SpiLink_Init(void);

/**
  * @brief  Publish new data for the master. magic, uptime_ms and checksum are
  *         filled by the driver. Safe to call from task context.
  */
void SpiLink_Publish(const SpiLinkPacket_t *pkt);

/**
  * @brief  Diagnostic counters (complete transfers / resyncs after a broken
  *         transaction).
  */
uint32_t SpiLink_GetTransfers(void);
uint32_t SpiLink_GetResyncs(void);

#ifdef __cplusplus
}
#endif

#endif /* SPI_LINK_H */
