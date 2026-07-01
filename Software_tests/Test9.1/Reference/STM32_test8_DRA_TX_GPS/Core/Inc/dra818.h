/**
 * @file    dra818.h
 * @brief   DRA818V VHF radio module driver (UART AT-command interface).
 */

#ifndef DRA818_H
#define DRA818_H

#include <stdint.h>
#include <stdbool.h>

#define DRA818_OK     ( 0)
#define DRA818_ERR    (-1)

int DRA818_Init(void);
int DRA818_Configure(void);
void DRA818_SetPTT(bool active);
bool DRA818_IsChannelBusy(void);
void DRA818_PowerOff(void);
int DRA818_GetStatus(void);

#endif /* DRA818_H */
