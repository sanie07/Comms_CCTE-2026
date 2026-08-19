/**
 * @file    ax25.c
 * @brief   AX.25 UI frame encoder and decoder for APRS.
 *
 * All buffers are statically allocated — no malloc(), no RTOS.
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

static uint8_t  txBitBuf[AFSK_TX_BUF_BYTES];
static uint16_t txBitCount;
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

static void appendBytePlain(uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++)
        appendBit((byte >> i) & 1U);
}

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
                appendBit(0U);
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

static void encodeAddr(const char *call, uint8_t ssid,
                        bool isLast, bool isCommand,
                        uint8_t *out)
{
    for (uint8_t i = 0U; i < 6U; i++)
    {
        char c = (i < (uint8_t)strlen(call)) ? call[i] : ' ';
        out[i] = (uint8_t)((uint8_t)c << 1U);
    }

    uint8_t sb = 0x60U;
    if (isCommand)  sb |= 0x80U;
    sb |= (uint8_t)((ssid & 0x0FU) << 1U);
    if (isLast)     sb |= 0x01U;
    out[6] = sb;
}

/* ================================================================
 * AX25_BuildTxFrame
 * ================================================================ */

bool AX25_BuildTxFrame(const uint8_t *info, uint16_t infoLen)
{
    uint8_t  frame[7U + 7U + 7U + 7U + 1U + 1U + AX25_MAX_INFO_LEN + 2U];
    uint16_t fLen = 0U;

    bool hasPath1 = (APRS_PATH1CALL[0] != '\0');
    bool hasPath2 = hasPath1 && (APRS_PATH2CALL[0] != '\0');

    encodeAddr(APRS_DESTCALL, APRS_DESTSSID, false, true, &frame[fLen]);
    fLen += 7U;

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

    frame[fLen++] = AX25_UI_CTRL;
    frame[fLen++] = AX25_PID_NO_L3;

    if (infoLen > AX25_MAX_INFO_LEN)
        infoLen = AX25_MAX_INFO_LEN;
    memcpy(&frame[fLen], info, infoLen);
    fLen += infoLen;

    uint16_t crc = crc16_block(frame, fLen);
    frame[fLen++] = (uint8_t)(crc & 0xFFU);
    frame[fLen++] = (uint8_t)(crc >> 8U);

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

    uint8_t onesCount = 0U;
    for (uint16_t i = 0U; i < fLen; i++)
        appendByteStuffed(frame[i], &onesCount);

    for (uint8_t f = 0U; f < AX25_FOOTER_FLAGS; f++)
        appendBytePlain(AX25_FLAG);

    for (uint16_t f = 0U; f < txTailFlags; f++)
        appendBytePlain(AX25_FLAG);

    if (txOverflow)
        return false;

    AFSK_TX_Load(txBitBuf, txBitCount);
    return true;
}

/* ================================================================
 * RX decoder state machine
 * ================================================================ */

static AX25_RxCallback_t rxCallback  = NULL;

static bool     rxInFrame   = false;
static uint8_t  rxOnesCount = 0U;
static uint8_t  rxShiftReg  = 0U;
static uint8_t  rxBitCount  = 0U;

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
    if (rxFrameLen < 18U)
        return;

    uint16_t dataLen = rxFrameLen - 2U;
    uint16_t crcCalc = crc16_block(rxFrameBuf, dataLen);
    uint16_t crcRcvd = (uint16_t)rxFrameBuf[dataLen] |
                       ((uint16_t)rxFrameBuf[dataLen + 1U] << 8U);

    if (crcCalc != crcRcvd)
        return;

    if (rxCallback == NULL)
        return;

    char srcCall[10] = {0};
    uint8_t callLen = 0U;
    for (uint8_t i = 0U; i < 6U; i++)
    {
        char c = (char)(rxFrameBuf[7U + i] >> 1U);
        if (c == ' ') break;
        srcCall[callLen++] = c;
    }

    uint8_t ssid = (rxFrameBuf[13U] >> 1U) & 0x0FU;
    if (ssid != 0U)
    {
        srcCall[callLen++] = '-';
        if (ssid >= 10U) { srcCall[callLen++] = '1'; ssid -= 10U; }
        srcCall[callLen++] = (char)('0' + ssid);
    }
    srcCall[callLen] = '\0';

    uint16_t addrEnd = 0U;
    for (uint16_t i = 6U; i + 7U <= dataLen; i += 7U)
    {
        if (rxFrameBuf[i] & 0x01U)
        {
            addrEnd = i + 1U;
            break;
        }
    }

    uint16_t infoStart = addrEnd + 2U;

    if (infoStart < dataLen)
        rxCallback(&rxFrameBuf[infoStart], dataLen - infoStart, srcCall);
}

/* ================================================================
 * AX25_Init / AX25_RxBit
 * ================================================================ */

void AX25_Init(AX25_RxCallback_t cb)
{
    rxCallback = cb;
    rxReset();
}

void AX25_RxBit(uint8_t bit)
{
    if (bit)
    {
        rxOnesCount++;
        if (rxOnesCount > 6U)
        {
            rxReset();
            return;
        }
    }

    if (!bit)
    {
        if (rxOnesCount == 6U)
        {
            if (rxInFrame)
                rxProcessFrame();
            rxReset();
            rxInFrame = true;
            return;
        }
        if (rxOnesCount == 5U)
        {
            rxOnesCount = 0U;
            return;
        }
        rxOnesCount = 0U;
    }

    if (!rxInFrame)
        return;

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
            rxReset();
        }
    }
}
