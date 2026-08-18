/**
 * @file    aprs_config.h
 * @brief   APRS station and radio configuration for Test9 TNC/Digipeater.
 */

#ifndef APRS_CONFIG_H
#define APRS_CONFIG_H

#include <stdint.h>

/* ================================================================
 * Station identity
 * Replace APRS_MYCALL before any on-air transmission.
 * ================================================================ */
#define APRS_MYCALL         "ZP6UJK"
#define APRS_MYSSID         6U
#define APRS_DESTCALL       "APNV01"
#define APRS_DESTSSID       0U
#define APRS_PATH1CALL      "WIDE1"
#define APRS_PATH1SSID      1U
#define APRS_PATH2CALL      "WIDE2"
#define APRS_PATH2SSID      1U

/* Fixed fallback position used by OTA and no-fix tracker modes. */
#define APRS_LATITUDE       "0000.00N"
#define APRS_LONGITUDE      "00000.00E"
#define APRS_SYMBOL_TABLE   '/'
#define APRS_SYMBOL_CODE    '-'
#define APRS_BEACON_MSG     "TEST9DIGI"
#define APRS_FIXED_INFO     "!0000.00N/00000.00E-TEST9DIGI"
#define APRS_BEACON_INTERVAL_MS  10000UL

/* ================================================================
 * DRA818V radio
 * ================================================================ */
#define DRA818_TX_FREQ      "145.8250"
#define DRA818_RX_FREQ      "145.8250"
#define DRA818_SQUELCH      1U
#define DRA818_CTCSS_TX     "0000"
#define DRA818_CTCSS_RX     "0000"
#define DRA818_BANDWIDTH    0U
#define DRA818_VOLUME       5U
#define DRA818_UART_BAUD    9600U
#define DRA818_PTT_ON_DELAY_MS   300U
#define DRA818_PTT_OFF_DELAY_MS  50U
#define DRA818_CHANNEL_WAIT_SEC  5U

/* ================================================================
 * Bell 202 AFSK modem — TIM2 must run at 9600 Hz.
 * ================================================================ */
#define AFSK_SAMPLE_RATE        9600U
#define AFSK_BAUD_RATE          1200U
#define AFSK_SAMPLES_PER_SYM    8U
#define AFSK_MARK_HZ            1200U
#define AFSK_SPACE_HZ           2200U
#define AFSK_SINE_LEN           256U
#define AFSK_DAC_MID            2048U
#define AFSK_DAC_AMP            2000U
#define AFSK_ADC_SQUELCH_THR    150U
#define AFSK_CAL_ALT_TONE_MS    500U
#define AFSK_CAL_ALT_CYCLES     10U

/* ================================================================
 * AX.25 timing
 * ================================================================ */
#define AX25_TX_DELAY_MS     300U
#define AX25_TX_TAIL_MS      30U
#define AX25_HEADER_FLAGS    4U
#define AX25_FOOTER_FLAGS    8U
#define AX25_MAX_INFO_LEN    256U
#define AFSK_TX_BUF_BYTES    512U

/* ================================================================
 * Digipeater configuration
 *
 * APRS_DIGI_WIDE1_ENABLE:  1 = digipeat WIDE1-1 (simple, set H-bit only)
 * APRS_DIGI_WIDE2_ENABLE:  1 = digipeat WIDE2-n (traced, insert own call)
 * APRS_DIGI_WIDE2_MAX_N:   Maximum n value in WIDEn-N to honour (≤ 7)
 * APRS_DIGI_DEDUPE_SECS:   Duplicate filter window in seconds
 * ================================================================ */
#define APRS_DIGI_WIDE1_ENABLE   1U
#define APRS_DIGI_WIDE2_ENABLE   1U
#define APRS_DIGI_WIDE2_MAX_N    2U
#define APRS_DIGI_DEDUPE_SECS    30U

#endif /* APRS_CONFIG_H */
