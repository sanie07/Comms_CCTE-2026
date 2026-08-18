/**
 * @file    ax25.h
 * @brief   AX.25 UI frame encoder and decoder for APRS.
 *
 * Encoder (TX)
 * ─────────────
 *  AX25_BuildTxFrame() assembles a complete AX.25 UI frame from aprs_config.h
 *  fields.  AX25_BuildDigiTxFrame() re-encodes an already-modified raw frame
 *  (used by the digipeater after updating the path in-place).
 *  Both functions apply bit stuffing, CRC-16 CCITT, preamble and postamble
 *  flags, and load the result into the AFSK modem via AFSK_TX_Load().
 *
 * Decoder (RX)
 * ─────────────
 *  AX25_RxBit() processes one NRZ-decoded bit at a time.
 *  It detects HDLC flags (6 consecutive 1s followed by a 0),
 *  removes stuffed bits, assembles frame bytes, validates CRC, and
 *  calls the registered callbacks with the decoded data:
 *    • AX25_RxCallback_t  — receives (info, infoLen, srcCall)
 *    • AX25_DigiCallback_t — receives (rawFrame, frameLen) without FCS
 *
 * CRC-16 CCITT
 * ─────────────
 *  Polynomial 0x8408 (reflected), init 0xFFFF, complemented at the end.
 *  Transmitted FCS: low byte first, high byte second.
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
 *
 * @param info     Pointer to the information field (APRS payload string).
 * @param infoLen  Length of the information field in bytes.
 * @param srcCall  Zero-terminated source callsign (e.g. "W1ABC-9").
 */
typedef void (*AX25_RxCallback_t)(const uint8_t *info,
                                   uint16_t       infoLen,
                                   const char    *srcCall);

/**
 * Raw-frame callback for the digipeater.
 * Invoked after FCS validation, delivers the complete AX.25 frame bytes
 * (address + ctrl + pid + info) WITHOUT the 2-byte FCS trailer.
 * Called from within the TIM2 ISR — keep implementation ISR-safe.
 *
 * @param frame    Pointer to the raw frame buffer (dest + src + path + ctrl + pid + info).
 * @param frameLen Number of bytes in frame (FCS not included).
 */
typedef void (*AX25_DigiCallback_t)(const uint8_t *frame, uint16_t frameLen);

/**
 * @brief  Register only an info RX callback and reset the decoder.
 *         Equivalent to AX25_Init2(cb, NULL).
 * @param  cb  Info callback, or NULL to disable.
 */
void AX25_Init(AX25_RxCallback_t cb);

/**
 * @brief  Register both the info callback and the raw-frame (digi) callback,
 *         then reset the decoder state machine.
 * @param  infoCb   Info callback, or NULL to disable.
 * @param  digiCb   Raw-frame callback, or NULL to disable.
 */
void AX25_Init2(AX25_RxCallback_t infoCb, AX25_DigiCallback_t digiCb);

/**
 * @brief  Build a complete APRS UI frame and load it into the AFSK TX buffer.
 *
 *  Destination, source, and digipeater path are taken from aprs_config.h.
 *  The frame includes preamble flags, bit stuffing, FCS, and postamble flags.
 *
 * @param  info     Pointer to the APRS information field bytes.
 * @param  infoLen  Length of the information field (<= AX25_MAX_INFO_LEN).
 * @return true on success, false if the bit buffer would overflow.
 */
bool AX25_BuildTxFrame(const uint8_t *info, uint16_t infoLen);

/**
 * @brief  Re-encode and transmit a digipeated frame.
 *
 *  Takes a raw AX.25 frame (address + ctrl + pid + info, NO FCS) that has
 *  already been modified by the digipeater (path updated), recomputes FCS,
 *  wraps with preamble/postamble flags, applies bit stuffing, and loads the
 *  resulting bit-stream into the AFSK modem via AFSK_TX_Load().
 *
 * @param  frame     Pointer to the modified raw frame (without FCS).
 * @param  frameLen  Length of the frame in bytes.
 * @return true on success, false if the bit buffer would overflow.
 */
bool AX25_BuildDigiTxFrame(const uint8_t *frame, uint16_t frameLen);

/**
 * @brief  Feed one NRZ bit from the AFSK demodulator into the decoder.
 *
 *  Internally handles:
 *    - HDLC flag detection (6 ones + 0)
 *    - Bit de-stuffing (drop 0 after 5 consecutive 1s)
 *    - Frame byte assembly
 *    - CRC-16 CCITT validation
 *    - Callback invocation on valid frames
 *
 * @param  bit  Decoded NRZ bit (0 or 1).
 */
void AX25_RxBit(uint8_t bit);

#endif /* AX25_H */
