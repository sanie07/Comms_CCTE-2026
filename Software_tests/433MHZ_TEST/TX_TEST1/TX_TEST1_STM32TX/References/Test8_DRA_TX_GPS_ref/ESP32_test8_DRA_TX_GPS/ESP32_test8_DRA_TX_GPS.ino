/**
 * ESP32_test8_DRA_TX_GPS
 *
 * SPI master monitor for the STM32 GPS APRS tracker.
 * Polls a 1-byte status code from the STM32 slave (SPI1).
 *
 * Status codes must match STM32 Core/Inc/app.h
 */

#include <SPI.h>

#define SPI_MISO 4
#define SPI_MOSI 5
#define SPI_SCK  6
#define SPI_CS   7

#define SPI_STATUS_INIT          0
#define SPI_STATUS_HANDSHAKE_OK  1
#define SPI_STATUS_HANDSHAKE_ERR 2
#define SPI_STATUS_TX_ACTIVE     3
#define SPI_STATUS_TX_DONE       4
#define SPI_STATUS_GPS_FIX       5
#define SPI_STATUS_GPS_WAIT      6
#define SPI_STATUS_LOOPBACK_OK   7
#define SPI_STATUS_LOOPBACK_ERR  8

#define POLL_INTERVAL_MS    200
#define HEARTBEAT_MS        5000

static const SPISettings kSpiSettings(1000000, MSBFIRST, SPI_MODE0);

static uint32_t txCount = 0;

static bool isStartupReady(uint8_t status)
{
  return status == SPI_STATUS_HANDSHAKE_OK ||
         status == SPI_STATUS_HANDSHAKE_ERR ||
         status == SPI_STATUS_GPS_WAIT ||
         status == SPI_STATUS_GPS_FIX ||
         status == SPI_STATUS_LOOPBACK_OK ||
         status == SPI_STATUS_LOOPBACK_ERR ||
         status == SPI_STATUS_TX_ACTIVE ||
         status == SPI_STATUS_TX_DONE;
}

static uint8_t readSpiStatus(void)
{
  SPI.beginTransaction(kSpiSettings);

  digitalWrite(SPI_CS, LOW);
  delayMicroseconds(50);

  uint8_t status = SPI.transfer(0x00);

  digitalWrite(SPI_CS, HIGH);
  SPI.endTransaction();

  return status;
}

static const char* statusLabel(uint8_t status)
{
  switch (status) {
    case SPI_STATUS_INIT:          return "INIT";
    case SPI_STATUS_HANDSHAKE_OK:  return "HANDSHAKE OK";
    case SPI_STATUS_HANDSHAKE_ERR: return "HANDSHAKE ERR";
    case SPI_STATUS_TX_ACTIVE:     return "TX ACTIVE";
    case SPI_STATUS_TX_DONE:       return "TX DONE";
    case SPI_STATUS_GPS_FIX:       return "GPS FIX";
    case SPI_STATUS_GPS_WAIT:      return "GPS WAIT";
    case SPI_STATUS_LOOPBACK_OK:   return "LOOPBACK OK";
    case SPI_STATUS_LOOPBACK_ERR:  return "LOOPBACK ERR";
    default:                       return "UNKNOWN";
  }
}

static void printStatus(uint8_t status)
{
  Serial.print("[");
  Serial.print(statusLabel(status));
  Serial.print(" (");
  Serial.print(status);
  Serial.print(")] ");

  switch (status) {
    case SPI_STATUS_INIT:
      Serial.println("STM32 booting...");
      break;
    case SPI_STATUS_HANDSHAKE_OK:
      Serial.println("DRA818 ready on 145.000 MHz");
      break;
    case SPI_STATUS_HANDSHAKE_ERR:
      Serial.println("DRA818 AT handshake failed — check UART/power");
      break;
    case SPI_STATUS_TX_ACTIVE:
      Serial.println("APRS packet on air (AFSK 1200 baud / AX.25)");
      break;
    case SPI_STATUS_TX_DONE:
      Serial.print("APRS beacon sent (total: ");
      Serial.print(txCount);
      Serial.println(") — next in ~30 s");
      break;
    case SPI_STATUS_GPS_FIX:
      Serial.println("GPS fix valid — beacon will TX when channel is clear");
      break;
    case SPI_STATUS_GPS_WAIT:
      Serial.println("Waiting for GPS fix...");
      break;
    case SPI_STATUS_LOOPBACK_OK:
      Serial.println("DAC-to-ADC AX.25 loopback decoded with valid CRC");
      break;
    case SPI_STATUS_LOOPBACK_ERR:
      Serial.println("Loopback TX completed without a matching decoded frame");
      break;
    default:
      if (status != 0x00 && status != 0xFF) {
        Serial.print("Unexpected byte 0x");
        if (status < 0x10) Serial.print("0");
        Serial.println(status, HEX);
      }
      break;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);

  Serial.println();
  Serial.println("=================================");
  Serial.println(" ESP32 APRS Tracker Monitor");
  Serial.println(" STM32 + DRA818 + GPS");
  Serial.println("=================================");
  Serial.println("Waiting for STM32 status...");

  uint8_t status = 0x00;
  uint32_t waitStart = millis();

  while (!isStartupReady(status)) {
    status = readSpiStatus();

    if (!isStartupReady(status)) {
      if (millis() - waitStart > 30000) {
        Serial.println("Timeout waiting for STM32 status (30 s)");
        break;
      }
      delay(50);
    }
  }

  printStatus(status);
  Serial.println("Polling STM32 status every 200 ms...");
  Serial.println();
}

void loop()
{
  static uint8_t lastStatus = 0xFF;
  static uint32_t lastHeartbeatMs = 0;

  uint8_t status = readSpiStatus();
  uint32_t now = millis();

  if (status == SPI_STATUS_TX_DONE && lastStatus == SPI_STATUS_TX_ACTIVE) {
    txCount++;
  }

  if (status != lastStatus) {
    printStatus(status);
    lastStatus = status;
  } else if (status == SPI_STATUS_GPS_WAIT || status == SPI_STATUS_GPS_FIX) {
    if (now - lastHeartbeatMs >= HEARTBEAT_MS) {
      printStatus(status);
      lastHeartbeatMs = now;
    }
  }

  delay(POLL_INTERVAL_MS);
}
