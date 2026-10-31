/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    subghz_phy_app.c
  * @author  MCD Application Team
  * @brief   Application of the SubGHz_Phy Middleware
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "platform.h"
#include "sys_app.h"
#include "subghz_phy_app.h"
#include "radio.h"

/* USER CODE BEGIN Includes */
#include "main.h"  /* Error_Handler */
#include "stm32_seq.h"
#include "stm32_timer.h"
#include "utilities_def.h"
#include "spi_link.h"
#include <string.h>
/* USER CODE END Includes */

/* External variables ---------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  RX_EVT_NONE = 0,
  RX_EVT_DONE,
  RX_EVT_TIMEOUT,
  RX_EVT_ERROR
} RxEvent_t;

typedef struct
{
  char dest[12];
  char src[12];
  const uint8_t *info;
  uint16_t infoLen;
  uint8_t ctrl;
  uint8_t pid;
  bool fcsOk;
} Ax25Ui_t;

/* Last packet summary, meant for STM32CubeIDE "Live Expressions" */
typedef struct
{
  uint32_t total;          /* packets received (good + bad) */
  uint32_t ok;             /* FCS OK */
  uint32_t bad;            /* FCS fail / too short */
  uint32_t rxErrors;       /* RX timeout / error events */
  uint16_t bytes;          /* bytes on air of the last packet */
  uint16_t msgBytes;       /* AX.25 info bytes of the last packet */
  int16_t  rssiDbm;
  int8_t   cfoKhz;
  uint8_t  fcsOk;
  char     from[12];
  char     message[48];
} RxMonitor_t;

static UTIL_TIMER_Object_t timerStatus;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* NOTE: All RF parameters are defined in subghz_phy_app.h */
#define STATUS_PERIOD_MS                            10000

/* AX.25 UI frame as sent by ESP32TX / TX_TEST1 (Path A):
 * dest(7) + src(7) + ctrl + pid + info + FCS(2), followed by 2x 0x7E. */
#define AX25_FLAG                                   0x7EU
#define AX25_ADDR_LEN                               7U
#define AX25_MIN_FRAME_LEN                          18U
#define AX25_CTRL_UI                                0x03U
#define AX25_PID_NO_L3                              0xF0U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* Radio events function pointer */
static RadioEvents_t RadioEvents;

/* USER CODE BEGIN PV */
static uint8_t RxBuffer[PAYLOAD_LEN];
static volatile uint16_t RxSize = 0;
static volatile int16_t RxRssi = 0;
static volatile int8_t RxCfoKhz = 0;
static volatile RxEvent_t RxEvent = RX_EVT_NONE;
static volatile uint8_t StatusPending = 0;

static uint32_t RxCountOk = 0;
static uint32_t RxCountFcsFail = 0;
static uint32_t RxCountErr = 0;

static uint8_t FskSyncWord[] = { 0xC1, 0x94, 0xC1 };

/* Global (not static) so the debugger finds it by name: add "RxMonitor"
 * in Live Expressions. Works over plain SWD, no UART or SWO needed. */
RxMonitor_t RxMonitor = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/*!
 * @brief Function to be executed on Radio Tx Done event
 */
static void OnTxDone(void);

/**
  * @brief Function to be executed on Radio Rx Done event
  * @param  payload ptr of buffer received
  * @param  size buffer size
  * @param  rssi
  * @param  LoraSnr_FskCfo
  */
static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo);

/**
  * @brief Function executed on Radio Tx Timeout event
  */
static void OnTxTimeout(void);

/**
  * @brief Function executed on Radio Rx Timeout event
  */
static void OnRxTimeout(void);

/**
  * @brief Function executed on Radio Rx Error event
  */
static void OnRxError(void);

/* USER CODE BEGIN PFP */
static void StartReceive(void);
static void ProcessRx(void);
static void OnStatusTimerEvent(void *context);
static void PrintStatus(void);
static void PrintPacket(const uint8_t *data, uint16_t len);
static bool DecodeAx25UI(const uint8_t *data, uint16_t len, Ax25Ui_t *out);
static uint16_t Ax25Crc16(const uint8_t *data, uint16_t len);
static void FormatCallsign(const uint8_t *addr, char *out);
static void PublishToSpi(void);
/* USER CODE END PFP */

/* Exported functions ---------------------------------------------------------*/
void SubghzApp_Init(void)
{
  /* USER CODE BEGIN SubghzApp_Init_1 */
  APP_PRINTF("\r\n=========================================\r\n");
  APP_PRINTF(" STM32WLE5 FSK AX.25 Receiver (Path A)\r\n");
  APP_PRINTF(" %u Hz  %u bps  fdev %u Hz  BW %u Hz\r\n",
             (unsigned)RF_FREQUENCY, (unsigned)FSK_DATARATE,
             (unsigned)FSK_FDEV, (unsigned)FSK_BANDWIDTH);
  APP_PRINTF(" sync C1 94 C1, BT=0.5, no whitening, no CRC\r\n");
  APP_PRINTF("=========================================\r\n");
  /* USER CODE END SubghzApp_Init_1 */

  /* Radio initialization */
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError = OnRxError;

  Radio.Init(&RadioEvents);

  /* USER CODE BEGIN SubghzApp_Init_2 */
  /* Pin PHY to match ESP32/SX1278 TX (and the TX_TEST1 STM32 TX):
   * BT=0.5, whitening off, CRC off, variable length, sync C1 94 C1. */
  RxConfigGeneric_t RxConfig = {0};

  Radio.SetChannel(RF_FREQUENCY);

  RxConfig.fsk.ModulationShaping = RADIO_FSK_MOD_SHAPING_G_BT_05;
  RxConfig.fsk.Bandwidth = FSK_BANDWIDTH;
  RxConfig.fsk.BitRate = FSK_DATARATE;
  RxConfig.fsk.PreambleLen = FSK_PREAMBLE_LENGTH;
  RxConfig.fsk.PreambleMinDetect = RADIO_FSK_PREAMBLE_DETECTOR_16_BITS; /* SX1278 side: 2 bytes */
  RxConfig.fsk.SyncWordLength = sizeof(FskSyncWord);
  RxConfig.fsk.SyncWord = FskSyncWord;
  RxConfig.fsk.whiteSeed = 0x0000U;
  RxConfig.fsk.LengthMode = RADIO_FSK_PACKET_VARIABLE_LENGTH;
  RxConfig.fsk.CrcLength = RADIO_FSK_CRC_OFF;
  RxConfig.fsk.Whitening = RADIO_FSK_DC_FREE_OFF;
  RxConfig.fsk.AddrComp = RADIO_FSK_ADDRESSCOMP_FILT_OFF;
  RxConfig.fsk.MaxPayloadLength = PAYLOAD_LEN;
  RxConfig.fsk.StopTimerOnPreambleDetect = 0;
  /* rxContinuous = 0: single RX, re-armed from ProcessRx() after each event */
  if (0UL != Radio.RadioSetRxGenericConfig(GENERIC_FSK, &RxConfig, 0, 0))
  {
    APP_PRINTF("RadioSetRxGenericConfig failed\r\n");
    Error_Handler();
  }

  /* Register RX processing task with the sequencer */
  UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), UTIL_SEQ_RFU, ProcessRx);

  /* Periodic status line (counters + instant RSSI), like the ESP32 RX test */
  UTIL_TIMER_Create(&timerStatus, 0xFFFFFFFFU, UTIL_TIMER_PERIODIC, OnStatusTimerEvent, NULL);
  UTIL_TIMER_SetPeriod(&timerStatus, STATUS_PERIOD_MS);
  UTIL_TIMER_Start(&timerStatus);

  /* SPI1 slave link: last message for the ESP32-C6 viewer */
  SpiLink_Init();

  StartReceive();
  APP_PRINTF("Listening (ESP32 TX every 5s). Status line every %us.\r\n",
             (unsigned)(STATUS_PERIOD_MS / 1000));
  APP_PRINTF("SPI1 slave ready for the ESP32-C6 viewer (%u-byte packets).\r\n\r\n",
             (unsigned)SPI_LINK_PACKET_SIZE);
  /* USER CODE END SubghzApp_Init_2 */
}

/* USER CODE BEGIN EF */
static void StartReceive(void)
{
  /* PA9 (RF_CRL_TO_STM32) is driven LOW = RX by RBI_ConfigRFSwitch() */
  Radio.Rx(RX_TIMEOUT_VALUE);
}

static void OnStatusTimerEvent(void *context)
{
  StatusPending = 1;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
}

static void ProcessRx(void)
{
  RxEvent_t evt = RxEvent;
  RxEvent = RX_EVT_NONE;

  switch (evt)
  {
    case RX_EVT_DONE:
      PrintPacket(RxBuffer, RxSize);
      break;
    case RX_EVT_TIMEOUT:
      RxCountErr++;
      APP_PRINTF("[RX] timeout / header error\r\n");
      break;
    case RX_EVT_ERROR:
      RxCountErr++;
      APP_PRINTF("[RX] error\r\n");
      break;
    default:
      break;
  }

  RxMonitor.rxErrors = RxCountErr;
  if ((evt == RX_EVT_TIMEOUT) || (evt == RX_EVT_ERROR))
  {
    PublishToSpi();
  }
  if (evt != RX_EVT_NONE)
  {
    StartReceive();
  }

  if (StatusPending != 0U)
  {
    StatusPending = 0;
    PrintStatus();
  }
}

static void PrintStatus(void)
{
  int16_t rssiNow = Radio.Rssi(MODEM_FSK);
  bool rxRunning = (Radio.GetStatus() == RF_RX_RUNNING);

  APP_PRINTF("[stat] ok=%u fcs_fail=%u err=%u  RSSI=%d dBm  rx=%u  spi=%u resync=%u\r\n",
             (unsigned)RxCountOk, (unsigned)RxCountFcsFail, (unsigned)RxCountErr,
             (int)rssiNow, rxRunning ? 1U : 0U,
             (unsigned)SpiLink_GetTransfers(), (unsigned)SpiLink_GetResyncs());

  /* Watchdog: radio left RX without raising an event -> re-arm */
  if ((!rxRunning) && (RxEvent == RX_EVT_NONE))
  {
    APP_PRINTF("[stat] radio not in RX, re-arming\r\n");
    StartReceive();
  }
}

static void PrintPacket(const uint8_t *data, uint16_t len)
{
  static const char hexDigits[] = "0123456789ABCDEF";
  char line[72];
  Ax25Ui_t ui;
  bool decoded = DecodeAx25UI(data, len, &ui);
  bool good = decoded && ui.fcsOk;

  if (good)
  {
    RxCountOk++;
  }
  else
  {
    RxCountFcsFail++;
  }

  APP_PRINTF("\r\n======== MENSAJE RECIBIDO #%u ========\r\n",
             (unsigned)(RxCountOk + RxCountFcsFail));
  if (good)
  {
    APP_PRINTF("  Hola! Estoy recibiendo datos de %s\r\n", ui.src);
  }
  else
  {
    APP_PRINTF("  Llegaron datos, pero la trama vino con errores\r\n");
  }

  if (decoded)
  {
    uint16_t pos = 0U;
    for (uint16_t i = 0U; (i < ui.infoLen) && (pos < (sizeof(line) - 1U)); i++)
    {
      char c = (char)ui.info[i];
      line[pos++] = ((c >= 0x20) && (c <= 0x7E)) ? c : '.';
    }
    line[pos] = '\0';
    APP_PRINTF("  Mensaje : %s\r\n", line);
    APP_PRINTF("  De      : %s  ->  Para: %s\r\n", ui.src, ui.dest);
    APP_PRINTF("  Bytes   : %u recibidos (%u de mensaje)\r\n",
               (unsigned)len, (unsigned)ui.infoLen);
  }
  else
  {
    line[0] = '\0';
    APP_PRINTF("  Bytes   : %u recibidos (trama AX.25 muy corta)\r\n", (unsigned)len);
  }

  /* Same data for Live Expressions */
  RxMonitor.total = RxCountOk + RxCountFcsFail;
  RxMonitor.ok = RxCountOk;
  RxMonitor.bad = RxCountFcsFail;
  RxMonitor.bytes = len;
  RxMonitor.msgBytes = decoded ? ui.infoLen : 0U;
  RxMonitor.rssiDbm = RxRssi;
  RxMonitor.cfoKhz = RxCfoKhz;
  RxMonitor.fcsOk = good ? 1U : 0U;
  strncpy(RxMonitor.from, decoded ? ui.src : "?", sizeof(RxMonitor.from) - 1U);
  strncpy(RxMonitor.message, line, sizeof(RxMonitor.message) - 1U);
  PublishToSpi();

  APP_PRINTF("  RSSI    : %d dBm   CFO: %d kHz\r\n", (int)RxRssi, (int)RxCfoKhz);
  if (decoded)
  {
    APP_PRINTF("  FCS     : %s%s\r\n", ui.fcsOk ? "OK" : "FALLO",
               (((ui.ctrl & 0xEFU) != AX25_CTRL_UI) || (ui.pid != AX25_PID_NO_L3)) ?
               "  (no es trama UI)" : "");
  }
  APP_PRINTF("  Total   : %u OK, %u con error\r\n",
             (unsigned)RxCountOk, (unsigned)RxCountFcsFail);
  APP_PRINTF("  Hex:\r\n");

  for (uint16_t i = 0U; i < len; i += 16U)
  {
    uint16_t pos = 0U;
    line[pos++] = ' ';
    line[pos++] = ' ';
    line[pos++] = hexDigits[(i >> 4U) & 0x0FU];
    line[pos++] = hexDigits[i & 0x0FU];
    line[pos++] = ':';
    line[pos++] = ' ';
    for (uint16_t j = i; (j < len) && (j < (uint16_t)(i + 16U)); j++)
    {
      line[pos++] = hexDigits[data[j] >> 4U];
      line[pos++] = hexDigits[data[j] & 0x0FU];
      line[pos++] = ' ';
    }
    line[pos] = '\0';
    APP_PRINTF("%s\r\n", line);
  }
  APP_PRINTF("======================================\r\n");
}

/* Returns false when the frame is too short to hold an AX.25 UI header */
static bool DecodeAx25UI(const uint8_t *data, uint16_t len, Ax25Ui_t *out)
{
  uint16_t start = 0U;
  uint16_t end = len;

  /* Strip optional leading / trailing HDLC flags */
  while ((start < end) && (data[start] == AX25_FLAG))
  {
    start++;
  }
  while ((end > start) && (data[end - 1U] == AX25_FLAG))
  {
    end--;
  }

  uint16_t frameLen = (uint16_t)(end - start);
  if (frameLen < AX25_MIN_FRAME_LEN)
  {
    return false;
  }

  const uint8_t *frame = &data[start];
  uint16_t gotFcs = (uint16_t)frame[frameLen - 2U] |
                    (uint16_t)((uint16_t)frame[frameLen - 1U] << 8U);
  uint16_t expFcs = Ax25Crc16(frame, (uint16_t)(frameLen - 2U));

  FormatCallsign(&frame[0], out->dest);
  FormatCallsign(&frame[AX25_ADDR_LEN], out->src);
  out->ctrl = frame[14];
  out->pid = frame[15];
  out->info = &frame[16];
  out->infoLen = (uint16_t)(frameLen - AX25_MIN_FRAME_LEN);
  out->fcsOk = (gotFcs == expFcs);
  return true;
}

/* Copy RxMonitor into the SPI1 slave packet read by the ESP32-C6 */
static void PublishToSpi(void)
{
  SpiLinkPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.status = (RxMonitor.total > 0U) ? SPI_LINK_STATUS_RX_FRAME : SPI_LINK_STATUS_RX_IDLE;
  pkt.seq = (uint16_t)RxMonitor.total;
  pkt.rx_ok = RxMonitor.ok;
  pkt.rx_bad = RxMonitor.bad;
  pkt.rx_errors = RxMonitor.rxErrors;
  pkt.rssi_dbm = RxMonitor.rssiDbm;
  pkt.cfo_khz = RxMonitor.cfoKhz;
  pkt.fcs_ok = RxMonitor.fcsOk;
  pkt.bytes = (uint8_t)RxMonitor.bytes;
  pkt.msg_len = (uint8_t)RxMonitor.msgBytes;
  strncpy(pkt.from, RxMonitor.from, sizeof(pkt.from) - 1U);
  strncpy(pkt.msg, RxMonitor.message, sizeof(pkt.msg) - 1U);
  SpiLink_Publish(&pkt);
}

/* CRC-16 CCITT (reflected 0x8408, init 0xFFFF, final invert), same as AX25_SubGHz.c */
static uint16_t Ax25Crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0U; i < len; i++)
  {
    crc ^= (uint16_t)data[i];
    for (uint8_t j = 0U; j < 8U; j++)
    {
      if (crc & 1U)
      {
        crc = (uint16_t)((crc >> 1U) ^ 0x8408U);
      }
      else
      {
        crc = (uint16_t)(crc >> 1U);
      }
    }
  }
  return (uint16_t)(~crc);
}

/* "CALL-SSID" from a 7-byte AX.25 address field (chars shifted left by 1) */
static void FormatCallsign(const uint8_t *addr, char *out)
{
  uint8_t pos = 0U;
  for (uint8_t i = 0U; i < 6U; i++)
  {
    char c = (char)(addr[i] >> 1U);
    if (c != ' ')
    {
      out[pos++] = ((c >= 0x20) && (c <= 0x7E)) ? c : '?';
    }
  }
  uint8_t ssid = (uint8_t)((addr[6] >> 1U) & 0x0FU);
  out[pos++] = '-';
  if (ssid >= 10U)
  {
    out[pos++] = '1';
    ssid = (uint8_t)(ssid - 10U);
  }
  out[pos++] = (char)('0' + ssid);
  out[pos] = '\0';
}
/* USER CODE END EF */

/* Private functions ---------------------------------------------------------*/
static void OnTxDone(void)
{
  /* USER CODE BEGIN OnTxDone */
  /* USER CODE END OnTxDone */
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t LoraSnr_FskCfo)
{
  /* USER CODE BEGIN OnRxDone */
  /* Runs in radio IRQ context: copy and defer to ProcessRx() */
  uint16_t n = (size > PAYLOAD_LEN) ? PAYLOAD_LEN : size;
  memcpy(RxBuffer, payload, n);
  RxSize = n;
  RxRssi = rssi;
  RxCfoKhz = LoraSnr_FskCfo;
  RxEvent = RX_EVT_DONE;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxDone */
}

static void OnTxTimeout(void)
{
  /* USER CODE BEGIN OnTxTimeout */
  /* USER CODE END OnTxTimeout */
}

static void OnRxTimeout(void)
{
  /* USER CODE BEGIN OnRxTimeout */
  RxEvent = RX_EVT_TIMEOUT;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxTimeout */
}

static void OnRxError(void)
{
  /* USER CODE BEGIN OnRxError */
  RxEvent = RX_EVT_ERROR;
  UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_SubGHz_Phy_App_Process), CFG_SEQ_Prio_0);
  /* USER CODE END OnRxError */
}

/* USER CODE BEGIN PrFD */

/* USER CODE END PrFD */
