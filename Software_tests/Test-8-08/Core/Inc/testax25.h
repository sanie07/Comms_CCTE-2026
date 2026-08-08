#ifndef TESTAX25_H
#define TESTAX25_H

#include <stdint.h>
#include <stdbool.h>

#define AX25_FLAG       0x7EU
#define AX25_UI_CTRL    0x03U
#define AX25_PID_NO_L3  0xF0U

/**
 * @brief Construye una trama AX.25 pura (solo bytes, sin bit-stuffing ni flags).
 * Útil si el módem o radio de hardware ya se encarga del HDLC/framing.
 *
 * @param info Puntero al texto/datos del payload.
 * @param infoLen Longitud del payload.
 * @param outBuf Búfer donde se guardará la trama (debe tener al menos infoLen + 30 bytes).
 * @return La cantidad de bytes de la trama generada.
 */
uint16_t AX25_BuildRawFrame(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf);

/**
 * @brief Construye una trama AX.25 para 1200 baudios usando NRZI (sin G3RUH).
 * Incluye flags (0x7E), bit-stuffing HDLC y codificación NRZI.
 */
uint16_t AX25_Build1200Frame_NRZI(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen);

/**
 * @brief Construye una trama AX.25 para 9600 baudios usando G3RUH y NRZ (sin NRZI).
 * Incluye flags (0x7E), bit-stuffing HDLC, aleatorizador G3RUH y formato NRZ.
 */
uint16_t AX25_Build9600Frame_G3RUH_NRZ(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen);

/**
 * @brief Compatibilidad hacia atrás (alias de AX25_Build1200Frame_NRZI).
 */
uint16_t AX25_BuildStuffedFrame(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen);

/**
 * @brief Compatibilidad hacia atrás (alias de AX25_Build9600Frame_G3RUH_NRZ).
 */
uint16_t AX25_BuildG3RUHFrame(const uint8_t *info, uint16_t infoLen, uint8_t *outBuf, uint16_t outMaxLen);

#endif /* TESTAX25_H */
