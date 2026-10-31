/**
 * @file    ax25.h
 * @brief   AX.25 UI frame encoder and decoder for APRS.
 *
 * Encoder (TX)
 * ─────────────
 *  AX25_BuildTxFrame() assembles the complete AX.25 UI frame:
 *    [preamble flags] [address field] [ctrl 0x03] [pid 0xF0]
 *    [information]    [FCS 2 bytes]   [postamble flags]
 *  Bit stuffing and CRC-16 CCITT are applied.  The resulting packed
 *  NRZ bit-stream is loaded into the AFSK modem via AFSK_TX_Load().
 *
 * Decoder (RX)
 * ─────────────
 *  AX25_RxBit() processes one NRZ-decoded bit at a time.
 *  It detects HDLC flags (6 consecutive 1s followed by a 0),
 *  removes stuffed bits, assembles frame bytes, validates CRC, and
 *  calls the registered callback with the information field.
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
 * @brief  Register an RX callback and reset the decoder state machine.
 * @param  cb  Callback function pointer, or NULL to disable RX decoding.
 */
void AX25_Init(AX25_RxCallback_t cb);

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
