/**
 * @file    dra818.c
 * @brief   DRA818V VHF radio module driver — handshake-only implementation.
 *
 * This file is a reduced version of the full DRA818 driver used in
 * STM32_test5_DRA.  It performs:
 *   1. ENA assert  (power-on the DRA818)
 *   2. USART1 reconfiguration to 9600 baud
 *   3. AT+DMOCONNECT handshake (two attempts as per DRA818V datasheet)
 *
 * TX audio, AX.25 encoding, PTT control, and AT+DMOSETGROUP are
 * intentionally omitted.  DRA818_GetStatus() lets the application
 * layer query whether the handshake succeeded without re-running it.
 *
 * AT command reference (DRA818V datasheet, 9600 8N1):
 *   AT+DMOCONNECT\r\n  →  +DMOCONNECT:0\r\n
 *
 * Squelch pin polarity (DRA818V datasheet, pin 7):
 *   SQ LOW  = carrier detected (channel busy)
 *   SQ HIGH = no carrier       (channel clear)
 */

#include "dra818.h"
#include "dra818_config.h"
#include "main.h"
#include "usart.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Private constants                                                   */
/* ------------------------------------------------------------------ */

#define DRA_CMD_TIMEOUT_MS   2000U   /* Transmit timeout                  */
#define DRA_RESP_TIMEOUT_MS  500U   /* Receive timeout                   */
#define DRA_RX_BUF_SIZE      80U     /* Response receive buffer (bytes)   */
#define DRA_BOOT_DELAY_MS    1000U   /* Wait after ENA assert             */
#define DRA_CMD_DELAY_MS     50U    /* Wait between TX and RX            */

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

/* huart1 is declared in CubeMX-generated usart.c */
extern UART_HandleTypeDef huart1;

/** Cached result of the last DRA818_Init() call. */
static int s_initStatus = DRA818_ERR;

/* ------------------------------------------------------------------ */
/* Private helpers                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief  Transmit cmd, wait DRA_CMD_DELAY_MS, then receive up to
 *         DRA_RX_BUF_SIZE bytes and check that expected is a substring.
 */
static int draSendCmd(const char *cmd, const char *expected)
{
    uint8_t rxBuf[DRA_RX_BUF_SIZE];
    memset(rxBuf, 0, sizeof(rxBuf));

    /* Flush any pending RX data */
    __HAL_UART_FLUSH_DRREGISTER(&huart1);

    HAL_UART_Transmit(&huart1,
                      (const uint8_t *)cmd,
                      (uint16_t)strlen(cmd),
                      DRA_CMD_TIMEOUT_MS);

    HAL_Delay(DRA_CMD_DELAY_MS);

    HAL_UART_Receive(&huart1,
                     rxBuf,
                     DRA_RX_BUF_SIZE - 1U,
                     DRA_RESP_TIMEOUT_MS);

    if (expected == NULL)
        return DRA818_OK;               /* Caller does not care about ACK */

    return (strstr((char *)rxBuf, expected) != NULL) ? DRA818_OK : DRA818_ERR;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int DRA818_Init(void)
{
    /* 1. Assert ENA to power on the DRA818V */
    HAL_GPIO_WritePin(STM32_TO_DRA_ENA_GPIO_Port,
                      STM32_TO_DRA_ENA_Pin,
                      GPIO_PIN_SET);

    /* 2. Wait for DRA818 boot */
    HAL_Delay(DRA_BOOT_DELAY_MS);
   
    /* 3. Re-configure USART1 to 9600 baud (DRA818 fixed rate).
     *    CubeMX initialises it at 115 200 baud; we override here. */
    HAL_UART_DeInit(&huart1);
    huart1.Init.BaudRate = DRA818_UART_BAUD;   /* 9600 */
    huart1.Init.Parity = UART_PARITY_NONE;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        s_initStatus = DRA818_ERR;
        return DRA818_ERR;
    }

    /* 4. Handshake — try twice (first byte may be garbled after boot) */
    if (draSendCmd("AT+DMOCONNECT\r\n", "+DMOCONNECT:0") != DRA818_OK)
    {
        HAL_Delay(500U);
        if (draSendCmd("AT+DMOCONNECT\r\n", "+DMOCONNECT:0") != DRA818_OK)
        {
        	 HAL_Delay(500U);
        	 if (draSendCmd("AT+DMOCONNECT\r\n", "+DMOCONNECT:0") != DRA818_OK)
        	 {
            s_initStatus = DRA818_ERR;
            return DRA818_ERR;   /* Module not responding */
        	 }
        }
    }

    s_initStatus = DRA818_OK;
    return DRA818_OK;
}

bool DRA818_IsChannelBusy(void)
{
    /* SQ LOW (GPIO_PIN_RESET) = carrier detected = channel busy */
    return (HAL_GPIO_ReadPin(DRA_SQ_TO_STM32_GPIO_Port,
                             DRA_SQ_TO_STM32_Pin) == GPIO_PIN_RESET);
}

void DRA818_PowerOff(void)
{
    HAL_GPIO_WritePin(STM32_TO_DRA_ENA_GPIO_Port,
                      STM32_TO_DRA_ENA_Pin,
                      GPIO_PIN_RESET);
}

int DRA818_GetStatus(void)
{
    return s_initStatus;
}
