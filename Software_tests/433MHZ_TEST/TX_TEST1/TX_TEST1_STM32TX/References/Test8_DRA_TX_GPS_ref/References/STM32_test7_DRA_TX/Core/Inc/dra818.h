/**
 * @file    dra818.h
 * @brief   DRA818V VHF radio module driver (UART AT-command interface).
 *
 * Hardware connections (from CubeMX project):
 *   USART1 TX  PB6  → DRA818 RX  (AT commands out)
 *   USART1 RX  PB7  ← DRA818 TX  (responses in)
 *   PA6 (ENA)  OUT  → DRA818 PWR_DOWN (HIGH = module on)
 *   PA7 (PTT)  OUT  → DRA818 PTT      (LOW  = transmit)
 *   PA8 (SQ)   IN   ← DRA818 SQ       (LOW  = carrier detected)
 *
 * The DRA818V UART is fixed at 9600 baud.  DRA818_Init() re-initialises
 * USART1 from the CubeMX default (115 200 baud) to 9600 baud.
 *
 * This variant only performs the AT+DMOCONNECT handshake.
 * TX/RX audio (AFSK / AX.25) is not used in STM32_test4_DRA.
 */

#ifndef DRA818_H
#define DRA818_H

#include <stdint.h>
#include <stdbool.h>

/** Return codes */
#define DRA818_OK     ( 0)
#define DRA818_ERR    (-1)

/**
 * @brief  Power on the DRA818V, set UART to 9600 baud, and perform the
 *         AT+DMOCONNECT handshake.
 *         On success the module is on-air in receive mode.
 * @return DRA818_OK on success, DRA818_ERR if no response received.
 */
int DRA818_Init(void);

/**
 * @brief  Sample the squelch output pin (PA8).
 * @return true  when the DRA818 detects a carrier (SQ pin LOW = busy).
 *         false when the channel is quiet  (SQ pin HIGH).
 */
bool DRA818_IsChannelBusy(void);

/**
 * @brief  Deassert ENA to power down the module (saves current).
 */
void DRA818_PowerOff(void);

/**
 * @brief  Returns the result of the last DRA818_Init() call.
 * @return DRA818_OK if handshake succeeded, DRA818_ERR otherwise.
 */
int DRA818_GetStatus(void);

#endif /* DRA818_H */
