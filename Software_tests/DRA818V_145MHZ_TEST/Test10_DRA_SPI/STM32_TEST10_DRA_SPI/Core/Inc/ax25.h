/**
 * @file    ax25.h
 * @brief   AX.25 UI frame encoder and decoder for APRS.
 */

#ifndef AX25_H
#define AX25_H

#include <stdint.h>
#include <stdbool.h>
#include "aprs_config.h"

/* AX.25 constants */
#define AX25_FLAG       0x7EU   /**< HDLC flag byte                   */
#define AX25_UI_CTRL    0x03U   /**< Unnumbered Information control   */
#define AX25_PID_NO_L3  0xF0U   /**< No Layer-3 protocol              */

/**
 * Callback invoked by AX25_RxBit() when a valid APRS frame is received.
 */
typedef void (*AX25_RxCallback_t)(const uint8_t *info,
                                   uint16_t       infoLen,
                                   const char    *srcCall);

void AX25_Init(AX25_RxCallback_t cb);

/**
 * @brief  Build a complete APRS UI frame and load it into the AFSK TX buffer.
 */
bool AX25_BuildTxFrame(const uint8_t *info, uint16_t infoLen);

/**
 * @brief  Feed one NRZ bit from the AFSK demodulator into the decoder.
 */
void AX25_RxBit(uint8_t bit);

#endif /* AX25_H */
