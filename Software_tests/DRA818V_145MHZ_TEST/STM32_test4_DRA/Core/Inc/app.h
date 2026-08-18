/**
 * @file    app.h
 * @brief   Application top-level for STM32_test4_DRA — DRA818 handshake only.
 *
 * This project verifies that the DRA818V module powers on and responds
 * to the AT+DMOCONNECT handshake.  No AFSK modulation, no AX.25 encoding,
 * and no packet transmission are performed.
 *
 * Call order from main.c:
 *   1. App_Init() — once, after all MX_*_Init() calls
 *   2. App_Run()  — repeatedly inside while(1)
 */

#ifndef APP_H
#define APP_H

/**
 * @brief  Power on the DRA818V and run the AT+DMOCONNECT handshake.
 *         Must be called after all MX_*_Init() peripheral initialisations.
 *         The result (OK / ERR) is stored internally and returned by
 *         DRA818_GetStatus().
 */
void App_Init(void);

/**
 * @brief  Periodic application handler — call this in the main() while(1) loop.
 *         In this handshake-only project the loop body is intentionally minimal:
 *         it monitors the squelch pin and can be expanded for future use.
 */
void App_Run(void);

#endif /* APP_H */
