#ifndef __GFSK_TX_H
#define __GFSK_TX_H

#ifdef __cplusplus
extern "C" {
#endif

//#include "FreeRTOS.h"
//#include "task.h"
//#include "semphr.h"
#include "stm32wlxx_hal.h"
#include "radio.h"

/* Exported functions ------------------------------------------------------- */
void GFSK_Init(void);
//BaseType_t GFSK_SendPacket_RTOS(uint8_t *payload, uint16_t size, TickType_t xTicksToWait);
void GFSK_TxDone_Callback(void);
void GFSK_TxTimeout_Callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __GFSK_TX_H */
