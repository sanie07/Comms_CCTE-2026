/**
  ******************************************************************************
  * @file    spi_link.c
  * @brief   SPI1 slave link to the ESP32-C6 viewer (see spi_link.h).
  *
  *  Same approach as Test10_DRA_SPI (proven on this board): a full-duplex
  *  HAL_SPI_TransmitReceive_IT() is always armed, and re-armed with fresh
  *  data from HAL_SPI_TxRxCpltCallback(). CS (PA0) is a plain EXTI input
  *  (NSS soft), so its rising edge is used to detect a broken transaction
  *  and resynchronize: SPI1 is reset, which also flushes the FIFOs.
  ******************************************************************************
  */
#include "spi_link.h"

#include <stdbool.h>
#include <string.h>
#include "main.h"
#include "spi.h"

_Static_assert(sizeof(SpiLinkPacket_t) == SPI_LINK_PACKET_SIZE,
               "SpiLinkPacket_t must be SPI_LINK_PACKET_SIZE bytes");

/* Latest data published by the application (written in task context) */
static SpiLinkPacket_t s_latest;
/* Buffers owned by the SPI peripheral while a transfer is armed */
static SpiLinkPacket_t s_tx;
static uint8_t s_rx[SPI_LINK_PACKET_SIZE];

static volatile uint32_t s_transfers = 0;
static volatile uint32_t s_resyncs = 0;

static uint8_t Checksum(const uint8_t *data, uint16_t len)
{
  uint8_t cs = 0U;
  for (uint16_t i = 0U; i < len; i++)
  {
    cs ^= data[i];
  }
  return cs;
}

/* Called with the SPI idle (init, ISR after completion or after reset) */
static void Arm(void)
{
  memcpy(&s_tx, &s_latest, sizeof(s_tx));
  s_tx.magic = SPI_LINK_MAGIC;
  s_tx.uptime_ms = HAL_GetTick();
  s_tx.checksum = Checksum((const uint8_t *)&s_tx, SPI_LINK_PACKET_SIZE - 1U);
  (void)HAL_SPI_TransmitReceive_IT(&hspi1, (uint8_t *)&s_tx, s_rx, SPI_LINK_PACKET_SIZE);
}

/* Hard reset of SPI1: drops a half-done transfer and stale FIFO bytes */
static void Resync(void)
{
  s_resyncs++;
  __HAL_RCC_SPI1_FORCE_RESET();
  __HAL_RCC_SPI1_RELEASE_RESET();
  hspi1.State = HAL_SPI_STATE_RESET;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  Arm();
}

void SpiLink_Init(void)
{
  memset(&s_latest, 0, sizeof(s_latest));
  s_latest.status = SPI_LINK_STATUS_RX_IDLE;
  Arm();

  /* Same priority as SPI1_IRQn (spi.c) so they never preempt each other */
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

void SpiLink_Publish(const SpiLinkPacket_t *pkt)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  memcpy(&s_latest, pkt, sizeof(s_latest));
  __set_PRIMASK(primask);
}

uint32_t SpiLink_GetTransfers(void)
{
  return s_transfers;
}

uint32_t SpiLink_GetResyncs(void)
{
  return s_resyncs;
}

/* ---- HAL callbacks --------------------------------------------------------*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin != SPI1_CS_Pin)
  {
    return;
  }
  if (HAL_GPIO_ReadPin(SPI1_CS_GPIO_Port, SPI1_CS_Pin) != GPIO_PIN_SET)
  {
    return; /* CS falling: transfer already armed */
  }

  /* CS rising = end of transaction. A complete one was re-armed in
   * TxRxCplt and nothing has been clocked yet (RxXferCount == size). */
  bool armedAndUntouched = (hspi1.State == HAL_SPI_STATE_BUSY_TX_RX) &&
                           (hspi1.RxXferCount == SPI_LINK_PACKET_SIZE);
  if (!armedAndUntouched)
  {
    Resync();
  }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    s_transfers++;
    Arm();
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    Resync();
  }
}
