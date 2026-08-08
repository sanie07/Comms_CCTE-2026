/**
 * @file AX25_SubGHz.c
 * @brief Implementación del protocolo AX.25 para el middleware SubGHz_Phy.
 *
 * Portado desde RadioLib (C++) a C puro para STM32WLE5 con middleware SubGHz_Phy.
 * Sin memoria dinámica (heap). Compatible con cualquier compilador C99 o superior.
 *
 * Para transmitir usar AX25SG_BuildFrame() o AX25SG_BuildUIFrame() y pasar
 * el buffer resultante directamente a Radio.Send(buffer, len).
 *
 * Uso típico 9600 bps (G3RUH):
 * @code
 *   AX25SG_Client_t client;
 *   AX25SG_Init(&client, "MYCALL", 0, 16);
 *   AX25SG_SetScrambler(&client, AX25SG_SCRAMBLER_G3RUH_POLY, AX25SG_SCRAMBLER_G3RUH_INIT);
 *
 *   uint8_t txBuf[AX25SG_MAX_FRAME_BUF];
 *   uint16_t len = AX25SG_BuildUIFrame(&client, "Hola desde STM32!", "APRS", 0, txBuf, sizeof(txBuf));
 *   Radio.Send(txBuf, len);
 * @endcode
 *
 * Uso típico 1200 bps (NRZI, sin G3RUH):
 * @code
 *   AX25SG_Client_t client;
 *   AX25SG_Init(&client, "MYCALL", 0, 16);
 *   // NO llamar AX25SG_SetScrambler → scramblerPoly = 0 por defecto
 *
 *   uint8_t txBuf[AX25SG_MAX_FRAME_BUF];
 *   uint16_t len = AX25SG_BuildUIFrame(&client, "Hola!", "NOCALL", 0, txBuf, sizeof(txBuf));
 *   Radio.Send(txBuf, len);
 * @endcode
 */

#include "Protocol/AX25_SubGHz.h"
#include <string.h>

/* =========================================================================
 * Utilidades internas — CRC-CCITT (FCS AX.25)
 * =========================================================================
 * AX.25 usa CRC-CCITT: poly=0x1021, init=0xFFFF, refIn=false, refOut=false,
 * finalXOR=0x0000. Los bytes del frame se reflejan (bit-reverse) antes de
 * calcular el CRC, igual que hace RadioLib.
 * ========================================================================= */

/**
 * @brief Refleja (invierte el orden de bits) un byte.
 */
static uint8_t reflect8(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0U) >> 4U) | ((b & 0x0FU) << 4U));
    b = (uint8_t)(((b & 0xCCU) >> 2U) | ((b & 0x33U) << 2U));
    b = (uint8_t)(((b & 0xAAU) >> 1U) | ((b & 0x55U) << 1U));
    return b;
}

/**
 * @brief Calcula CRC-CCITT-FALSE sobre un bloque de bytes.
 *
 * Los bytes se ingresan tal cual (sin reflejar), poly=0x1021, init=0xFFFF.
 * El resultado NO se refleja y NO se aplica XOR final (out=0x0000).
 */
static uint16_t crc_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t j = 0U; j < 8U; j++) {
            if (crc & 0x8000U)
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            else
                crc <<= 1U;
        }
    }
    return crc;
}

/* =========================================================================
 * Utilidades internas — Buffer de bits con NRZI / NRZ
 * =========================================================================
 * La FIFO del radio SubGHz transfiere bytes empezando por el MSB (bit 7).
 * Para que el primer bit AX.25 (LSB del primer byte del frame) se transmita
 * primero por el aire, guardamos los bits en orden MSB-first dentro del
 * buffer: el primer bit va al bit 7, el segundo al bit 6, etc.
 * ========================================================================= */

typedef struct {
    uint8_t  *buf;
    uint16_t  maxLen;        /* Máximo en bytes */
    uint32_t  bitCount;      /* Bits escritos hasta ahora */
    uint8_t   onesCount;     /* Contador de '1' consecutivos para bit-stuffing */
    uint8_t   nrziState;     /* Último estado de salida NRZI (0 o 1) */
    bool      useNrzi;       /* true → NRZI (1200 bps); false → NRZ directo (9600 G3RUH) */
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
        /* NRZI: '0' → conmuta el estado; '1' → mantiene el estado */
        if (bit == 0U) {
            bb->nrziState ^= 1U;
        }
        outBit = bb->nrziState;
    } else {
        /* NRZ directo (el scrambler G3RUH ya garantiza aleatoriedad) */
        outBit = bit;
    }

    uint32_t byteIdx = bb->bitCount >> 3U;
    uint8_t  bitIdx  = (uint8_t)(bb->bitCount & 7U);

    if (bitIdx == 0U) {
        bb->buf[byteIdx] = 0U;  /* Inicializar byte al empezar */
    }

    /* MSB-first dentro del byte para coincidir con el orden de transmisión
     * de la FIFO SubGHz */
    if (outBit) {
        bb->buf[byteIdx] |= (uint8_t)(1U << (7U - bitIdx));
    }

    bb->bitCount++;
}

/** Escribe 8 bits de un byte sin bit-stuffing (para flags y preámbulo) */
static void bb_appendBytePlain(BitBuf_t *bb, uint8_t byte)
{
    /* RadioLib itera MSB-first al procesar los bytes del frame ya reflejado.
     * Para el flag/preámbulo (0x7E) emitimos los bits MSB-first. */
    for (int8_t i = 7; i >= 0; i--) {
        bb_appendBit(bb, (byte >> (uint8_t)i) & 1U);
    }
}

/** Escribe 8 bits con bit-stuffing HDLC (para datos del frame) */
static void bb_appendByteStuffed(BitBuf_t *bb, uint8_t byte)
{
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t bit = (byte >> (uint8_t)i) & 1U;
        bb_appendBit(bb, bit);

        if (bit) {
            bb->onesCount++;
            if (bb->onesCount == 5U) {
                bb_appendBit(bb, 0U);   /* Bit de relleno (stuffed zero) */
                bb->onesCount = 0U;
            }
        } else {
            bb->onesCount = 0U;
        }
    }
}

/* =========================================================================
 * Utilidades internas — Scrambler G3RUH (para 9600 bps)
 * =========================================================================
 * Polinomio: 1 + X^12 + X^17   (taps en posición 12 y 17 del registro)
 * El scrambler se inicializa en 0 y procesa bit a bit.
 * ========================================================================= */

/**
 * @brief Aplica G3RUH bit a bit, emite bit codificado, sin bit-stuffing.
 */
static void bb_appendBytePlainG3RUH(BitBuf_t *bb, uint8_t byte, uint32_t *sr)
{
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t bit    = (byte >> (uint8_t)i) & 1U;
        uint8_t outBit = (uint8_t)(bit ^
                         ((*sr >> 16U) & 1U) ^
                         ((*sr >> 11U) & 1U));
        *sr = ((*sr << 1U) | (uint32_t)outBit) & 0x1FFFFUL;
        bb_appendBit(bb, outBit);
    }
}

/**
 * @brief Aplica G3RUH bit a bit, emite bit codificado, CON bit-stuffing.
 *
 * @note  El bit stuffed se codifica también a través del scrambler, igual
 *        que hace RadioLib.
 */
static void bb_appendByteStuffedG3RUH(BitBuf_t *bb, uint8_t byte, uint32_t *sr)
{
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t bit    = (byte >> (uint8_t)i) & 1U;
        uint8_t outBit = (uint8_t)(bit ^
                         ((*sr >> 16U) & 1U) ^
                         ((*sr >> 11U) & 1U));
        *sr = ((*sr << 1U) | (uint32_t)outBit) & 0x1FFFFUL;
        bb_appendBit(bb, outBit);

        if (bit) {
            bb->onesCount++;
            if (bb->onesCount == 5U) {
                /* Insertar cero de stuffing, también pasa por el scrambler */
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
 * Construcción de la cabecera AX.25 (campo de dirección)
 * =========================================================================
 * Cada estación ocupa 7 bytes: 6 bytes de callsign (ASCII << 1) + 1 byte SSID.
 * El bit 0 del byte SSID es el bit de extensión HDLC:
 *   0 = hay más direcciones
 *   1 = última dirección
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
    if (isCommand) { sb |= 0x80U; }          /* C/H bit */
    sb |= (uint8_t)((ssid & 0x0FU) << 1U);
    if (isLast)   { sb |= AX25SG_SSID_HDLC_END; }
    out[6] = sb;
}

/* =========================================================================
 * Construcción del frame raw (sin flags, sin stuffing, sin encoding)
 * ========================================================================= */

/**
 * @brief Serializa la cabecera + control + PID + info + FCS en frameBuf.
 *
 * Este es el "frame raw" al que RadioLib le aplica reflect8() byte a byte
 * antes de calcular el CRC.  Aquí lo hacemos directamente.
 *
 * @return Número de bytes escritos, 0 si error.
 */
static uint16_t buildRawFrame(const AX25SG_Client_t *client,
                              const AX25SG_Frame_t  *frame,
                              uint8_t *buf, uint16_t maxLen)
{
    /* Tamaño mínimo requerido:
     *   (2 + numRepeaters) * 7  +  1 (ctrl)  +  1 (PID si aplica)  +  infoLen  +  2 (FCS)
     */
    uint16_t addrLen   = (uint16_t)(2U + frame->numRepeaters) * 7U;
    uint16_t minNeeded = addrLen + 1U + (frame->protocolID ? 1U : 0U) + frame->infoLen + 2U;
    if (minNeeded > maxLen) {
        return 0U;
    }

    uint16_t pos = 0U;

    /* ── Dirección destino ────────────────────────────────────────────── */
    bool destIsLast = (frame->numRepeaters == 0U) &&
                      (strlen(frame->srcCallsign) == 0U);
    buildAddr(frame->destCallsign, frame->destSSID,
              destIsLast, true, &buf[pos]);
    pos += 7U;

    /* ── Dirección origen ─────────────────────────────────────────────── */
    bool srcIsLast = (frame->numRepeaters == 0U);
    buildAddr(frame->srcCallsign, frame->srcSSID,
              srcIsLast, false, &buf[pos]);
    pos += 7U;

    /* ── Repetidores ──────────────────────────────────────────────────── */
    for (uint8_t r = 0U; r < frame->numRepeaters; r++) {
        bool repIsLast = (r == (frame->numRepeaters - 1U));
        buildAddr(frame->repeaterCallsigns[r], frame->repeaterSSIDs[r],
                  repIsLast, false, &buf[pos]);
        pos += 7U;
    }

    /* ── Aplicar reflect8 a todo el campo de dirección (igual que RadioLib) */
    for (uint16_t i = 0U; i < pos; i++) {
        buf[i] = reflect8(buf[i]);
    }

    /* ── Control ──────────────────────────────────────────────────────── */
    uint8_t ctrl = frame->control;
    /* I-frame: añadir números de secuencia */
    if ((ctrl & 0x01U) == 0U) {
        ctrl |= (uint8_t)(frame->rcvSeqNumber  << 5U);
        ctrl |= (uint8_t)(frame->sendSeqNumber << 1U);
    } else if ((ctrl & 0x02U) == 0U) {
        /* S-frame: solo Ns */
        ctrl |= (uint8_t)(frame->rcvSeqNumber << 5U);
    }
    buf[pos++] = ctrl;

    /* ── PID ──────────────────────────────────────────────────────────── */
    if (frame->protocolID != 0x00U) {
        buf[pos++] = frame->protocolID;
    }

    /* ── Info ─────────────────────────────────────────────────────────── */
    if (frame->infoLen > 0U) {
        memcpy(&buf[pos], frame->info, frame->infoLen);
        pos += frame->infoLen;
    }

    /* ── FCS (CRC-CCITT sobre todos los bytes anteriores) ─────────────── */
    /* RadioLib calcula el CRC sobre el buffer ya con reflect8 en las dir.
     * y lo almacena en big-endian (MSB primero). */
    uint16_t fcs = crc_ccitt(buf, pos);
    buf[pos++] = (uint8_t)((fcs >> 8U) & 0xFFU);
    buf[pos++] = (uint8_t)( fcs        & 0xFFU);

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

    return 0;
}

void AX25SG_SetScrambler(AX25SG_Client_t *client, uint32_t poly, uint32_t init)
{
    if (client == NULL) { return; }
    client->scramblerPoly = poly;
    client->scramblerInit = init;
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

    /* ── 1. Construir el frame raw ───────────────────────────────────── */
    /* Buffer interno estático para el frame raw antes del encoding */
    static uint8_t rawBuf[AX25SG_MAX_INFO_LEN + 64U];  /* Max addr + info + FCS */
    uint16_t rawLen = buildRawFrame(client, frame, rawBuf, sizeof(rawBuf));
    if (rawLen == 0U) { return 0U; }

    /* ── 2. Inicializar BitBuffer ─────────────────────────────────────── */
    bool useNrzi = (client->scramblerPoly == 0U);  /* NRZI si no hay scrambler */

    BitBuf_t bb = {
        .buf       = outBuf,
        .maxLen    = outMaxLen,
        .bitCount  = 0U,
        .onesCount = 0U,
        .nrziState = 0U,
        .useNrzi   = useNrzi,
        .overflow  = false
    };

    /* ── 3. Preámbulo ─────────────────────────────────────────────────── */
    if (client->scramblerPoly != 0U) {
        /* G3RUH: preámbulo también pasa por el scrambler */
        uint32_t sr = client->scramblerInit;
        for (uint8_t f = 0U; f < client->preambleLen + 1U; f++) {
            bb_appendBytePlainG3RUH(&bb, AX25SG_FLAG, &sr);
        }

        /* ── 4G. Datos con bit-stuffing + G3RUH ─────────────────────── */
        for (uint16_t i = 0U; i < rawLen; i++) {
            bb_appendByteStuffedG3RUH(&bb, rawBuf[i], &sr);
        }

        /* ── 5G. Flag de cierre + G3RUH ─────────────────────────────── */
        bb_appendBytePlainG3RUH(&bb, AX25SG_FLAG, &sr);

    } else {
        /* NRZI: preámbulo sin stuffing */
        for (uint8_t f = 0U; f < client->preambleLen + 1U; f++) {
            bb_appendBytePlain(&bb, AX25SG_FLAG);
        }

        /* ── 4N. Datos con bit-stuffing + NRZI ──────────────────────── */
        for (uint16_t i = 0U; i < rawLen; i++) {
            bb_appendByteStuffed(&bb, rawBuf[i]);
        }

        /* ── 5N. Flag de cierre + NRZI ───────────────────────────────── */
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
