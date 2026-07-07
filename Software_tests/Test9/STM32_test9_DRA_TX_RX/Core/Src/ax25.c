/**
 * @file    ax25.c
 * @brief   AX.25 UI frame encoder and decoder for APRS.
 *
 * All buffers are statically allocated — no malloc(), no RTOS.
 *
 * TX frame structure (bytes, before bit-stream packing):
 *   Dest  (7 bytes) + Src (7 bytes) + [Path1 (7 bytes)] + [Path2 (7 bytes)]
 *   + Control (1) + PID (1) + Information (≤256) + FCS low (1) + FCS high (1)
 *
 * Address byte encoding (AX.25 §3.12.1):
 *   Callsign bytes: ASCII left-shifted by 1 (bit 0 = 0)
 *   SSID byte:      bit7=C/H  bit6=res(1)  bit5=res(1)
 *                   bits4-1=SSID(0-15)  bit0=extension(1 if last addr)
 *
 * CRC-16 CCITT (reflected, poly=0x8408, init=0xFFFF, output complemented).
 * FCS is transmitted low-byte first.
 */

#include "ax25.h"
#include "afsk.h"
#include "aprs_config.h"
#include <string.h>
#include <stddef.h>

/* ================================================================
 * CRC-16 CCITT
 * ================================================================ */

static uint16_t crc16_update(uint16_t crc, uint8_t data)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        if ((crc ^ (uint16_t)data) & 1U)
            crc = (crc >> 1U) ^ 0x8408U;
        else
            crc >>= 1U;
        data >>= 1U;
    }
    return crc;
}

static uint16_t crc16_block(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0U; i < len; i++)
        crc = crc16_update(crc, data[i]);
    return (uint16_t)(~crc);
}

/* ================================================================
 * TX packed bit-stream buffer (shared with AFSK)
 * ================================================================ */

static uint8_t  txBitBuf[AFSK_TX_BUF_BYTES];  /* packed NRZ bit-stream */
static uint16_t txBitCount;                     /* total bits written    */
static bool     txOverflow;

static inline void appendBit(uint8_t bit)
{
    if ((txBitCount >> 3U) >= AFSK_TX_BUF_BYTES)
    {
        txOverflow = true;
        return;
    }

    uint16_t byteIdx = txBitCount >> 3U;
    uint8_t  bitIdx  = (uint8_t)(txBitCount & 7U);

    if (bit)
        txBitBuf[byteIdx] |=  (uint8_t)(1U << bitIdx);
    else
        txBitBuf[byteIdx] &= (uint8_t)~(1U << bitIdx);

    txBitCount++;
}

/** Append 8 bits of a byte, LSB first, WITHOUT bit stuffing (for flags). */
static void appendBytePlain(uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++)
        appendBit((byte >> i) & 1U);
}

/** Append 8 bits of a byte, LSB first, WITH HDLC bit stuffing.
 *  onesCount tracks consecutive 1s across byte boundaries. */
static void appendByteStuffed(uint8_t byte, uint8_t *onesCount)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint8_t bit = (byte >> i) & 1U;
        appendBit(bit);
        if (bit)
        {
            (*onesCount)++;
            if (*onesCount == 5U)
            {
                appendBit(0U);   /* insert stuffed zero */
                *onesCount = 0U;
            }
        }
        else
        {
            *onesCount = 0U;
        }
    }
}

/* ================================================================
 * Address field encoder
 * ================================================================ */

/**
 * Write a 7-byte AX.25 address field into out[0..6].
 *   call     : up to 6 ASCII characters (space-padded)
 *   ssid     : 0-15
 *   isLast   : true if this is the last address in the field
 *   isCommand: true for destination in a command frame (C bit)
 */
static void encodeAddr(const char *call, uint8_t ssid,
                        bool isLast, bool isCommand,
                        uint8_t *out)
{
    for (uint8_t i = 0U; i < 6U; i++)
    {
        char c = (i < (uint8_t)strlen(call)) ? call[i] : ' ';
        out[i] = (uint8_t)((uint8_t)c << 1U);
    }

    /* SSID byte */
    uint8_t sb = 0x60U;               /* reserved bits 6,5 must be 1 */
    if (isCommand)  sb |= 0x80U;      /* C bit (H bit for repeaters)  */
    sb |= (uint8_t)((ssid & 0x0FU) << 1U);
    if (isLast)     sb |= 0x01U;      /* extension: last address       */
    out[6] = sb;
}

/* ================================================================
 * AX25_BuildTxFrame
 * ================================================================ */

bool AX25_BuildTxFrame(const uint8_t *info, uint16_t infoLen)
{
    /* ---- Build raw frame bytes (address + ctrl + pid + info + FCS) ---- */

    /* Maximum: dest(7)+src(7)+path1(7)+path2(7)+ctrl(1)+pid(1)+info(256)+FCS(2) */
    uint8_t  frame[7U + 7U + 7U + 7U + 1U + 1U + AX25_MAX_INFO_LEN + 2U];
    uint16_t fLen = 0U;

    bool hasPath1 = (APRS_PATH1CALL[0] != '\0');
    bool hasPath2 = hasPath1 && (APRS_PATH2CALL[0] != '\0');

    /* Destination — C=1, extension=0 (source follows) */
    encodeAddr(APRS_DESTCALL, APRS_DESTSSID, false, true, &frame[fLen]);
    fLen += 7U;

    /* Source — C=0, extension=1 if no path, else 0 */
    encodeAddr(APRS_MYCALL, APRS_MYSSID, !hasPath1, false, &frame[fLen]);
    fLen += 7U;

    if (hasPath1)
    {
        encodeAddr(APRS_PATH1CALL, APRS_PATH1SSID, !hasPath2, false, &frame[fLen]);
        fLen += 7U;
    }
    if (hasPath2)
    {
        encodeAddr(APRS_PATH2CALL, APRS_PATH2SSID, true, false, &frame[fLen]);
        fLen += 7U;
    }

    /* Control and PID */
    frame[fLen++] = AX25_UI_CTRL;
    frame[fLen++] = AX25_PID_NO_L3;

    /* Information field */
    if (infoLen > AX25_MAX_INFO_LEN)
        infoLen = AX25_MAX_INFO_LEN;
    memcpy(&frame[fLen], info, infoLen);
    fLen += infoLen;

    /* FCS — CRC over address + ctrl + pid + info, low byte first */
    uint16_t crc = crc16_block(frame, fLen);
    frame[fLen++] = (uint8_t)(crc & 0xFFU);
    frame[fLen++] = (uint8_t)(crc >> 8U);

    /* ---- Pack into NRZ bit-stream ---- */
    memset(txBitBuf, 0, sizeof(txBitBuf));
    txBitCount = 0U;
    txOverflow = false;

    uint16_t txDelayFlags = (uint16_t)(((uint32_t)AX25_TX_DELAY_MS *
                                        (uint32_t)AFSK_BAUD_RATE + 7999UL) / 8000UL);
    uint16_t txTailFlags = (uint16_t)(((uint32_t)AX25_TX_TAIL_MS *
                                       (uint32_t)AFSK_BAUD_RATE + 7999UL) / 8000UL);

    for (uint16_t f = 0U; f < txDelayFlags; f++)
        appendBytePlain(AX25_FLAG);

    for (uint8_t f = 0U; f < AX25_HEADER_FLAGS; f++)
        appendBytePlain(AX25_FLAG);

    /* Frame content WITH bit stuffing */
    uint8_t onesCount = 0U;
    for (uint16_t i = 0U; i < fLen; i++)
        appendByteStuffed(frame[i], &onesCount);

    for (uint8_t f = 0U; f < AX25_FOOTER_FLAGS; f++)
        appendBytePlain(AX25_FLAG);

    for (uint16_t f = 0U; f < txTailFlags; f++)
        appendBytePlain(AX25_FLAG);

    if (txOverflow)
        return false;

    /* Hand the bit-stream to the AFSK modem */
    AFSK_TX_Load(txBitBuf, txBitCount);
    return true;
}

/* ================================================================
 * RX decoder state machine
 * ================================================================ */

static AX25_RxCallback_t  rxCallback  = NULL;
static AX25_DigiCallback_t digiCallback = NULL;

/* ================================================================
 * AX.25 RX debug counters
 * Add these to the STM32CubeIDE Expressions / Live Expressions view.
 *
 *   ax25_dbg_flagCount   -- HDLC flags (0x7E) detected
 *                           If 0: bit timing is wrong, no sync at all
 *                           If > 0: flag detection works
 *   ax25_dbg_frameStart  -- frame receptions opened (flag -> data)
 *   ax25_dbg_tooShort    -- frames closed before reaching 18 bytes
 *   ax25_dbg_crcFail     -- frames with wrong CRC (NRZI/noise issue)
 *   ax25_dbg_tooLong     -- frames that exceeded the RX buffer (very long)
 * ================================================================ */
volatile uint32_t ax25_dbg_flagCount  = 0U;
volatile uint32_t ax25_dbg_frameStart = 0U;
volatile uint32_t ax25_dbg_tooShort   = 0U;
volatile uint32_t ax25_dbg_crcFail    = 0U;
volatile uint32_t ax25_dbg_tooLong    = 0U;
volatile uint32_t ax25_dbg_crcPass    = 0U;   /* CRC passed — callback should fire */
volatile uint16_t ax25_dbg_lastLen    = 0U;   /* rxFrameLen of last >=18-byte frame */
/* Snapshot of the last frame that FAILED CRC (inspect in Memory view) */
#define AX25_DBG_FAIL_MAX 64U
volatile uint8_t  ax25_dbg_failFrame[AX25_DBG_FAIL_MAX];
volatile uint16_t ax25_dbg_failLen    = 0U;

/* Decoder state */
static bool     rxInFrame   = false;
static uint8_t  rxOnesCount = 0U;
static uint8_t  rxShiftReg  = 0U;
static uint8_t  rxBitCount  = 0U;

/* Maximum frame: dest(7)+src(7)+2×path(14)+ctrl(1)+pid(1)+info(256)+FCS(2)=288 */
#define RX_FRAME_BUF_SIZE   (7U + 7U + 14U + 2U + AX25_MAX_INFO_LEN + 2U)

static uint8_t  rxFrameBuf[RX_FRAME_BUF_SIZE];
static uint16_t rxFrameLen  = 0U;

static void rxReset(void)
{
    rxInFrame   = false;
    rxOnesCount = 0U;
    rxShiftReg  = 0U;
    rxBitCount  = 0U;
    rxFrameLen  = 0U;
}

static void rxProcessFrame(void)
{
    /* Minimum valid frame: dest(7)+src(7)+ctrl(1)+pid(1)+FCS(2) = 18 bytes */
    if (rxFrameLen < 18U)
    {
        ax25_dbg_tooShort++;
        return;
    }

    ax25_dbg_lastLen = rxFrameLen;  /* Record length before CRC check */

    uint16_t dataLen = rxFrameLen - 2U;  /* Bytes before FCS */
    uint16_t crcCalc = crc16_block(rxFrameBuf, dataLen);
    uint16_t crcRcvd = (uint16_t)rxFrameBuf[dataLen] |
                       ((uint16_t)rxFrameBuf[dataLen + 1U] << 8U);

    if (crcCalc != crcRcvd)
    {
        ax25_dbg_crcFail++;   /* CRC mismatch — NRZI polarity? noise? */
        /* Capture the failing frame bytes for inspection in Memory view */
        ax25_dbg_failLen = (rxFrameLen < AX25_DBG_FAIL_MAX) ? rxFrameLen : AX25_DBG_FAIL_MAX;
        for (uint16_t i = 0U; i < ax25_dbg_failLen; i++)
            ax25_dbg_failFrame[i] = rxFrameBuf[i];
        return;
    }

    ax25_dbg_crcPass++;  /* CRC passed — if g_dbg_rxFrames stays 0, callback is broken */

    /* ---- Raw-frame (digi) callback: deliver frame WITHOUT FCS ---- */
    if (digiCallback != NULL)
        digiCallback(rxFrameBuf, dataLen);

    /* ---- Info callback (loopback / tracker monitor) ---- */
    if (rxCallback == NULL)
        return;

    /* Extract source callsign from bytes [7..12] (each >> 1) */
    char srcCall[10] = {0};
    uint8_t callLen = 0U;
    for (uint8_t i = 0U; i < 6U; i++)
    {
        char c = (char)(rxFrameBuf[7U + i] >> 1U);
        if (c == ' ') break;
        srcCall[callLen++] = c;
    }

    /* Append SSID if non-zero */
    uint8_t ssid = (rxFrameBuf[13U] >> 1U) & 0x0FU;
    if (ssid != 0U)
    {
        srcCall[callLen++] = '-';
        if (ssid >= 10U) { srcCall[callLen++] = '1'; ssid -= 10U; }
        srcCall[callLen++] = (char)('0' + ssid);
    }
    srcCall[callLen] = '\0';

    /* Find end of address field (extension bit = 1 in SSID byte, bit 0) */
    uint16_t addrEnd = 0U;
    for (uint16_t i = 6U; i + 7U <= dataLen; i += 7U)
    {
        if (rxFrameBuf[i] & 0x01U)   /* extension bit set = last address */
        {
            addrEnd = i + 1U;
            break;
        }
    }

    /* ctrl + pid follow the address field */
    uint16_t infoStart = addrEnd + 2U;

    if (infoStart < dataLen)
        rxCallback(&rxFrameBuf[infoStart], dataLen - infoStart, srcCall);
}

/* ================================================================
 * AX25_Init / AX25_RxBit
 * ================================================================ */

void AX25_Init(AX25_RxCallback_t cb)
{
    AX25_Init2(cb, NULL);
}

void AX25_Init2(AX25_RxCallback_t infoCb, AX25_DigiCallback_t digiCb)
{
    rxCallback   = infoCb;
    digiCallback = digiCb;
    rxReset();
}

void AX25_RxBit(uint8_t bit)
{
    /* ---- Track consecutive ones for flag/stuff detection ---- */
    if (bit)
    {
        rxOnesCount++;
        if (rxOnesCount > 6U)
        {
            /* Seven or more ones — abort */
            rxReset();
            return;
        }
    }

    /* ---- Zero bit — handle flag and stuffing ---- */
    if (!bit)
    {
        if (rxOnesCount == 6U)
        {
            /* HDLC flag: 01111110 — frame boundary */
            ax25_dbg_flagCount++;
            if (rxInFrame)
                rxProcessFrame();   /* Validate and deliver completed frame */
            rxReset();
            rxInFrame = true;       /* Ready to receive next frame */
            ax25_dbg_frameStart++;
            return;
        }
        if (rxOnesCount == 5U)
        {
            /* Stuffed zero — discard and continue */
            rxOnesCount = 0U;
            return;
        }
        rxOnesCount = 0U;
    }

    if (!rxInFrame)
        return;   /* Waiting for opening flag */

    /* ---- Accumulate frame byte (LSB first) ---- */
    rxShiftReg = (uint8_t)((rxShiftReg >> 1U) | (bit ? 0x80U : 0x00U));
    rxBitCount++;

    if (rxBitCount == 8U)
    {
        rxBitCount = 0U;
        if (rxFrameLen < RX_FRAME_BUF_SIZE)
        {
            rxFrameBuf[rxFrameLen++] = rxShiftReg;
        }
        else
        {
            ax25_dbg_tooLong++;
            rxReset();  /* Frame too long — discard */
        }
    }
}

/* ================================================================
 * AX25_BuildDigiTxFrame
 *
 * Re-encode a digipeater-modified raw frame (no FCS) into the AFSK
 * TX bit-stream. Recomputes FCS and wraps with standard preamble /
 * postamble flags plus HDLC bit stuffing.
 * ================================================================ */
bool AX25_BuildDigiTxFrame(const uint8_t *frame, uint16_t frameLen)
{
    if (frame == NULL || frameLen == 0U)
        return false;

    memset(txBitBuf, 0, sizeof(txBitBuf));
    txBitCount = 0U;
    txOverflow = false;

    uint16_t txDelayFlags = (uint16_t)(((uint32_t)AX25_TX_DELAY_MS *
                                        (uint32_t)AFSK_BAUD_RATE + 7999UL) / 8000UL);
    uint16_t txTailFlags  = (uint16_t)(((uint32_t)AX25_TX_TAIL_MS *
                                        (uint32_t)AFSK_BAUD_RATE + 7999UL) / 8000UL);

    /* Preamble */
    for (uint16_t f = 0U; f < txDelayFlags; f++)
        appendBytePlain(AX25_FLAG);
    for (uint8_t f = 0U; f < AX25_HEADER_FLAGS; f++)
        appendBytePlain(AX25_FLAG);

    /* Frame content with bit stuffing */
    uint8_t onesCount = 0U;
    for (uint16_t i = 0U; i < frameLen; i++)
        appendByteStuffed(frame[i], &onesCount);

    /* Recomputed FCS, LSB first */
    uint16_t crc = crc16_block(frame, frameLen);
    appendByteStuffed((uint8_t)(crc & 0xFFU), &onesCount);
    appendByteStuffed((uint8_t)(crc >> 8U),   &onesCount);

    /* Postamble */
    for (uint8_t f = 0U; f < AX25_FOOTER_FLAGS; f++)
        appendBytePlain(AX25_FLAG);
    for (uint16_t f = 0U; f < txTailFlags; f++)
        appendBytePlain(AX25_FLAG);

    if (txOverflow)
        return false;

    AFSK_TX_Load(txBitBuf, txBitCount);
    return true;
}
