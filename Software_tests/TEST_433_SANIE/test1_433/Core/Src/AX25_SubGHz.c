/**
 * @file AX25_SubGHz.c
 * @brief Implementación estándar de AX.25 HDLC (NRZI y G3RUH) para STM32WLE5 / SubGHz_Phy.
 *
 * Compatible al 100% con SoundModem, DireWolf, UZ7HO hs_soundmodem, SDR y módulos LoRa/FSK.
 *
 * Características:
 *  - Transmisión LSB-first para todos los bytes según estándar AX.25 / HDLC
 *  - Bit-stuffing HDLC (inserción de '0' tras cinco '1's consecutivos)
 *  - CRC-16 CCITT (polinomio 0x8408 reflejado, inicial 0xFFFF, negación final)
 *  - Codificación NRZI estándar para 1200 bps
 *  - Scrambler G3RUH opcional para 9600 bps / G3RUH FSK
 */

#include "AX25_SubGHz.h"
#include <string.h>

/* =========================================================================
 * Cálculo de CRC-16 CCITT (FCS estándar AX.25 / HDLC)
 * ========================================================================= */

static uint16_t ax25_crc16(const uint8_t *data, size_t len)
{
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

/* =========================================================================
 * Buffer de bits con NRZI / G3RUH y orden MSB-first en FIFO
 * ========================================================================= */

typedef struct {
    uint8_t  *buf;
    uint16_t  maxLen;
    uint32_t  bitCount;
    uint8_t   onesCount;
    uint8_t   nrziState;
    bool      useNrzi;
    bool      invertPolarity;
    bool      overflow;
} BitBuf_t;

static void bb_appendBit(BitBuf_t *bb, uint8_t bit)
{
    if ((bb->bitCount >> 3U) >= (uint32_t)bb->maxLen) {
        bb->overflow = true;
        return;
    }

    uint8_t outBit;
    if (bb->useNrzi) {
        /* NRZI: '0' cambia el estado, '1' mantiene el estado */
        if (bit == 0U) {
            bb->nrziState ^= 1U;
        }
        outBit = bb->nrziState;
    } else {
        outBit = bit;
    }

    if (bb->invertPolarity) {
        outBit ^= 1U;
    }

    uint32_t byteIdx = bb->bitCount >> 3U;
    uint8_t  bitIdx  = (uint8_t)(bb->bitCount & 7U);

    if (bitIdx == 0U) {
        bb->buf[byteIdx] = 0U;
    }

    /* La FIFO del radio SubGHz transmite MSB primero (bit 7 a bit 0) */
    if (outBit) {
        bb->buf[byteIdx] |= (uint8_t)(1U << (7U - bitIdx));
    }

    bb->bitCount++;
}

/** Escribe un byte (bit 0 a bit 7, LSB first) sin bit-stuffing (para flags) */
static void bb_appendBytePlain(BitBuf_t *bb, uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        bb_appendBit(bb, (byte >> i) & 1U);
    }
}

/** Escribe un byte (bit 0 a bit 7, LSB first) con bit-stuffing HDLC */
static void bb_appendByteStuffed(BitBuf_t *bb, uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        uint8_t bit = (byte >> i) & 1U;
        bb_appendBit(bb, bit);

        if (bit) {
            bb->onesCount++;
            if (bb->onesCount == 5U) {
                bb_appendBit(bb, 0U);   /* Cero de relleno */
                bb->onesCount = 0U;
            }
        } else {
            bb->onesCount = 0U;
        }
    }
}

/* =========================================================================
 * Scrambler G3RUH (para 9600 bps o Direct FSK G3RUH)
 * ========================================================================= */

static void bb_appendBytePlainG3RUH(BitBuf_t *bb, uint8_t byte, uint32_t *sr)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        uint8_t bit    = (byte >> i) & 1U;
        uint8_t outBit = (uint8_t)(bit ^
                         ((*sr >> 16U) & 1U) ^
                         ((*sr >> 11U) & 1U));
        *sr = ((*sr << 1U) | (uint32_t)outBit) & 0x1FFFFUL;
        bb_appendBit(bb, outBit);
    }
}

static void bb_appendByteStuffedG3RUH(BitBuf_t *bb, uint8_t byte, uint32_t *sr)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        uint8_t bit    = (byte >> i) & 1U;
        uint8_t outBit = (uint8_t)(bit ^
                         ((*sr >> 16U) & 1U) ^
                         ((*sr >> 11U) & 1U));
        *sr = ((*sr << 1U) | (uint32_t)outBit) & 0x1FFFFUL;
        bb_appendBit(bb, outBit);

        if (bit) {
            bb->onesCount++;
            if (bb->onesCount == 5U) {
                uint8_t stuff = (uint8_t)(0U ^
                                ((*sr >> 16U) & 1U) ^
                                ((*sr >> 11U) & 1U));
                *sr = ((*sr << 1U) | (uint32_t)stuff) & 0x1FFFFUL;
                bb_appendBit(bb, stuff);
                bb->onesCount = 0U;
            }
        } else {
            bb->onesCount = 0U;
        }
    }
}

/* =========================================================================
 * Construcción de campo de dirección AX.25
 * ========================================================================= */

static void buildAddr(const char *callsign, uint8_t ssid,
                      bool isLast, bool isCommand,
                      uint8_t *out)
{
    size_t clen = strlen(callsign);
    for (uint8_t i = 0U; i < AX25SG_MAX_CALLSIGN_LEN; i++) {
        char c = (i < clen) ? callsign[i] : ' ';
        out[i] = (uint8_t)((uint8_t)c << 1U);
    }
    uint8_t sb = AX25SG_SSID_RESERVED_BITS;
    if (isCommand) { sb |= 0x80U; }
    sb |= (uint8_t)((ssid & 0x0FU) << 1U);
    if (isLast)   { sb |= AX25SG_SSID_HDLC_END; }
    out[6] = sb;
}

static uint16_t buildRawFrame(const AX25SG_Client_t *client,
                              const AX25SG_Frame_t  *frame,
                              uint8_t *buf, uint16_t maxLen)
{
    uint16_t addrLen   = (uint16_t)(2U + frame->numRepeaters) * 7U;
    uint16_t minNeeded = addrLen + 1U + (frame->protocolID ? 1U : 0U) + frame->infoLen + 2U;
    if (minNeeded > maxLen) {
        return 0U;
    }

    uint16_t pos = 0U;

    /* ── 1. Dirección destino ─────────────────────────────────────────── */
    bool destIsLast = (frame->numRepeaters == 0U) &&
                      (strlen(frame->srcCallsign) == 0U);
    buildAddr(frame->destCallsign, frame->destSSID,
              destIsLast, true, &buf[pos]);
    pos += 7U;

    /* ── 2. Dirección origen ──────────────────────────────────────────── */
    bool srcIsLast = (frame->numRepeaters == 0U);
    buildAddr(frame->srcCallsign, frame->srcSSID,
              srcIsLast, false, &buf[pos]);
    pos += 7U;

    /* ── 3. Repetidores ───────────────────────────────────────────────── */
    for (uint8_t r = 0U; r < frame->numRepeaters; r++) {
        bool repIsLast = (r == (frame->numRepeaters - 1U));
        buildAddr(frame->repeaterCallsigns[r], frame->repeaterSSIDs[r],
                  repIsLast, false, &buf[pos]);
        pos += 7U;
    }

    /* ── 4. Control ───────────────────────────────────────────────────── */
    uint8_t ctrl = frame->control;
    if ((ctrl & 0x01U) == 0U) {
        ctrl |= (uint8_t)(frame->rcvSeqNumber  << 5U);
        ctrl |= (uint8_t)(frame->sendSeqNumber << 1U);
    } else if ((ctrl & 0x02U) == 0U) {
        ctrl |= (uint8_t)(frame->rcvSeqNumber << 5U);
    }
    buf[pos++] = ctrl;

    /* ── 5. PID ───────────────────────────────────────────────────────── */
    if (frame->protocolID != 0x00U) {
        buf[pos++] = frame->protocolID;
    }

    /* ── 6. Campo Info ────────────────────────────────────────────────── */
    if (frame->infoLen > 0U) {
        memcpy(&buf[pos], frame->info, frame->infoLen);
        pos += frame->infoLen;
    }

    /* ── 7. FCS (CRC-16 CCITT) ────────────────────────────────────────── */
    uint16_t fcs = ax25_crc16(buf, pos);
    buf[pos++] = (uint8_t)(fcs & 0xFFU);         /* Low byte first */
    buf[pos++] = (uint8_t)((fcs >> 8U) & 0xFFU);  /* High byte second */

    return pos;
}

/* =========================================================================
 * API Pública — Implementación
 * ========================================================================= */

int8_t AX25SG_Init(AX25SG_Client_t *client,
                   const char *callsign, uint8_t ssid,
                   uint8_t preamble)
{
    if ((client == NULL) || (callsign == NULL)) { return -1; }
    if (strlen(callsign) > AX25SG_MAX_CALLSIGN_LEN) { return -1; }

    memset(client, 0, sizeof(*client));
    memcpy(client->srcCallsign, callsign, strlen(callsign));
    client->srcCallsign[strlen(callsign)] = '\0';
    client->srcSSID       = ssid;
    client->preambleLen   = preamble;
    client->scramblerPoly = 0U;
    client->scramblerInit = 0U;
    client->invertPolarity = false;

    return 0;
}

void AX25SG_SetScrambler(AX25SG_Client_t *client, uint32_t poly, uint32_t init)
{
    if (client == NULL) { return; }
    client->scramblerPoly = poly;
    client->scramblerInit = init;
}

void AX25SG_SetInvert(AX25SG_Client_t *client, bool invert)
{
    if (client == NULL) { return; }
    client->invertPolarity = invert;
}

void AX25SG_FrameInit(AX25SG_Frame_t *frame,
                      const char *destCallsign, uint8_t destSSID,
                      const char *srcCallsign,  uint8_t srcSSID,
                      uint8_t control)
{
    if (frame == NULL) { return; }
    memset(frame, 0, sizeof(*frame));

    if (destCallsign != NULL) {
        size_t len = strlen(destCallsign);
        if (len > AX25SG_MAX_CALLSIGN_LEN) { len = AX25SG_MAX_CALLSIGN_LEN; }
        memcpy(frame->destCallsign, destCallsign, len);
    }
    frame->destSSID = destSSID;

    if (srcCallsign != NULL) {
        size_t len = strlen(srcCallsign);
        if (len > AX25SG_MAX_CALLSIGN_LEN) { len = AX25SG_MAX_CALLSIGN_LEN; }
        memcpy(frame->srcCallsign, srcCallsign, len);
    }
    frame->srcSSID  = srcSSID;
    frame->control  = control;
}

int8_t AX25SG_FrameInitUI(AX25SG_Frame_t *frame,
                           const char *destCallsign, uint8_t destSSID,
                           const char *srcCallsign,  uint8_t srcSSID,
                           const char *info)
{
    if ((frame == NULL) || (info == NULL)) { return -1; }

    AX25SG_FrameInit(frame, destCallsign, destSSID, srcCallsign, srcSSID,
                     AX25SG_CTRL_UFRAME | AX25SG_CTRL_U_UI);
    frame->protocolID = AX25SG_PID_NO_L3;

    uint16_t ilen = (uint16_t)strlen(info);
    if (ilen > AX25SG_MAX_INFO_LEN) { ilen = AX25SG_MAX_INFO_LEN; }
    memcpy(frame->info, info, ilen);
    frame->infoLen = ilen;

    return 0;
}

int8_t AX25SG_SetRepeaters(AX25SG_Frame_t *frame,
                            const char callsigns[][AX25SG_MAX_CALLSIGN_LEN + 1],
                            const uint8_t *ssids,
                            uint8_t count)
{
    if ((frame == NULL) || (callsigns == NULL) || (ssids == NULL)) { return -1; }
    if ((count < 1U) || (count > AX25SG_MAX_REPEATERS))            { return -1; }

    for (uint8_t i = 0U; i < count; i++) {
        size_t clen = strlen(callsigns[i]);
        if (clen > AX25SG_MAX_CALLSIGN_LEN) { return -1; }
        memcpy(frame->repeaterCallsigns[i], callsigns[i], clen);
        frame->repeaterCallsigns[i][clen] = '\0';
        frame->repeaterSSIDs[i] = ssids[i];
    }
    frame->numRepeaters = count;

    return 0;
}

uint16_t AX25SG_BuildFrame(const AX25SG_Client_t *client,
                           const AX25SG_Frame_t  *frame,
                           uint8_t *outBuf, uint16_t outMaxLen)
{
    if ((client == NULL) || (frame == NULL) || (outBuf == NULL)) { return 0U; }

    static uint8_t rawBuf[AX25SG_MAX_INFO_LEN + 64U];
    uint16_t rawLen = buildRawFrame(client, frame, rawBuf, sizeof(rawBuf));
    if (rawLen == 0U) { return 0U; }

    bool useNrzi = (client->scramblerPoly == 0U);

    BitBuf_t bb = {
        .buf            = outBuf,
        .maxLen         = outMaxLen,
        .bitCount       = 0U,
        .onesCount      = 0U,
        .nrziState      = 0U,
        .useNrzi        = false,
        .invertPolarity = client->invertPolarity,
        .overflow       = false
    };

    if (client->scramblerPoly != 0U) {
        uint32_t sr = client->scramblerInit;
        for (uint8_t f = 0U; f < client->preambleLen; f++) {
            bb_appendBytePlainG3RUH(&bb, AX25SG_FLAG, &sr);
        }
        for (uint16_t i = 0U; i < rawLen; i++) {
            bb_appendByteStuffedG3RUH(&bb, rawBuf[i], &sr);
        }
        bb_appendBytePlainG3RUH(&bb, AX25SG_FLAG, &sr);
    } else {
        for (uint8_t f = 0U; f < client->preambleLen; f++) {
            bb_appendBytePlain(&bb, AX25SG_FLAG);
        }
        for (uint16_t i = 0U; i < rawLen; i++) {
        	bb_appendBytePlain(&bb, rawBuf[i]);
        }
        bb_appendBytePlain(&bb, AX25SG_FLAG);
        bb_appendBytePlain(&bb, AX25SG_FLAG);
    }

    if (bb.overflow) { return 0U; }

    return (uint16_t)((bb.bitCount + 7U) / 8U);
}

uint16_t AX25SG_BuildUIFrame(const AX25SG_Client_t *client,
                              const char *str,
                              const char *destCallsign, uint8_t destSSID,
                              uint8_t *outBuf, uint16_t outMaxLen)
{
    if ((client == NULL) || (str == NULL) || (outBuf == NULL)) { return 0U; }

    AX25SG_Frame_t frame;
    if (AX25SG_FrameInitUI(&frame,
                            destCallsign, destSSID,
                            client->srcCallsign, client->srcSSID,
                            str) != 0) {
        return 0U;
    }

    return AX25SG_BuildFrame(client, &frame, outBuf, outMaxLen);
}
