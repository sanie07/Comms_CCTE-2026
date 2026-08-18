/**
 * @file    digipeater.h
 * @brief   APRS digipeater: WIDE1-1 simple and WIDE2-n traced path processing.
 *
 * Usage:
 *   1. Call Digi_Init() once during setup.
 *   2. Register Digi_ProcessFrame as the AX25_DigiCallback_t via AX25_Init2().
 *   3. In the main loop, poll Digi_IsPending().  When true, retrieve the
 *      modified frame with Digi_GetFrame(), call AX25_BuildDigiTxFrame(), then
 *      drive the PTT state machine.  Call Digi_ClearPending() after TX.
 *
 * Configuration is in aprs_config.h:
 *   APRS_DIGI_WIDE1_ENABLE  -- enable WIDE1-1 simple alias
 *   APRS_DIGI_WIDE2_ENABLE  -- enable WIDE2-n traced alias
 *   APRS_DIGI_WIDE2_MAX_N   -- maximum N to honour
 *   APRS_DIGI_DEDUPE_SECS   -- duplicate filter window in seconds
 *
 * Own callsign for path insertion: APRS_MYCALL / APRS_MYSSID (aprs_config.h).
 */

#ifndef DIGIPEATER_H
#define DIGIPEATER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  Initialise the digipeater: encode own callsign, clear dedupe table.
 *         Must be called once before Digi_ProcessFrame().
 */
void Digi_Init(void);

/**
 * @brief  Process a received raw AX.25 frame (no FCS).
 *         Checks duplicate filter, finds the next undigipeated path element,
 *         applies the appropriate alias rule (WIDE1-1 or WIDE2-n), and stores
 *         the modified frame in the pending TX slot.
 *         ISR-safe: uses only memcpy / flag writes.
 *
 * @param  frame     Raw frame bytes (dest + src + path + ctrl + pid + info).
 * @param  len       Frame length in bytes (FCS NOT included).
 */
void Digi_ProcessFrame(const uint8_t *frame, uint16_t len);

/**
 * @brief  Returns true if a digipeated frame is ready to transmit.
 */
bool Digi_IsPending(void);

/**
 * @brief  Retrieve pointer and length of the pending modified frame.
 * @param  buf  Receives pointer to internal pending-frame buffer.
 * @param  len  Receives number of bytes in the frame (without FCS).
 * @return true if a frame is available, false otherwise.
 */
bool Digi_GetFrame(uint8_t **buf, uint16_t *len);

/**
 * @brief  Clear the pending-TX flag after the frame has been transmitted.
 */
void Digi_ClearPending(void);

#endif /* DIGIPEATER_H */
