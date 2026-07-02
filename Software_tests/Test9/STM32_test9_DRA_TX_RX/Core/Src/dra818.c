/**
 * @file    dra818.c
 * @brief   DRA818V driver — power-on, AT handshake, APRS radio config.
 */

#include "dra818.h"
#include "aprs_config.h"
#include "main.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

#define DRA_CMD_TIMEOUT_MS   2000U
#define DRA_RESP_TIMEOUT_MS  500U
#define DRA_INTERBYTE_MS     50U
#define DRA_RX_BUF_SIZE      80U
#define DRA_BOOT_DELAY_MS    1000U
#define DRA_CMD_DELAY_MS     50U

extern UART_HandleTypeDef huart1;

static int s_initStatus = DRA818_ERR;

uint8_t g_draRxBuf[DRA_RX_BUF_SIZE];
uint32_t g_draRxLen = 0;
int g_draLastResult = DRA818_ERR;

static uint32_t draReceiveResponse(uint8_t *buf, uint32_t maxLen)
{
    uint32_t count = 0;
    uint32_t startTick = HAL_GetTick();

    while ((HAL_GetTick() - startTick) < DRA_RESP_TIMEOUT_MS)
    {
        uint8_t byte;
        if (HAL_UART_Receive(&huart1, &byte, 1, DRA_INTERBYTE_MS) == HAL_OK)
        {
            if (count < maxLen - 1)
            {
                buf[count++] = byte;
            }
            if (byte == '\n')
            {
                break;
            }
            startTick = HAL_GetTick();
        }
        else if (count > 0)
        {
            break;
        }
    }
    buf[count] = '\0';
    return count;
}

static int draSendCmd(const char *cmd, const char *expected)
{
    memset(g_draRxBuf, 0, sizeof(g_draRxBuf));
    g_draRxLen = 0;

    __HAL_UART_FLUSH_DRREGISTER(&huart1);

    HAL_UART_Transmit(&huart1,
                      (const uint8_t *)cmd,
                      (uint16_t)strlen(cmd),
                      DRA_CMD_TIMEOUT_MS);

    if (expected == NULL)
    {
        HAL_Delay(DRA_CMD_DELAY_MS);
        g_draLastResult = DRA818_OK;
        return DRA818_OK;
    }

    g_draRxLen = draReceiveResponse(g_draRxBuf, sizeof(g_draRxBuf));

    g_draLastResult = (strstr((char *)g_draRxBuf, expected) != NULL) ? DRA818_OK : DRA818_ERR;
    return g_draLastResult;
}

int DRA818_Configure(void)
{
    char cmd[96];

    snprintf(cmd, sizeof(cmd),
             "AT+DMOSETGROUP=%d,%s,%s,%s,%d,%s\r\n",
             DRA818_BANDWIDTH,
             DRA818_TX_FREQ,
             DRA818_RX_FREQ,
             DRA818_CTCSS_TX,
             DRA818_SQUELCH,
             DRA818_CTCSS_RX);

    if (draSendCmd(cmd, "+DMOSETGROUP:0") != DRA818_OK)
    {
        return DRA818_ERR;
    }

    snprintf(cmd, sizeof(cmd), "AT+DMOSETVOLUME=%d\r\n", DRA818_VOLUME);
    draSendCmd(cmd, "+DMOSETVOLUME:0");

  /* Raw audio path for Bell 202 AFSK */
    draSendCmd("AT+SETFILTER=0,0,0\r\n", NULL);

    return DRA818_OK;
}

int DRA818_Init(void)
{
    HAL_GPIO_WritePin(STM32_TO_DRA_ENA_GPIO_Port,
                      STM32_TO_DRA_ENA_Pin,
                      GPIO_PIN_SET);

    DRA818_SetPTT(false);
    HAL_Delay(DRA_BOOT_DELAY_MS);

    HAL_UART_DeInit(&huart1);
    huart1.Init.BaudRate = DRA818_UART_BAUD;
    huart1.Init.Parity = UART_PARITY_NONE;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        s_initStatus = DRA818_ERR;
        return DRA818_ERR;
    }

    HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_DisableFifoMode(&huart1);

    if (draSendCmd("AT+DMOCONNECT\r\n", "+DMOCONNECT:0") != DRA818_OK)
    {
        HAL_Delay(500U);
        if (draSendCmd("AT+DMOCONNECT\r\n", "+DMOCONNECT:0") != DRA818_OK)
        {
            HAL_Delay(500U);
            if (draSendCmd("AT+DMOCONNECT\r\n", "+DMOCONNECT:0") != DRA818_OK)
            {
                s_initStatus = DRA818_ERR;
                return DRA818_ERR;
            }
        }
    }

    if (DRA818_Configure() != DRA818_OK)
    {
        s_initStatus = DRA818_ERR;
        return DRA818_ERR;
    }

    s_initStatus = DRA818_OK;
    return DRA818_OK;
}

void DRA818_SetPTT(bool active)
{
    /* External transistor inverts: HIGH = transmit, LOW = receive */
    HAL_GPIO_WritePin(STM32_TO_DRA_PTT_GPIO_Port,
                      STM32_TO_DRA_PTT_Pin,
                      active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool DRA818_IsChannelBusy(void)
{
    return (HAL_GPIO_ReadPin(DRA_SQ_TO_STM32_GPIO_Port,
                             DRA_SQ_TO_STM32_Pin) == GPIO_PIN_RESET);
}

void DRA818_PowerOff(void)
{
    DRA818_SetPTT(false);
    HAL_GPIO_WritePin(STM32_TO_DRA_ENA_GPIO_Port,
                      STM32_TO_DRA_ENA_Pin,
                      GPIO_PIN_RESET);
}

int DRA818_GetStatus(void)
{
    return s_initStatus;
}
