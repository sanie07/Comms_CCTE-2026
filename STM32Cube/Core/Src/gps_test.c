#include "gps_test.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_UART        huart2
#define DEBUG_UART      huart1
#define GPS_LINE_MAX    128

static char gps_line[GPS_LINE_MAX];
static uint16_t gps_line_len;

static void Debug_Print(const char *msg)
{
  HAL_UART_Transmit(&DEBUG_UART, (uint8_t *)msg, (uint16_t)strlen(msg), 1000);
}

static void Debug_PrintLine(const char *line)
{
  Debug_Print(line);
  Debug_Print("\r\n");
}

static int ParseGGA(const char *line)
{
  char buf[GPS_LINE_MAX];
  char *token;
  char *ctx = NULL;
  uint8_t field = 0;
  int fix_quality = 0;
  int num_sats = 0;

  if (strncmp(line, "$GNGGA", 6) != 0 && strncmp(line, "$GPGGA", 6) != 0)
  {
    return 0;
  }

  strncpy(buf, line, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';

  token = strtok_r(buf, ",", &ctx);
  while (token != NULL)
  {
    if (field == 6)
    {
      fix_quality = atoi(token);
    }
    else if (field == 7)
    {
      num_sats = atoi(token);
    }
    else if (field == 8)
    {
      char summary[96];
      snprintf(summary, sizeof(summary),
               "GPS fix quality=%d sats=%d hdop=%s",
               fix_quality, num_sats, token);
      Debug_PrintLine(summary);
      return fix_quality > 0;
    }

    token = strtok_r(NULL, ",", &ctx);
    field++;
  }

  return 0;
}

static int ParseRMC(const char *line)
{
  char buf[GPS_LINE_MAX];
  char *token;
  char *ctx = NULL;
  uint8_t field = 0;

  if (strncmp(line, "$GNRMC", 6) != 0 && strncmp(line, "$GPRMC", 6) != 0)
  {
    return 0;
  }

  strncpy(buf, line, sizeof(buf) - 1U);
  buf[sizeof(buf) - 1U] = '\0';

  token = strtok_r(buf, ",", &ctx);
  while (token != NULL)
  {
    if (field == 2)
    {
      if (token[0] == 'A')
      {
        Debug_PrintLine("GPS RMC: valid fix");
        return 1;
      }
      if (token[0] == 'V')
      {
        Debug_PrintLine("GPS RMC: searching for satellites...");
        return 0;
      }
    }

    token = strtok_r(NULL, ",", &ctx);
    field++;
  }

  return 0;
}

static void HandleGpsLine(const char *line)
{
  if (line[0] != '$')
  {
    return;
  }

  Debug_PrintLine(line);
  (void)ParseGGA(line);
  (void)ParseRMC(line);
}

void GPS_Test_Init(void)
{
  gps_line_len = 0;

  Debug_PrintLine("");
  Debug_PrintLine("=== HGLRC M100 / u-blox M10 GPS test ===");
  Debug_PrintLine("GPS UART: USART2 PA2/PA3 @ 115200 (J9 connector)");
  Debug_PrintLine("Debug UART: USART1 PB6 TX @ 115200");
  Debug_PrintLine("Waiting for NMEA from GPS module...");
  Debug_PrintLine("Tip: first fix outdoors can take 30-60 s cold start.");
}

void GPS_Test_Process(void)
{
  uint8_t byte;

  if (HAL_UART_Receive(&GPS_UART, &byte, 1, 10) != HAL_OK)
  {
    return;
  }

  if (byte == '\n' || byte == '\r')
  {
    if (gps_line_len > 0U)
    {
      gps_line[gps_line_len] = '\0';
      HandleGpsLine(gps_line);
      gps_line_len = 0;
    }
    return;
  }

  if (gps_line_len < (GPS_LINE_MAX - 1U))
  {
    gps_line[gps_line_len++] = (char)byte;
  }
  else
  {
    gps_line_len = 0;
  }
}
