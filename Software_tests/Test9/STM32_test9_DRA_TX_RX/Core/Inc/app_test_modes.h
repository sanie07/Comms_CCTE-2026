/**
 * @file    app_test_modes.h
 * @brief   Compile-time bring-up mode selection for Test9.
 *
 * Available modes:
 *   APP_TEST_DRA818_ONLY     - DRA818V handshake + config only
 *   APP_TEST_TONE_1200       - Continuous 1200 Hz calibration tone
 *   APP_TEST_CAL_ALT         - Alternating 1200/2200 Hz tones
 *   APP_TEST_AX25_LOOPBACK   - Internal TX->RX loopback via DAC->ADC
 *   APP_TEST_AX25_OTA        - Fixed-info packet over the air
 *   APP_TEST_GPS             - GPS NMEA parse only, no RF
 *   APP_TEST_APRS_TRACKER    - Periodic GPS APRS beacon (pure tracker)
 *   COMPLETE_DIGIPEATER      - Full APRS digipeater: RX -> path modify -> TX
 *   APP_TEST_RX_MONITOR      - Pure RX: decode APRS, dump every frame via SPI.
 *                              No PTT, no retransmission. Use this first to
 *                              confirm the RX/decode chain works before enabling TX.
 */

#ifndef APP_TEST_MODES_H
#define APP_TEST_MODES_H

#define APP_TEST_DRA818_ONLY     1
#define APP_TEST_TONE_1200       2
#define APP_TEST_CAL_ALT         3
#define APP_TEST_AX25_LOOPBACK   4
#define APP_TEST_AX25_OTA        5
#define APP_TEST_GPS             6
#define APP_TEST_APRS_TRACKER    7
#define COMPLETE_DIGIPEATER      8
#define APP_TEST_RX_MONITOR      9   /* <-- use this to debug RX decode */

/*
 * Select the active mode:
 *   9 = APP_TEST_RX_MONITOR   -- pure RX listener (start here)
 *   8 = COMPLETE_DIGIPEATER   -- full APRS digipeater
 *   7 = APP_TEST_APRS_TRACKER -- periodic GPS beacon only
 */
#ifndef APP_TEST_MODE
#define APP_TEST_MODE COMPLETE_DIGIPEATER
#endif

#endif /* APP_TEST_MODES_H */
