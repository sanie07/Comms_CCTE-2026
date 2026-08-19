/**
 * ESP32_test10_DRA_TX_SPI
 *
 * SPI Master for Test10 dual-test firmware.
 * Communicates with STM32 slave (SPI1) using 32-byte SpiPacket_t.
 *
 * TEST_MODE 0: Sends hardcoded coordinates to STM32 for DRA818V APRS TX.
 * TEST_MODE 1: Reads raw coordinate telemetry from STM32 and displays it.
 *
 * Configuration is set in app.h — TEST_MODE must match STM32 app.h.
 */

#include <SPI.h>
#include "app.h"

/* ================================================================
 * SPI Pin Mapping
 * ================================================================ */
#define SPI_MISO 4
#define SPI_MOSI 5
#define SPI_SCK  6
#define SPI_CS   7

/* ================================================================
 * Timing
 * ================================================================ */
#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)
  #define TRANSFER_INTERVAL_MS  5000  /* Send coords every 5 seconds */
#else
  #define TRANSFER_INTERVAL_MS  1500   /* Poll raw data every 1.5 seconds */
#endif

#define HEARTBEAT_MS  5000

/* ================================================================
 * SPI Configuration
 * ================================================================ */
static const SPISettings kSpiSettings(1000000, MSBFIRST, SPI_MODE0);

static uint16_t s_txSeqNum = 0;
static uint32_t s_txCount  = 0;

/* ================================================================
 * Checksum
 * ================================================================ */
static uint8_t computeChecksum(const uint8_t *data, uint8_t len)
{
    uint8_t xor_sum = 0;
    for (uint8_t i = 0; i < len; i++)
        xor_sum ^= data[i];
    return xor_sum;
}

/* ================================================================
 * SPI Transfer — exchange 32-byte packets
 * ================================================================ */
static void spiTransfer(SpiPacket_t *txPkt, SpiPacket_t *rxPkt)
{
    SPI.beginTransaction(kSpiSettings);

    digitalWrite(SPI_CS, LOW);
    delayMicroseconds(100);  /* Give slave time to load TX buffer */

    const uint8_t *txData = (const uint8_t *)txPkt;
    uint8_t *rxData = (uint8_t *)rxPkt;
    
    for (int i = 0; i < SPI_PACKET_SIZE; i++) {
        rxData[i] = SPI.transfer(txData[i]);
        delayMicroseconds(20); /* Give STM32 interrupt handler time to fetch next byte */
    }

    digitalWrite(SPI_CS, HIGH);
    SPI.endTransaction();
}

/* ================================================================
 * Status label
 * ================================================================ */
static const char* statusLabel(uint8_t status)
{
    switch (status) {
        case SPI_STATUS_INIT:       return "INIT";
        case SPI_STATUS_DRA_READY:  return "DRA READY";
        case SPI_STATUS_DRA_ERR:    return "DRA ERROR";
        case SPI_STATUS_TX_ACTIVE:  return "TX ACTIVE";
        case SPI_STATUS_TX_DONE:    return "TX DONE";
        case SPI_STATUS_COORD_RCVD: return "COORD RCVD";
        case SPI_STATUS_READY:      return "READY";
        default:                    return "UNKNOWN";
    }
}

/* ================================================================
 * Mode 0: Send coordinates to STM32
 * ================================================================ */
#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)

static void buildCoordsPacket(SpiPacket_t *pkt)
{
    memset(pkt, 0, sizeof(SpiPacket_t));
    pkt->magic        = SPI_PACKET_MAGIC;
    pkt->cmd          = SPI_CMD_SEND_COORDS;
    pkt->seq_num      = s_txSeqNum++;
    pkt->lat_e6       = ESP32_HARDCODED_LAT_E6;
    pkt->lon_e6       = ESP32_HARDCODED_LON_E6;
    pkt->alt_cm       = ESP32_HARDCODED_ALT_CM;
    pkt->timestamp_ms = millis();
    strncpy(pkt->comment, "TEST10-SPI", sizeof(pkt->comment) - 1);
    pkt->comment[sizeof(pkt->comment) - 1] = '\0';
    pkt->status       = 0;
    pkt->checksum     = computeChecksum((const uint8_t *)pkt, SPI_PACKET_SIZE - 1);
}

static void printCoordsSendResult(const SpiPacket_t *rxPkt)
{
    Serial.println("─────────────────────────────────────────────");
    Serial.print("  TX Seq #");
    Serial.println(s_txSeqNum - 1);

    Serial.print("  Sent Lat: ");
    Serial.print(ESP32_HARDCODED_LAT_E6 / 1000000.0, 6);
    Serial.print("°  Lon: ");
    Serial.print(ESP32_HARDCODED_LON_E6 / 1000000.0, 6);
    Serial.print("°  Alt: ");
    Serial.print(ESP32_HARDCODED_ALT_CM / 100.0, 2);
    Serial.println(" m");

    Serial.print("  STM32 Status: [");
    Serial.print(statusLabel(rxPkt->status));
    Serial.print("] (0x");
    if (rxPkt->status < 0x10) Serial.print("0");
    Serial.print(rxPkt->status, HEX);
    Serial.println(")");

    if (rxPkt->magic == SPI_PACKET_MAGIC) {
        uint8_t cs = computeChecksum((const uint8_t *)rxPkt, SPI_PACKET_SIZE - 1);
        Serial.print("  Response Checksum: ");
        Serial.println(cs == rxPkt->checksum ? "OK" : "FAIL");
    } else {
        Serial.println("  Response: Invalid magic byte");
    }
    Serial.println("─────────────────────────────────────────────");
}

#endif /* TEST_MODE_ESP_TO_STM32_DRA */

/* ================================================================
 * Mode 1: Read raw coords from STM32
 * ================================================================ */
#if (TEST_MODE == TEST_MODE_STM32_TO_ESP_RAW)

static void buildPollPacket(SpiPacket_t *pkt)
{
    memset(pkt, 0, sizeof(SpiPacket_t));
    pkt->magic        = SPI_PACKET_MAGIC;
    pkt->cmd          = SPI_CMD_POLL_STATUS;
    pkt->seq_num      = s_txSeqNum++;
    pkt->timestamp_ms = millis();
    strncpy(pkt->comment, "POLL", sizeof(pkt->comment) - 1);
    pkt->comment[sizeof(pkt->comment) - 1] = '\0';
    pkt->status       = 0;
    pkt->checksum     = computeChecksum((const uint8_t *)pkt, SPI_PACKET_SIZE - 1);
}

static void printRawTelemetry(const SpiPacket_t *rxPkt)
{
    /* Validate */
    if (rxPkt->magic != SPI_PACKET_MAGIC) {
        Serial.println("[WARN] Invalid magic byte from STM32");
        return;
    }

    uint8_t cs = computeChecksum((const uint8_t *)rxPkt, SPI_PACKET_SIZE - 1);
    bool csOk = (cs == rxPkt->checksum);

    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("       STM32 RAW TELEMETRY (Test Mode 1)                  ");
    Serial.println("═══════════════════════════════════════════════════════════");

    Serial.print("  Seq #:        ");
    Serial.println(rxPkt->seq_num);

    Serial.print("  Timestamp:    ");
    Serial.print(rxPkt->timestamp_ms);
    Serial.println(" ms");

    Serial.println("───────────────────────────────────────────────────────────");
    Serial.println("  FIELD           RAW (int32)        CONVERTED            ");
    Serial.println("───────────────────────────────────────────────────────────");

    Serial.print("  Latitude:       ");
    char buf[32];
    snprintf(buf, sizeof(buf), "%-18ld", (long)rxPkt->lat_e6);
    Serial.print(buf);
    Serial.print(rxPkt->lat_e6 / 1000000.0, 6);
    Serial.println("°");

    Serial.print("  Longitude:      ");
    snprintf(buf, sizeof(buf), "%-18ld", (long)rxPkt->lon_e6);
    Serial.print(buf);
    Serial.print(rxPkt->lon_e6 / 1000000.0, 6);
    Serial.println("°");

    Serial.print("  Altitude:       ");
    snprintf(buf, sizeof(buf), "%-18ld", (long)rxPkt->alt_cm);
    Serial.print(buf);
    Serial.print(rxPkt->alt_cm / 100.0, 2);
    Serial.println(" m");

    Serial.println("───────────────────────────────────────────────────────────");

    Serial.print("  Comment:      \"");
    Serial.print(rxPkt->comment);
    Serial.println("\"");

    Serial.print("  Status:       [");
    Serial.print(statusLabel(rxPkt->status));
    Serial.print("] (0x");
    if (rxPkt->status < 0x10) Serial.print("0");
    Serial.print(rxPkt->status, HEX);
    Serial.println(")");

    Serial.print("  Checksum:     ");
    Serial.println(csOk ? "OK" : "FAIL");

    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println();
}

#endif /* TEST_MODE_STM32_TO_ESP_RAW */

/* ================================================================
 * setup()
 * ================================================================ */
void setup()
{
    Serial.begin(115200);
    delay(500);

    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);

    Serial.println();
    Serial.println("═══════════════════════════════════════════════");

#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)
    Serial.println("  ESP32 Test10 — Mode 0: Coords -> STM32 -> DRA818V");
    Serial.print("  Hardcoded Lat: ");
    Serial.print(ESP32_HARDCODED_LAT_E6 / 1000000.0, 6);
    Serial.print("°  Lon: ");
    Serial.print(ESP32_HARDCODED_LON_E6 / 1000000.0, 6);
    Serial.println("°");
    Serial.print("  Transfer interval: ");
    Serial.print(TRANSFER_INTERVAL_MS / 1000);
    Serial.println(" s");
#else
    Serial.println("  ESP32 Test10 — Mode 1: Read STM32 Raw Telemetry");
    Serial.print("  Poll interval: ");
    Serial.print(TRANSFER_INTERVAL_MS);
    Serial.println(" ms");
#endif

    Serial.println("═══════════════════════════════════════════════");
    Serial.println();

    /* Wait for STM32 to boot and initialize */
    Serial.println("Waiting for STM32 to initialize...");
    delay(3000);

    /* Initial status poll */
    SpiPacket_t txPkt, rxPkt;
    memset(&txPkt, 0, sizeof(txPkt));
    txPkt.magic    = SPI_PACKET_MAGIC;
    txPkt.cmd      = SPI_CMD_POLL_STATUS;
    txPkt.checksum = computeChecksum((const uint8_t *)&txPkt, SPI_PACKET_SIZE - 1);

    spiTransfer(&txPkt, &rxPkt);

    if (rxPkt.magic == SPI_PACKET_MAGIC) {
        Serial.print("STM32 initial status: [");
        Serial.print(statusLabel(rxPkt.status));
        Serial.println("]");
    } else {
        Serial.println("STM32 not responding (invalid magic byte)");
    }

    Serial.println("Starting main loop...");
    Serial.println();
}

/* ================================================================
 * loop()
 * ================================================================ */
void loop()
{
    static uint32_t lastTransferMs = 0;
    static uint8_t  lastStatus     = 0xFF;

    uint32_t now = millis();

    if ((now - lastTransferMs) >= TRANSFER_INTERVAL_MS)
    {
        lastTransferMs = now;

        SpiPacket_t txPkt, rxPkt;

#if (TEST_MODE == TEST_MODE_ESP_TO_STM32_DRA)
        /* Mode 0: Send hardcoded coordinates to STM32 */
        buildCoordsPacket(&txPkt);
        spiTransfer(&txPkt, &rxPkt);
        printCoordsSendResult(&rxPkt);

        if (rxPkt.status == SPI_STATUS_TX_DONE) {
            s_txCount++;
        }

        /* Track status changes */
        if (rxPkt.status != lastStatus) {
            lastStatus = rxPkt.status;
        }

#else
        /* Mode 1: Poll STM32 for raw telemetry */
        buildPollPacket(&txPkt);
        spiTransfer(&txPkt, &rxPkt);

        if (rxPkt.magic == SPI_PACKET_MAGIC && rxPkt.cmd == SPI_CMD_RAW_TELEMETRY) {
            printRawTelemetry(&rxPkt);
        } else {
            Serial.print("[");
            Serial.print(statusLabel(rxPkt.status));
            Serial.print("] Waiting for telemetry data (magic=0x");
            if (rxPkt.magic < 0x10) Serial.print("0");
            Serial.print(rxPkt.magic, HEX);
            Serial.print(", cmd=0x");
            if (rxPkt.cmd < 0x10) Serial.print("0");
            Serial.print(rxPkt.cmd, HEX);
            Serial.println(")");
        }
#endif
    }
}
