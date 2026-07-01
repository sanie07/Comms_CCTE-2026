/**
 * @file    app_test_modes.h
 * @brief   Compile-time bring-up mode selection for Test8.
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

/*
 * Start conservatively: handshake/configure only. Advance this value one
 * phase at a time after each bench gate passes.
 */
#ifndef APP_TEST_MODE
#define APP_TEST_MODE APP_TEST_APRS_TRACKER
#endif

#endif /* APP_TEST_MODES_H */
