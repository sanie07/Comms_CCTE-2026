#include <RadioLib.h>
#include <SPI.h>

// ─── Pin definitions ───────────────────────────────────────────────────────
const int sckPin  = 9;
const int misoPin = 10;
const int mosiPin = 11;
const int ssPin   = 12;
const int dio0Pin = 14;
const int rstPin  = 13;
const int ledPin  = 21;

SX1278 radio = new Module(ssPin, dio0Pin, rstPin, RADIOLIB_NC);

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  delay(3000);

  SPI.begin(sckPin, misoPin, mosiPin, ssPin);

  Serial.println(F("========================================="));
  Serial.println(F(" SX1278 FSK Diagnostic Receiver v3"));
  Serial.println(F("========================================="));

  // ── Step 1: Initialize FSK ──────────────────────────────────────────────
  // Parameters to match STM32WL:
  //   RF_FREQUENCY = 433018893 Hz = 433.018893 MHz
  //   FSK_DATARATE = 1200 bps
  //   FSK_FDEV     = 5000 Hz = 5.0 kHz
  //   Preamble     = 8 bytes (0xAA)
  //   RX BW        = 50 kHz (wider to account for crystal drift)
  Serial.print(F("[1] beginFSK... "));
  int state = radio.beginFSK(433.0189, 1.2, 5.0, 50.0, 10, 8, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("FAIL: ")); Serial.println(state); while(1) delay(10);
  }
  Serial.println(F("OK"));

  // ── Step 2: CRC off ─────────────────────────────────────────────────────
  Serial.print(F("[2] setCRC(false)... "));
  state = radio.setCRC(false);
  Serial.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  // ── Step 3: Sync Word = 0xC1 0x94 0xC1 ─────────────────────────────────
  // This is hardcoded in STM32WL's radio.c RadioSetTxConfig() for MODEM_FSK:
  //   SUBGRF_SetSyncWord( (uint8_t[]){0xC1, 0x94, 0xC1, 0, 0, 0, 0, 0} );
  //   SyncWordLength = 3 << 3; (3 bytes)
  Serial.print(F("[3] setSyncWord(C1 94 C1)... "));
  uint8_t syncWord[] = {0xC1, 0x94, 0xC1};
  state = radio.setSyncWord(syncWord, 3);
  Serial.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  // ── Step 4: Data Whitening ON ───────────────────────────────────────────
  // Both SX1278 and SX1262 use the same IBM whitening: 
  //   LFSR x^9 + x^5 + 1, seed 0x01FF
  // STM32WL driver forces: DcFree = RADIO_DC_FREEWHITENING + seed 0x01FF
  // RadioLib SX1278: setEncoding(WHITENING) uses same algorithm + same seed
  Serial.print(F("[4] setEncoding(WHITENING)... "));
  state = radio.setEncoding(RADIOLIB_ENCODING_WHITENING);
  Serial.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  // ── Step 5: Packet length mode ──────────────────────────────────────────
  // STM32 uses fixLen=false → variable length. SX1278 must match.
  Serial.print(F("[5] variablePacketLengthMode... "));
  state = radio.variablePacketLengthMode();
  Serial.println(state == RADIOLIB_ERR_NONE ? "OK" : String(state).c_str());

  Serial.println(F("\n========================================="));
  Serial.println(F(" Listening... (STM32 TX every 5s)"));
  Serial.println(F("=========================================\n"));
}

void loop() {
  // ── RSSI scan (quick peek at RF energy before blocking receive) ─────────
  // We read the instantaneous RSSI by briefly entering RX mode
  // Put radio in RX mode to read RSSI
  static unsigned long lastRssiPrint = 0;
  if (millis() - lastRssiPrint > 2000) {
    lastRssiPrint = millis();
    Serial.print(F("."));  // heartbeat - shows the loop is alive
  }

  // ── Try to receive ──────────────────────────────────────────────────────
  byte buf[256];
  int state = radio.receive(buf, 0);  // 0 = variable length from packet header

  if (state == RADIOLIB_ERR_NONE) {
    int len = radio.getPacketLength();
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════╗"));
    Serial.println(F("║        PACKET RECEIVED!              ║"));
    Serial.println(F("╚══════════════════════════════════════╝"));
    Serial.print(F("  Length: ")); Serial.println(len);
    Serial.print(F("  RSSI:  ")); Serial.print(radio.getRSSI()); Serial.println(F(" dBm"));
    
    // Raw hex dump (already de-whitened by SX1278 hardware)
    Serial.println(F("  ── Hex (de-whitened by HW) ──"));
    hexDump(buf, len);

    // ASCII
    Serial.print(F("  ASCII: "));
    for (int i = 0; i < len; i++) {
      Serial.print(isPrintable(buf[i]) ? (char)buf[i] : '.');
    }
    Serial.println();
    Serial.println(F("────────────────────────────────────────"));

    // Blink LED
    digitalWrite(ledPin, HIGH); delay(100); digitalWrite(ledPin, LOW);

  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.print(F("C"));  // CRC error indicator

  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    // Normal timeout, do nothing (the heartbeat dot shows we're alive)

  } else {
    // Some other error
    Serial.print(F("\n[ERR] code: "));
    Serial.println(state);
  }
}

// ─── Helper: hex dump ──────────────────────────────────────────────────────
void hexDump(const byte* data, int len) {
  for (int i = 0; i < len; i++) {
    if (i % 16 == 0) {
      if (i > 0) Serial.println();
      Serial.print(F("  "));
      if (i < 0x10) Serial.print('0');
      Serial.print(i, HEX);
      Serial.print(F(": "));
    }
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}
