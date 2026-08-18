/**
 * @file    app.h
 * @brief   Application layer — APRS tracker/digipeater with DRA818 + SPI debug.
 */

#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

/* ---- SPI status bytes (1 byte sent per master clock cycle) ---- */
#define SPI_STATUS_INIT          0x00U  /* Boot, not yet initialized          */
#define SPI_STATUS_HANDSHAKE_OK  0x01U  /* DRA818 AT handshake succeeded      */
#define SPI_STATUS_HANDSHAKE_ERR 0x02U  /* DRA818 AT handshake failed         */
#define SPI_STATUS_TX_ACTIVE     0x03U  /* AFSK TX in progress                */
#define SPI_STATUS_TX_DONE       0x04U  /* AFSK TX just finished              */
#define SPI_STATUS_GPS_FIX       0x05U  /* GPS has valid fix                  */
#define SPI_STATUS_GPS_WAIT      0x06U  /* Waiting for GPS fix                */
#define SPI_STATUS_LOOPBACK_OK   0x07U  /* AX.25 loopback frame decoded OK    */
#define SPI_STATUS_LOOPBACK_ERR  0x08U  /* AX.25 loopback frame not decoded   */
/* 0x09 reserved */
#define SPI_STATUS_RX_FRAME      0x0AU  /* Header: raw AX.25 frame bytes follow */
#define SPI_STATUS_DIGI_TX       0x0BU  /* Digipeater re-transmission active  */
#define SPI_STATUS_RX_IDLE       0x0CU  /* Listening, no frame pending        */
/* RX monitor mode */
#define SPI_STATUS_RX_SEEN       0x0DU  /* Frame decoded — brief pulse before dump */

void App_Init(void);
void App_Run(void);
void App_AFSK_TimerCallback(void);

bool App_GpsHasFix(void);
void App_GetGpsCoords(int *lat_int, int *lat_frac, int *lng_int, int *lng_frac);

#endif /* APP_H */
