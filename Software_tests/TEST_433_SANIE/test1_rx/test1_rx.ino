#include <RadioLib.h>
#include <SPI.h>
#include <ctype.h>

/* Path A: SX1278 FSK packet mode, matching STM32 RadioSetTxGenericConfig.
 * Whitening off, BT 0.5, CRC off, variable length, sync C1 94 C1.
 * Path B (direwolf/soundmodem) needs discriminator audio, not this sketch. */

/* ESP32-S3 USB Serial/JTAG is HWCDC (the COM/ttyACM port PuTTY opens), not UART0.
 * Arduino IDE: USB Mode = Hardware CDC and JTAG, USB CDC On Boot = Enabled.
 * PuTTY: Serial, 115200 8N1, Flow control None, Implicit CR in every LF. */
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !(defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT)
  #define LOG USBSerial
#else
  #define LOG Serial
#endif

const int sckPin  = 9;
const int misoPin = 10;
const int mosiPin = 11;
const int ssPin   = 12;
const int dio0Pin = 14;
const int rstPin  = 13;
const int ledPin  = 21;

SX1278 radio = new Module(ssPin, dio0Pin, rstPin, RADIOLIB_NC);

volatile bool gotPacket = false;

#if defined(ESP32)
void IRAM_ATTR onDio0() { gotPacket = true; }
#else
void onDio0() { gotPacket = true; }
#endif

static void hexDump(const byte* data, int len);
static bool parseAx25Ui(const byte* data, int len);

void setup() {
  LOG.begin(115200);
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  LOG.setTxTimeoutMs(0);
#endif
  pinMode(ledPin, OUTPUT);

  /* USB-JTAG re-enumerates after reset; wait so PuTTY can attach and catch the banner. */
  unsigned long t0 = millis();
  while (!LOG && (millis() - t0) < 5000) {
    delay(10);
  }
  delay(500);

  SPI.begin(sckPin, misoPin, mosiPin, ssPin);

  LOG.println(F("========================================="));
  LOG.println(F(" SX1278 FSK AX.25 Receiver (Path A)"));
  LOG.println(F(" USB Serial/JTAG  115200 8N1"));
  LOG.println(F("========================================="));
  LOG.flush();

  /* freq 433.0189 MHz, 1.2 kbps, 5.0 kHz fdev, 20 kHz RX BW, preamble 8 bytes */
  LOG.print(F("[1] beginFSK... "));
  int state = radio.beginFSK(433.0189, 1.2, 5.0, 20.0, 10, 8, false);
  if (state != RADIOLIB_ERR_NONE) {
    LOG.print(F("FAIL: ")); LOG.println(state); while (1) delay(10);
  }
  LOG.println(F("OK"));

  LOG.print(F("[2] setCRC(false)... "));
  state = radio.setCRC(false);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[3] setSyncWord(C1 94 C1)... "));
  uint8_t syncWord[] = {0xC1, 0x94, 0xC1};
  state = radio.setSyncWord(syncWord, 3);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[4] setEncoding(NRZ)... "));
  state = radio.setEncoding(RADIOLIB_ENCODING_NRZ);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[5] variablePacketLengthMode(63)... "));
  state = radio.variablePacketLengthMode(63);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[6] setDataShaping(BT=0.5)... "));
  state = radio.setDataShaping(RADIOLIB_SHAPING_0_5);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[7] setRxBandwidth(20 kHz)... "));
  state = radio.setRxBandwidth(20.0);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[8] setAFCBandwidth(20 kHz)... "));
  state = radio.setAFCBandwidth(20.0);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.print(F("[9] setAFC(true)... "));
  state = radio.setAFC(true);
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  radio.setDio0Action(onDio0, RISING);

  LOG.print(F("[10] startReceive... "));
  state = radio.startReceive();
  LOG.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  LOG.println();
  LOG.println(F("Listening (STM32 TX every 5s). Heartbeat '.' every 2s."));
  LOG.println();
  LOG.flush();
}

void loop() {
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    LOG.print(F("."));
    LOG.flush();
  }

  if (!gotPacket) {
    return;
  }
  gotPacket = false;

  byte buf[64];
  int state = radio.readData(buf, 0);
  int len = radio.getPacketLength();

  if (state == RADIOLIB_ERR_NONE) {
    LOG.println();
    LOG.println(F("======== PACKET RECEIVED ========"));
    LOG.print(F("  Length: ")); LOG.print(len);
    LOG.print(F("  (expect ~57 bytes)"));
    LOG.println();
    LOG.print(F("  RSSI:   ")); LOG.print(radio.getRSSI()); LOG.println(F(" dBm"));
    LOG.print(F("  FreqErr: ")); LOG.print(radio.getFrequencyError());
    LOG.println(F(" Hz  (trim STM32 XTAL_DEFAULT_CAP_VALUE if large)"));

    LOG.println(F("  Hex:"));
    hexDump(buf, len);

    LOG.print(F("  ASCII: "));
    for (int i = 0; i < len; i++) {
      LOG.print(isPrintable(buf[i]) ? (char)buf[i] : '.');
    }
    LOG.println();

    parseAx25Ui(buf, len);
    LOG.println(F("================================"));
    LOG.flush();

    digitalWrite(ledPin, HIGH);
    delay(80);
    digitalWrite(ledPin, LOW);
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    LOG.print(F("C"));
  } else {
    LOG.println();
    LOG.print(F("[ERR] readData: "));
    LOG.println(state);
    LOG.flush();
  }

  radio.startReceive();
}

static uint16_t ax25Crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 1U) {
        crc = (crc >> 1U) ^ 0x8408U;
      } else {
        crc = (crc >> 1U);
      }
    }
  }
  return (uint16_t)(~crc);
}

static void printCallsign(const byte* addr) {
  for (int i = 0; i < 6; i++) {
    char c = (char)(addr[i] >> 1);
    if (c != ' ') {
      LOG.print(c);
    }
  }
  LOG.print(F("-"));
  LOG.print((addr[6] >> 1) & 0x0F);
}

static bool parseAx25Ui(const byte* data, int len) {
  int start = 0;
  int end = len;
  while (start < end && data[start] == 0x7E) {
    start++;
  }
  while (end > start && data[end - 1] == 0x7E) {
    end--;
  }
  int frameLen = end - start;
  if (frameLen < 16) {
    LOG.println(F("  AX.25: frame too short"));
    return false;
  }

  uint16_t gotFcs = (uint16_t)data[start + frameLen - 2] |
                    ((uint16_t)data[start + frameLen - 1] << 8);
  uint16_t expFcs = ax25Crc16(&data[start], (size_t)(frameLen - 2));
  bool crcOk = (gotFcs == expFcs);

  LOG.print(F("  Dest: "));
  printCallsign(&data[start]);
  LOG.print(F("  Src: "));
  printCallsign(&data[start + 7]);
  LOG.println();

  uint8_t ctrl = data[start + 14];
  uint8_t pid  = data[start + 15];
  LOG.print(F("  Ctrl=0x"));
  if (ctrl < 0x10) LOG.print('0');
  LOG.print(ctrl, HEX);
  LOG.print(F(" PID=0x"));
  if (pid < 0x10) LOG.print('0');
  LOG.print(pid, HEX);
  if ((ctrl & 0xEF) != 0x03) {
    LOG.print(F(" (not UI)"));
  }
  if (pid != 0xF0) {
    LOG.print(F(" (PID != 0xF0)"));
  }
  LOG.println();

  LOG.print(F("  FCS "));
  LOG.println(crcOk ? F("OK") : F("FAIL"));

  int infoLen = frameLen - 18;
  if (infoLen > 0) {
    LOG.print(F("  Info: "));
    for (int i = 0; i < infoLen; i++) {
      char c = (char)data[start + 16 + i];
      LOG.print(isPrintable(c) ? c : '.');
    }
    LOG.println();
  }
  return crcOk;
}

static void hexDump(const byte* data, int len) {
  for (int i = 0; i < len; i++) {
    if (i % 16 == 0) {
      if (i > 0) LOG.println();
      LOG.print(F("  "));
      if (i < 0x10) LOG.print('0');
      LOG.print(i, HEX);
      LOG.print(F(": "));
    }
    if (data[i] < 0x10) LOG.print('0');
    LOG.print(data[i], HEX);
    LOG.print(' ');
  }
  LOG.println();
}
