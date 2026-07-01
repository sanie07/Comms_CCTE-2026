/**
 * @file    dra818_config.h
 * @brief   DRA818V compile-time configuration for STM32_test4_DRA.
 *
 * Only the parameters needed for the handshake-only driver are included
 * here.  Frequency, CTCSS, and AFSK/AX.25 settings from STM32_test5_DRA
 * are intentionally omitted — they are not used in this project.
 */

#ifndef DRA818_CONFIG_H
#define DRA818_CONFIG_H

#include <stdint.h>

/* ================================================================
 * DRA818V UART configuration
 * The DRA818 UART is fixed at 9600 baud — do not change.
 * ================================================================ */
#define DRA818_UART_BAUD    9600U   /**< Fixed by DRA818 hardware */

#endif /* DRA818_CONFIG_H */
