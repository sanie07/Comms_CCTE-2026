#ifndef APRS_TX_H
#define APRS_TX_H

#ifdef __cplusplus
extern "C" {
#endif

/*#include "main.h"
//#include "cmsis_os2.h"
//#include "FreeRTOS.h"
//#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <stdint.h>
#include <stdbool.h>

/* Defines
#define APRS_MAX_PAYLOAD_LEN 256

/* Enumerations
typedef enum {
    APRS_STATE_IDLE = 0,
    APRS_STATE_WAITING_PTT,
    APRS_STATE_TX_PREAMBLE,
    APRS_STATE_TX_DATA,
    APRS_STATE_TX_POSTAMBLE,
    APRS_STATE_TX_TAIL
} APRS_State_t;

/* Structures
typedef struct {
    char callsign_src[10];
    char callsign_dst[10];
    char path[20];
    uint8_t payload[APRS_MAX_PAYLOAD_LEN];
    uint16_t payload_len;
} APRS_Packet_t;

/* Public API
BaseType_t APRS_Init(DAC_HandleTypeDef *hdac, TIM_HandleTypeDef *htim, UART_HandleTypeDef *huart, SemaphoreHandle_t rf_mutex);

BaseType_t APRS_SendPacket_RTOS(const char *callsign_src, const char *callsign_dst, const char *path, const uint8_t *payload, uint16_t len, TickType_t xTicksToWait);

APRS_State_t APRS_GetState(void);

void APRS_DMA_HalfTransfer_ISR(void);
void APRS_DMA_FullTransfer_ISR(void);*/

#ifdef __cplusplus
}
#endif

#endif /* APRS_TX_H */
