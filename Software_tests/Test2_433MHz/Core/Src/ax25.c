#include "ax25.h"
#include "aprs_config.h"
#include <string.h>

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

    uint8_t sb = 0x60U;               /* bits 6,5 reservado (1) */
    if (isCommand)  sb |= 0x80U;      /* C/H bit */
    sb |= (uint8_t)((ssid & 0x0FU) << 1U);
    if (isLast)     sb |= 0x01U;      /* bit extension */
    out[6] = sb;
}

/* ================================================================
 * Frame Building Functions
 * ================================================================ */

uint16_t AX25_BuildRawFrame(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf)
{
    uint16_t fLen = 0U;
    bool hasPath1 = (APRS_PATH1CALL[0] != '\0');
    bool hasPath2 = hasPath1 && (APRS_PATH2CALL[0] != '\0');

    /* Destino */
    encodeAddr(APRS_DESTCALL, APRS_DESTSSID, false, true, &outBuf[fLen]);
    fLen += 7U;

    /* Origen */
    encodeAddr(APRS_MYCALL, APRS_MYSSID, !hasPath1, false, &outBuf[fLen]);
    fLen += 7U;

    /* Rutas */
    if (hasPath1)
    {
        encodeAddr(APRS_PATH1CALL, APRS_PATH1SSID, !hasPath2, false, &outBuf[fLen]);
        fLen += 7U;
    }
    if (hasPath2)
    {
        encodeAddr(APRS_PATH2CALL, APRS_PATH2SSID, true, false, &outBuf[fLen]);
        fLen += 7U;
    }

    /* Control y PID */
    outBuf[fLen++] = AX25_UI_CTRL;
    outBuf[fLen++] = AX25_PID_NO_L3;

    /* Payload */
    if (infoLen > AX25_MAX_INFO_LEN)
        infoLen = AX25_MAX_INFO_LEN;
    memcpy(&outBuf[fLen], info, infoLen);
    fLen += infoLen;

    /* FCS (CRC-16) */
    uint16_t crc = crc16_block(outBuf, fLen);
    outBuf[fLen++] = (uint8_t)(crc & 0xFFU);
    outBuf[fLen++] = (uint8_t)(crc >> 8U);

    return fLen;
}

/* Manejo de buffer a nivel bit para stuffing, NRZI y G3RUH */
typedef struct {
    uint8_t *buf;
    uint16_t maxLen;
    uint16_t bitCount;
    uint8_t onesCount;
    uint8_t nrziState;  /* Estado actual NRZI (0 o 1) */
    bool useNrzi;       /* true = aplica NRZI (1200 baud), false = NRZ directo (9600 baud G3RUH) */
    bool overflow;
} BitBuffer_t;

static void appendBit(BitBuffer_t *bb, uint8_t bit)
{
    if ((bb->bitCount >> 3U) >= bb->maxLen) {
        bb->overflow = true;
        return;
    }

    uint8_t outBit;
    if (bb->useNrzi) {
        /* Codificación NRZI: un '0' conmuta el estado, un '1' lo mantiene */
        if (bit == 0U) {
            bb->nrziState ^= 1U;
        }
        outBit = bb->nrziState;
    } else {
        /* Codificación NRZ directo */
        outBit = bit;
    }

    uint16_t byteIdx = bb->bitCount >> 3U;
    uint8_t  bitIdx  = (uint8_t)(bb->bitCount & 7U);

    if (bitIdx == 0U) bb->buf[byteIdx] = 0U; // Inicializar byte al comenzar primer bit

    /* La FIFO de la radio SubGHz transfiere cada byte comenzando por el MSB (bit 7).
     * Para que el primer bit generado (LSB del byte AX.25) se transmita PRIMERO por el aire,
     * guardamos el primer bit en el MSB (bit 7) y el último en el LSB (bit 0).
     */
    uint8_t hwBitPos = 7U - bitIdx;

    if (outBit) bb->buf[byteIdx] |=  (uint8_t)(1U << hwBitPos);
    else        bb->buf[byteIdx] &= (uint8_t)~(1U << hwBitPos);

    bb->bitCount++;
}

static void appendBytePlain(BitBuffer_t *bb, uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++)
        appendBit(bb, (byte >> i) & 1U);
}

static void appendByteStuffed(BitBuffer_t *bb, uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint8_t bit = (byte >> i) & 1U;
        appendBit(bb, bit);
        if (bit) {
            bb->onesCount++;
            if (bb->onesCount == 5U) {
                appendBit(bb, 0U);   /* Stuffed zero */
                bb->onesCount = 0U;
            }
        } else {
            bb->onesCount = 0U;
        }
    }
}

uint16_t AX25_Build1200Frame_NRZI(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen)
{
    uint8_t rawFrame[AX25_MAX_INFO_LEN + 35U]; // MAX addresses + ctrl + pid + FCS = 35
    uint16_t rawLen = AX25_BuildRawFrame(info, infoLen, rawFrame);

    BitBuffer_t bb = { .buf = outBuf, .maxLen = outMaxLen, .bitCount = 0, .onesCount = 0, .nrziState = 0, .useNrzi = true, .overflow = false };

    /* Preamble flags */
    for (uint8_t f = 0U; f < AX25_HEADER_FLAGS; f++)
        appendBytePlain(&bb, AX25_FLAG);

    /* Datos con bit stuffing */
    for (uint16_t i = 0U; i < rawLen; i++)
        appendByteStuffed(&bb, rawFrame[i]);

    /* Postamble flags */
    for (uint8_t f = 0U; f < AX25_FOOTER_FLAGS; f++)
        appendBytePlain(&bb, AX25_FLAG);

    if (bb.overflow)
        return 0;

    /* Devolver la longitud en bytes (redondeando hacia arriba) */
    return (bb.bitCount + 7U) / 8U;
}

uint16_t AX25_BuildStuffedFrame(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen)
{
    return AX25_Build1200Frame_NRZI(info, infoLen, outBuf, outMaxLen);
}

/* ================================================================
 * G3RUH Scrambler implementation for 9600 baud
 * ================================================================ */
static void appendBytePlainG3RUH(BitBuffer_t *bb, uint8_t byte, uint32_t *scrambler)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint8_t bit = (byte >> i) & 1U;
        // G3RUH Scrambler polynomial: 1 + X^12 + X^17
        uint8_t out_bit = bit ^ ((*scrambler >> 11) & 1) ^ ((*scrambler >> 16) & 1);
        *scrambler = (*scrambler << 1) | out_bit;
        appendBit(bb, out_bit);
    }
}

static void appendByteStuffedG3RUH(BitBuffer_t *bb, uint8_t byte, uint32_t *scrambler)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint8_t bit = (byte >> i) & 1U;
        
        uint8_t out_bit = bit ^ ((*scrambler >> 11) & 1) ^ ((*scrambler >> 16) & 1);
        *scrambler = (*scrambler << 1) | out_bit;
        appendBit(bb, out_bit);
        
        if (bit) {
            bb->onesCount++;
            if (bb->onesCount == 5U) {
                // Insert stuffed zero
                out_bit = 0 ^ ((*scrambler >> 11) & 1) ^ ((*scrambler >> 16) & 1);
                *scrambler = (*scrambler << 1) | out_bit;
                appendBit(bb, out_bit);
                bb->onesCount = 0U;
            }
        } else {
            bb->onesCount = 0U;
        }
    }
}

uint16_t AX25_Build9600Frame_G3RUH_NRZ(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen)
{
    uint8_t rawFrame[AX25_MAX_INFO_LEN + 35U];
    uint16_t rawLen = AX25_BuildRawFrame(info, infoLen, rawFrame);

    BitBuffer_t bb = { .buf = outBuf, .maxLen = outMaxLen, .bitCount = 0, .onesCount = 0, .nrziState = 0, .useNrzi = false, .overflow = false };
    uint32_t scrambler = 0;

    /* Preamble flags */
    for (uint8_t f = 0U; f < AX25_HEADER_FLAGS; f++)
        appendBytePlainG3RUH(&bb, AX25_FLAG, &scrambler);

    /* Datos con bit stuffing */
    for (uint16_t i = 0U; i < rawLen; i++)
        appendByteStuffedG3RUH(&bb, rawFrame[i], &scrambler);

    /* Postamble flags */
    for (uint8_t f = 0U; f < AX25_FOOTER_FLAGS; f++)
        appendBytePlainG3RUH(&bb, AX25_FLAG, &scrambler);

    if (bb.overflow)
        return 0;

    return (bb.bitCount + 7U) / 8U;
}

uint16_t AX25_BuildG3RUHFrame(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen)
{
    return AX25_Build9600Frame_G3RUH_NRZ(info, infoLen, outBuf, outMaxLen);
}
