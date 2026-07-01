/**
 * @file    aprs_config.h
 * @brief   APRS firmware configuration — edit before building.
 *
 * All station, radio, and modem parameters live here as compile-time
 * constants.  No USB, menu, or runtime configuration is used.
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  *** Replace "N0CALL" with your licensed amateur callsign ***   │
 * │  APRS transmissions without a valid callsign are illegal.       │
 * └─────────────────────────────────────────────────────────────────┘
 */

#ifndef APRS_CONFIG_H
#define APRS_CONFIG_H

#include <stdint.h>

/* ================================================================
 * 1. STATION IDENTIFICATION
 *    APRS requires a government-issued amateur radio callsign.
 *    "N0CALL" is a placeholder — replace it before going on-air.
 *    SSID is a numeric suffix (0–15).  SSID 0 has no suffix.
 * ================================================================ */
#define APRS_MYCALL         "ZPK6UJK"    /**< Your callsign, e.g. "W1ABC"    */
#define APRS_MYSSID         6           /**< SSID 0–15 (0 = no suffix)      */

/** AX.25 destination TOCALL.  "APRS" is the generic default. */
#define APRS_DESTCALL       "APRS"
#define APRS_DESTSSID       0

/** Digipeater path.  Set PATH1CALL to "" to disable both hops. */
#define APRS_PATH1CALL      "WIDE1"
#define APRS_PATH1SSID      1
#define APRS_PATH2CALL      "WIDE2"
#define APRS_PATH2SSID      1

/* ================================================================
 * 2. FIXED POSITION BEACON
 *    Format: DDMM.MMH  (degrees + decimal minutes + hemisphere)
 *    Examples:  "3456.78N"  "09812.34W"
 *    The symbol table '/' selects the primary symbol set.
 *    The symbol code '-' = house/home station.
 * ================================================================ */
#define APRS_LATITUDE       "0000.00N"  /**< Default lat when GPS has no fix */
#define APRS_LONGITUDE      "00000.00E" /**< Default lon when GPS has no fix */
#define APRS_SYMBOL_TABLE   '/'         /**< Primary symbol table        */
#define APRS_SYMBOL_CODE    '-'         /**< Symbol: house               */

/** Text appended to every beacon (keep under 43 chars for safety). */
#define APRS_BEACON_MSG     "STM32WLE5 APRS Beacon"

/** Beacon transmission interval in milliseconds (30 s). */
#define APRS_BEACON_INTERVAL_MS     5000UL

/* ================================================================
 * 3. DRA818V VHF RADIO MODULE
 *    Frequency range: 134.000–174.000 MHz, 100 Hz resolution.
 *    The DRA818 UART is fixed at 9600 baud — do not change.
 * ================================================================ */
#define DRA818_TX_FREQ      "145.0000"  /**< TX frequency (8-char string)   */
#define DRA818_RX_FREQ      "145.0000"  /**< RX frequency (8-char string)   */
#define DRA818_SQUELCH      1           /**< 0 = open; 1–8 = threshold      */
#define DRA818_CTCSS_TX     "0000"      /**< CTCSS code TX (0000 = none)    */
#define DRA818_CTCSS_RX     "0000"      /**< CTCSS code RX (0000 = none)    */
#define DRA818_BANDWIDTH    0           /**< 0 = 12.5 kHz  /  1 = 25 kHz   */
#define DRA818_VOLUME       5           /**< Speaker volume 1–8             */
#define DRA818_UART_BAUD    9600U       /**< Fixed by DRA818 hardware        */

/** Milliseconds to wait after asserting/de-asserting PTT. */
#define DRA818_PTT_ON_DELAY_MS      150U
#define DRA818_PTT_OFF_DELAY_MS     100U

/** Maximum seconds to wait for a clear channel before skipping TX. */
#define DRA818_CHANNEL_WAIT_SEC     5U

/* ================================================================
 * 4. AFSK BELL 202 MODEM
 *    System clock = 48 MHz (from CubeMX: HSE 32 MHz × PLL ÷2 ×6 ÷2)
 *    TIM2 ISR = 9 600 Hz  →  exactly 8 samples per 1 200-baud symbol.
 * ================================================================ */
#define AFSK_SAMPLE_RATE        9600U   /**< Hz — must match TIM2 ARR       */
#define AFSK_BAUD_RATE          1200U   /**< Symbols per second              */
#define AFSK_SAMPLES_PER_SYM    8U      /**< 9600 / 1200 = 8 (exact)        */
#define AFSK_MARK_HZ            1200U   /**< Mark  tone (NRZI logic-1)      */
#define AFSK_SPACE_HZ           2200U   /**< Space tone (NRZI logic-0)      */
#define AFSK_SINE_LEN           256U    /**< Entries in precomputed table    */
#define AFSK_DAC_MID            2048U   /**< 12-bit DAC mid-point (silence) */
#define AFSK_DAC_AMP            2000U   /**< Sine amplitude (peak deviation) */

/** ADC busy-detect threshold (0–4095, 12-bit).
 *  Samples above this level indicate an active received signal. */
#define AFSK_ADC_SQUELCH_THR    150U

/* ================================================================
 * 5. AX.25 FRAME PARAMETERS
 * ================================================================ */
#define AX25_PREAMBLE_FLAGS     20U     /**< 0x7E flags before frame        */
#define AX25_POSTAMBLE_FLAGS    8U      /**< 0x7E flags after frame         */
#define AX25_MAX_INFO_LEN       256U    /**< Max APRS information field     */

/** TX bit-stream buffer size in bytes (each byte holds 8 bits).
 *  512 bytes = 4096 bits — more than enough for any APRS frame. */
#define AFSK_TX_BUF_BYTES       512U

#endif /* APRS_CONFIG_H */
