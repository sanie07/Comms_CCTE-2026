/**
 * @file    app.h
 * @brief   Application layer — GPS APRS beacon over DRA818 + ESP32 SPI status.
 */

#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#define SPI_STATUS_INIT          0
#define SPI_STATUS_HANDSHAKE_OK  1
#define SPI_STATUS_HANDSHAKE_ERR 2
#define SPI_STATUS_TX_ACTIVE     3
#define SPI_STATUS_TX_DONE       4
#define SPI_STATUS_GPS_FIX       5
#define SPI_STATUS_GPS_WAIT      6
#define SPI_STATUS_LOOPBACK_OK   7
#define SPI_STATUS_LOOPBACK_ERR  8

void App_Init(void);
void App_Run(void);
void App_AFSK_TimerCallback(void);

bool App_GpsHasFix(void);
void App_GetGpsCoords(int *lat_int, int *lat_frac, int *lng_int, int *lng_frac);

#endif /* APP_H */
