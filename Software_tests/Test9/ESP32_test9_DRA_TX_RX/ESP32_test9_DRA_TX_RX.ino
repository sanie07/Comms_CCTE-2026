/**
 * ESP32_test9_DRA_TX_GPS
 *
 * SPI master monitor for the STM32 APRS TNC / Digipeater (Test9).
 *
 * Protocol (matches STM32 Core/Inc/app.h):
 *   Normal operation : STM32 sends 1 status byte per master clock cycle.
 *   Frame dump       : STM32 sends 0x0A (RX_FRAME header) followed by the
 *                      raw AX.25 frame bytes (dest+src+path+ctrl+pid+info,
 *                      no FCS), then resumes normal status bytes.
 *
 * This sketch:
 *   1. Polls STM32 every POLL_INTERVAL_MS.
 *   2. On receiving 0x0A, immediately clocks out all frame bytes (reading
 *      until the STM32 returns to a known status byte).
 *   3. Decodes and prints the AX.25 frame (callsigns + info field).
 *
 * SPI pin mapping (match STM32 SPI1 slave):
 *   GPIO 4  = MISO (STM32 PB4)
 *   GPIO 5  = MOSI (STM32 PB5)
 *   GPIO 6  = SCK  (STM32 PA1)
 *   GPIO 7  = CS   (software, active LOW)
 */

#include <SPI.h>

/* ---- Pin definitions ---- */
#define SPI_MISO 4
#define SPI_MOSI 5
#define SPI_SCK  6
#define SPI_CS   7

/* ---- Status codes (must match STM32 Core/Inc/app.h) ---- */
#define SPI_STATUS_INIT          0x00
#define SPI_STATUS_HANDSHAKE_OK  0x01
#define SPI_STATUS_HANDSHAKE_ERR 0x02
#define SPI_STATUS_TX_ACTIVE     0x03
#define SPI_STATUS_TX_DONE       0x04
#define SPI_STATUS_GPS_FIX       0x05
#define SPI_STATUS_GPS_WAIT      0x06
#define SPI_STATUS_LOOPBACK_OK   0x07
#define SPI_STATUS_LOOPBACK_ERR  0x08
/* 0x09 reserved */
#define SPI_STATUS_RX_FRAME      0x0A   /* header: raw AX.25 frame bytes follow */
#define SPI_STATUS_DIGI_TX       0x0B   /* digipeater re-transmission in progress */
#define SPI_STATUS_RX_IDLE       0x0C   /* listening, no frame pending */
#define SPI_STATUS_RX_SEEN       0x0D   /* frame just decoded (RX monitor mode) */

/* ---- Timing ---- */
#define POLL_INTERVAL_MS    200
#define HEARTBEAT_MS        5000

/* ---- Frame dump buffer ---- */
#define FRAME_MAX_BYTES     300

static const SPISettings kSpiSettings(500000, MSBFIRST, SPI_MODE0);

static uint32_t txCount   = 0;
static uint32_t digiCount = 0;
static uint32_t rxCount   = 0;   /* frames decoded in RX monitor mode */

/* ================================================================
 * SPI helpers
 * ================================================================ */

/** Transfer one byte and return the slave's response. */
static uint8_t spiTransfer(uint8_t out)
{
  SPI.beginTransaction(kSpiSettings);
  digitalWrite(SPI_CS, LOW);
  delayMicroseconds(50);
  uint8_t in = SPI.transfer(out);
  digitalWrite(SPI_CS, HIGH);
  SPI.endTransaction();
  return in;
}

/**
 * Read one status byte from the STM32.
 * Returns the raw byte (may be a status code or a frame-dump byte).
 */
static uint8_t readOneByte()
{
  return spiTransfer(0x00);
}

/* ================================================================
 * AX.25 frame decoder helpers
 * ================================================================ */

/** Return true if b is a known single-byte status code (not a frame byte). */
static bool isStatusByte(uint8_t b)
{
  return (b <= SPI_STATUS_RX_SEEN);
}

/**
 * Decode a 6-byte AX.25 callsign field (each byte >> 1 gives ASCII).
 * Writes a null-terminated string into out (must be >= 7 bytes).
 */
static void decodeCall(const uint8_t *addr, char *out)
{
  uint8_t len = 0;
  for (uint8_t i = 0; i < 6; i++) {
    char c = (char)(addr[i] >> 1);
    if (c != ' ') out[len++] = c;
  }
  out[len] = '\0';
}

/**
 * Decode the SSID from the 7th byte of an address field.
 * Returns 0-15.
 */
static uint8_t decodeSsid(uint8_t ssidByte)
{
  return (ssidByte >> 1) & 0x0F;
}

/** True if extension bit (bit 0) indicates this is the last address. */
static bool isLastAddr(uint8_t ssidByte)
{
  return (ssidByte & 0x01) != 0;
}

/** True if the H-bit (bit 7) is set — address has been repeated. */
static bool hasHbit(uint8_t ssidByte)
{
  return (ssidByte & 0x80) != 0;
}

/**
 * Print a decoded callsign + SSID, e.g. "W1ABC-9*".
 * Appends '*' if the H-bit is set (digipeated).
 */
static void printCallsign(const uint8_t *addr)
{
  char call[7];
  decodeCall(addr, call);
  uint8_t ssid  = decodeSsid(addr[6]);
  bool    hBit  = hasHbit(addr[6]);

  Serial.print(call);
  if (ssid != 0) {
    Serial.print('-');
    Serial.print(ssid);
  }
  if (hBit) Serial.print('*');
}

/**
 * Parse and print a raw AX.25 frame (no FCS).
 * Frame layout: [Dest 7B][Src 7B][Path 7B each][Ctrl 1B][PID 1B][Info nB]
 */
static void printAX25Frame(const uint8_t *frame, uint16_t len)
{
  if (len < 16) {                     /* Minimum: dest+src+ctrl+pid */
    Serial.println(F("  [Frame too short to decode]"));
    return;
  }

  /* ---- Destination ---- */
  Serial.print(F("  Dest: "));
  printCallsign(&frame[0]);

  /* ---- Source ---- */
  Serial.print(F("  From: "));
  printCallsign(&frame[7]);

  /* ---- Path elements ---- */
  uint16_t addrIdx = 14;              /* First byte of first path element */
  bool srcIsLast = isLastAddr(frame[13]);

  if (!srcIsLast) {
    Serial.print(F("  Via: "));
    bool first = true;
    while ((addrIdx + 6) < len) {
      uint8_t ssidByte = frame[addrIdx + 6];
      if (!first) Serial.print(',');
      printCallsign(&frame[addrIdx]);
      first = false;
      bool last = isLastAddr(ssidByte);
      addrIdx += 7;
      if (last) break;
    }
  }

  /* ---- ctrl + pid + info ---- */
  /* addrIdx now points past the last address field */
  uint16_t ctrlIdx = srcIsLast ? 14 : addrIdx;
  if (ctrlIdx + 2 > len) {
    Serial.println();
    return;
  }
  uint8_t ctrl = frame[ctrlIdx];
  uint8_t pid  = frame[ctrlIdx + 1];

  uint16_t infoStart = ctrlIdx + 2;
  uint16_t infoLen   = (infoStart < len) ? (len - infoStart) : 0;

  Serial.print(F("  Ctrl:0x"));
  if (ctrl < 0x10) Serial.print('0');
  Serial.print(ctrl, HEX);
  Serial.print(F(" PID:0x"));
  if (pid < 0x10) Serial.print('0');
  Serial.print(pid, HEX);

  if (infoLen > 0) {
    Serial.print(F("  Info: "));
    for (uint16_t i = 0; i < infoLen; i++) {
      char c = (char)frame[infoStart + i];
      Serial.print((c >= 0x20 && c <= 0x7E) ? c : '.');
    }
  }
  Serial.println();
}

/* ================================================================
 * Frame dump receiver
 * Clocks bytes from STM32 until it returns a normal status byte.
 * Returns that status byte so the main loop can process it.
 * ================================================================ */
static uint8_t receiveFrameDump()
{
  static uint8_t frameBuf[FRAME_MAX_BYTES];
  uint16_t frameLen = 0;

  Serial.println(F("\n--- AX.25 Frame Received ---"));

  /* Clock bytes until we see a known status byte or buffer full */
  while (frameLen < FRAME_MAX_BYTES) {
    delay(2);                         /* ~1 ms gap: let STM32 prepare next byte */
    uint8_t b = readOneByte();

    if (isStatusByte(b)) {
      /* STM32 has returned to normal status stream */
      printAX25Frame(frameBuf, frameLen);
      Serial.print(F("  ["));
      Serial.print(frameLen);
      Serial.println(F(" bytes total]"));
      Serial.println(F("----------------------------\n"));
      return b;                       /* Return the status byte for normal handling */
    }

    frameBuf[frameLen++] = b;
  }

  /* Buffer full — print what we have */
  printAX25Frame(frameBuf, frameLen);
  Serial.println(F("  [Frame truncated — buffer full]"));
  Serial.println(F("----------------------------\n"));

  /* Read one more byte to resync */
  return readOneByte();
}

/* ================================================================
 * Status label / print
 * ================================================================ */
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
    case SPI_STATUS_RX_FRAME:      return "RX FRAME";
    case SPI_STATUS_DIGI_TX:       return "DIGI TX";
    case SPI_STATUS_RX_IDLE:       return "RX IDLE";
    case SPI_STATUS_RX_SEEN:       return "FRAME DECODED";
    default:                       return "UNKNOWN";
  }
}

static void printStatusChange(uint8_t status)
{
  Serial.print(F("["));
  Serial.print(statusLabel(status));
  Serial.print(F(" (0x"));
  if (status < 0x10) Serial.print('0');
  Serial.print(status, HEX);
  Serial.print(F(")] "));

  switch (status) {
    case SPI_STATUS_INIT:
      Serial.println(F("STM32 booting..."));
      break;
    case SPI_STATUS_HANDSHAKE_OK:
      Serial.println(F("DRA818 ready on 145.825 MHz"));
      break;
    case SPI_STATUS_HANDSHAKE_ERR:
      Serial.println(F("DRA818 AT handshake failed — check UART/power"));
      break;
    case SPI_STATUS_TX_ACTIVE:
      Serial.println(F("APRS packet on air (AFSK 1200 baud / AX.25)"));
      break;
    case SPI_STATUS_TX_DONE:
      Serial.print(F("TX done (beacons: "));
      Serial.print(txCount);
      Serial.print(F("  digipeated: "));
      Serial.print(digiCount);
      Serial.println(F(")"));
      break;
    case SPI_STATUS_GPS_FIX:
      Serial.println(F("GPS fix valid"));
      break;
    case SPI_STATUS_GPS_WAIT:
      Serial.println(F("Waiting for GPS fix..."));
      break;
    case SPI_STATUS_LOOPBACK_OK:
      Serial.println(F("DAC→ADC AX.25 loopback decoded OK"));
      break;
    case SPI_STATUS_LOOPBACK_ERR:
      Serial.println(F("Loopback TX finished — no matching decode"));
      break;
    case SPI_STATUS_DIGI_TX:
      Serial.print(F("Digipeating frame (total digipeated: "));
      Serial.print(digiCount);
      Serial.println(F(")"));
      break;
    case SPI_STATUS_RX_IDLE:
      Serial.println(F("Listening — no frame pending"));
      break;
    case SPI_STATUS_RX_SEEN:
      Serial.print(F("Frame decoded! (total: "));
      Serial.print(rxCount);
      Serial.println(F(") — full dump follows via SPI"));
      break;
    default:
      Serial.print(F("Unexpected byte 0x"));
      if (status < 0x10) Serial.print('0');
      Serial.println(status, HEX);
      break;
  }
}

static bool isStartupReady(uint8_t status)
{
  return status >= SPI_STATUS_HANDSHAKE_OK && status <= SPI_STATUS_RX_IDLE
         && status != SPI_STATUS_RX_FRAME;
}

/* ================================================================
 * Arduino setup / loop
 * ================================================================ */
void setup()
{
  Serial.begin(115200);
  delay(500);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);

  Serial.println();
  Serial.println(F("=========================================="));
  Serial.println(F(" ESP32 APRS TNC / Digipeater Monitor"));
  Serial.println(F(" STM32WLE5 + DRA818V  145.825 MHz"));
  Serial.println(F("=========================================="));
  Serial.println(F("Waiting for STM32..."));

  uint8_t status = 0x00;
  uint32_t waitStart = millis();

  while (!isStartupReady(status)) {
    status = readOneByte();
    if (!isStartupReady(status)) {
      if (millis() - waitStart > 30000) {
        Serial.println(F("Timeout (30 s) — check SPI wiring"));
        break;
      }
      delay(50);
    }
  }

  printStatusChange(status);
  Serial.println(F("Polling every 200 ms...\n"));
}

void loop()
{
  static uint8_t  lastStatus      = 0xFF;
  static uint32_t lastHeartbeatMs = 0;

  uint8_t status = readOneByte();
  uint32_t now   = millis();

  /* ---- Frame dump: received 0x0A header ---- */
  if (status == SPI_STATUS_RX_FRAME) {
    delay(5);                         /* Small pause to let STM32 load first byte */
    status = receiveFrameDump();      /* Collect frame, returns next status byte  */
    lastStatus = 0xFF;                /* Force status print after frame dump      */
  }

  /* ---- Counters ---- */
  if (status == SPI_STATUS_TX_DONE && lastStatus == SPI_STATUS_TX_ACTIVE) {
    txCount++;
  }
  if (status == SPI_STATUS_DIGI_TX && lastStatus != SPI_STATUS_DIGI_TX) {
    digiCount++;
  }
  if (status == SPI_STATUS_RX_SEEN && lastStatus != SPI_STATUS_RX_SEEN) {
    rxCount++;
  }

  /* ---- Print on change or heartbeat ---- */
  if (status != lastStatus) {
    /* Skip printing RX_IDLE transitions every poll — too verbose */
    if (!(status == SPI_STATUS_RX_IDLE && lastStatus == SPI_STATUS_TX_DONE)) {
      printStatusChange(status);
    }
    lastStatus = status;
  } else if (status == SPI_STATUS_GPS_WAIT || status == SPI_STATUS_GPS_FIX
             || status == SPI_STATUS_RX_IDLE) {
    if (now - lastHeartbeatMs >= HEARTBEAT_MS) {
      printStatusChange(status);
      lastHeartbeatMs = now;
    }
  }

  delay(POLL_INTERVAL_MS);
}
