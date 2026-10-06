/**
 * @file AX25_SubGHz.h
 * @brief Protocolo AX.25 adaptado para el middleware SubGHz_Phy de STM32WLE5.
 *
 * Portado desde la librería RadioLib (C++) a C puro para ser compatible con
 * el middleware SubGHz_Phy de STMicroelectronics. En lugar de usar la
 * PhysicalLayer de RadioLib, la transmisión final se delega a Radio.Send()
 * del middleware SubGHz.
 *
 * Características:
 *  - Codificación de cabeceras AX.25 (callsign, SSID, rutas)
 *  - Bit-stuffing HDLC
 *  - Codificación NRZI (para 1200 bps AFSK / FSK)
 *  - Scrambler G3RUH (para 9600 bps FSK)
 *  - Sin memoria dinámica (heap): todos los buffers son estáticos
 *  - Sin dependencias de C++ ni de RadioLib
 */

#ifndef AX25_SUBGHZ_H
#define AX25_SUBGHZ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * Constantes del protocolo AX.25
 * ========================================================================= */

/** Máxima longitud del callsign (sin null-terminator) */
#define AX25SG_MAX_CALLSIGN_LEN         6U

/** Máximo número de repetidores en la ruta */
#define AX25SG_MAX_REPEATERS            8U

/** Máxima longitud del campo Info */
#define AX25SG_MAX_INFO_LEN             256U

/** Tamaño máximo del buffer de salida para una trama completa con flags */
#define AX25SG_MAX_FRAME_BUF            512U

/* ── Byte de flag HDLC ──────────────────────────────────────────────────── */
#define AX25SG_FLAG                     0x7EU  /**< Flag de inicio/fin de trama */

/* ── Flags de SSID (campo de dirección, byte 7 de cada dirección) ────────── */
#define AX25SG_SSID_COMMAND_DEST        0x80U  /**< Trama tipo comando  (en destino) */
#define AX25SG_SSID_COMMAND_SRC         0x00U  /**< Trama tipo comando  (en origen)  */
#define AX25SG_SSID_RESPONSE_DEST       0x00U  /**< Trama tipo respuesta (en destino)*/
#define AX25SG_SSID_RESPONSE_SRC        0x80U  /**< Trama tipo respuesta (en origen) */
#define AX25SG_SSID_NOT_REPEATED        0x00U  /**< Repetidor: no ha retransmitido   */
#define AX25SG_SSID_HAS_BEEN_REPEATED   0x80U  /**< Repetidor: ya retransmitió       */
#define AX25SG_SSID_RESERVED_BITS       0x60U  /**< Bits reservados (siempre 1)      */
#define AX25SG_SSID_HDLC_CONT           0x00U  /**< Extensión HDLC: continúa         */
#define AX25SG_SSID_HDLC_END            0x01U  /**< Extensión HDLC: fin de dirección */

/* ── Campo Control ──────────────────────────────────────────────────────── */
#define AX25SG_CTRL_U_SABM              0x6CU  /**< Set Async Balanced Mode          */
#define AX25SG_CTRL_U_SABME             0x2CU  /**< SABM Extended                    */
#define AX25SG_CTRL_U_DISC              0x40U  /**< Disconnect                       */
#define AX25SG_CTRL_U_DM               0x0CU  /**< Disconnected Mode                */
#define AX25SG_CTRL_U_UA               0x60U  /**< Unnumbered Ack                   */
#define AX25SG_CTRL_U_FRMR             0x84U  /**< Frame Reject                     */
#define AX25SG_CTRL_U_UI               0x00U  /**< Unnumbered Information (APRS)    */
#define AX25SG_CTRL_U_XID              0xACU  /**< Exchange ID                      */
#define AX25SG_CTRL_U_TEST             0xE0U  /**< Test                             */
#define AX25SG_CTRL_PF_ON              0x10U  /**< Poll/Final bit activado          */
#define AX25SG_CTRL_PF_OFF             0x00U  /**< Poll/Final bit desactivado       */
#define AX25SG_CTRL_S_RR               0x00U  /**< Receive Ready                    */
#define AX25SG_CTRL_S_RNR              0x04U  /**< Receive Not Ready                */
#define AX25SG_CTRL_S_REJ              0x08U  /**< Reject                           */
#define AX25SG_CTRL_S_SREJ             0x0CU  /**< Selective Reject                 */
#define AX25SG_CTRL_IFRAME             0x00U  /**< Information frame                */
#define AX25SG_CTRL_SFRAME             0x01U  /**< Supervisory frame                */
#define AX25SG_CTRL_UFRAME             0x03U  /**< Unnumbered frame                 */

/* ── PID (Protocol Identifier) ─────────────────────────────────────────── */
#define AX25SG_PID_ISO8208              0x01U
#define AX25SG_PID_TCP_COMPRESSED       0x06U
#define AX25SG_PID_TCP_UNCOMPRESSED     0x07U
#define AX25SG_PID_SEGMENTATION         0x08U
#define AX25SG_PID_TEXNET               0xC3U
#define AX25SG_PID_LQ_PROTOCOL         0xC4U
#define AX25SG_PID_APPLETALK            0xCAU
#define AX25SG_PID_APPLETALK_ARP        0xCBU
#define AX25SG_PID_ARPA_IP              0xCCU
#define AX25SG_PID_ARPA_ARP             0xCDU
#define AX25SG_PID_FLEXNET              0xCEU
#define AX25SG_PID_NET_ROM              0xCFU
#define AX25SG_PID_NO_L3                0xF0U  /**< Sin capa 3 (APRS)               */
#define AX25SG_PID_ESCAPE               0xFFU

/* ── Polinomio G3RUH para scrambler de 9600 bps ────────────────────────── */
#define AX25SG_SCRAMBLER_G3RUH_POLY     0x00010012UL /**< x^17 + x^12 + 1  */
#define AX25SG_SCRAMBLER_G3RUH_INIT     0x00000000UL /**< Valor inicial = 0 */

/* =========================================================================
 * Estructuras de datos
 * ========================================================================= */

/**
 * @brief Estructura que representa una trama AX.25.
 */
typedef struct {
    char     destCallsign[AX25SG_MAX_CALLSIGN_LEN + 1]; /**< Callsign destino (null-terminated) */
    uint8_t  destSSID;                                   /**< SSID destino (0-15)               */

    char     srcCallsign[AX25SG_MAX_CALLSIGN_LEN + 1];  /**< Callsign origen (null-terminated)  */
    uint8_t  srcSSID;                                    /**< SSID origen (0-15)                */

    char     repeaterCallsigns[AX25SG_MAX_REPEATERS][AX25SG_MAX_CALLSIGN_LEN + 1]; /**< Callsigns de repetidores */
    uint8_t  repeaterSSIDs[AX25SG_MAX_REPEATERS];       /**< SSIDs de repetidores               */
    uint8_t  numRepeaters;                               /**< Número de repetidores (0-8)        */

    uint8_t  control;                                    /**< Campo de control                   */
    uint8_t  protocolID;                                 /**< PID (0 si no aplica)               */

    uint8_t  info[AX25SG_MAX_INFO_LEN];                  /**< Datos del campo Info               */
    uint16_t infoLen;                                    /**< Longitud del campo Info            */

    uint8_t  rcvSeqNumber;                               /**< Número de secuencia de recepción   */
    uint8_t  sendSeqNumber;                              /**< Número de secuencia de envío       */
} AX25SG_Frame_t;

/**
 * @brief Configuración del cliente AX.25.
 */
typedef struct {
    char     srcCallsign[AX25SG_MAX_CALLSIGN_LEN + 1];  /**< Callsign de esta estación          */
    uint8_t  srcSSID;                                    /**< SSID de esta estación              */
    uint8_t  preambleLen;                                /**< Cantidad de bytes de preámbulo (flags 0x7E) */
    uint32_t scramblerPoly;                              /**< Polinomio scrambler (0 = desactivado / NRZI) */
    uint32_t scramblerInit;                              /**< Valor inicial del scrambler                 */
    bool     invertPolarity;                             /**< Invertir polaridad de bits (0 <-> 1)        */
} AX25SG_Client_t;

/* =========================================================================
 * API Pública — Inicialización
 * ========================================================================= */

/**
 * @brief Inicializa el cliente AX.25.
 *
 * @param client     Puntero al cliente a inicializar.
 * @param callsign   Callsign de origen (máx. 6 caracteres).
 * @param ssid       SSID de origen (0-15).
 * @param preamble   Cantidad de bytes 0x7E de preámbulo (recomendado: 16-32).
 * @return  0 si OK, -1 si callsign inválido.
 */
int8_t AX25SG_Init(AX25SG_Client_t *client,
                   const char *callsign, uint8_t ssid,
                   uint8_t preamble);

/**
 * @brief Configura el scrambler G3RUH (para 9600 bps o Direct FSK G3RUH).
 *
 * Para 1200 bps estándar (NRZI), no llamar o pasar poly = 0.
 *
 * @param client  Puntero al cliente.
 * @param poly    Polinomio del scrambler (0 para desactivar).
 * @param init    Valor inicial del scrambler.
 */
void AX25SG_SetScrambler(AX25SG_Client_t *client, uint32_t poly, uint32_t init);

/**
 * @brief Configura la inversión de polaridad de los bits FSK.
 *
 * @param client  Puntero al cliente.
 * @param invert  true para invertir bits (0 <-> 1), false para normal.
 */
void AX25SG_SetInvert(AX25SG_Client_t *client, bool invert);

/* =========================================================================
 * API Pública — Construcción de tramas
 * ========================================================================= */

/**
 * @brief Inicializa una trama AX.25 vacía con los campos mínimos.
 */
void AX25SG_FrameInit(AX25SG_Frame_t *frame,
                      const char *destCallsign, uint8_t destSSID,
                      const char *srcCallsign,  uint8_t srcSSID,
                      uint8_t control);

/**
 * @brief Inicializa una trama UI (Unnumbered Information) con payload de texto.
 */
int8_t AX25SG_FrameInitUI(AX25SG_Frame_t *frame,
                           const char *destCallsign, uint8_t destSSID,
                           const char *srcCallsign,  uint8_t srcSSID,
                           const char *info);

/**
 * @brief Añade repetidores (path) a una trama ya inicializada.
 */
int8_t AX25SG_SetRepeaters(AX25SG_Frame_t *frame,
                            const char callsigns[][AX25SG_MAX_CALLSIGN_LEN + 1],
                            const uint8_t *ssids,
                            uint8_t count);

/* =========================================================================
 * API Pública — Serialización
 * ========================================================================= */

/**
 * @brief Serializa una trama AX.25 al buffer de salida listo para Radio.Send().
 *
 * Aplica: direccionamiento AX.25, CRC-CCITT, bit-stuffing HDLC,
 * NRZI (1200 bps) o scrambler G3RUH (9600 bps) según configuración.
 *
 * @param client    Puntero al cliente configurado (callsign, preamble, scrambler).
 * @param frame     Puntero a la trama a enviar.
 * @param outBuf    Buffer de salida (debe ser al menos AX25SG_MAX_FRAME_BUF bytes).
 * @param outMaxLen Tamaño máximo del buffer de salida.
 * @return  Cantidad de bytes escritos en outBuf, 0 si error u overflow.
 */
uint16_t AX25SG_BuildFrame(const AX25SG_Client_t *client,
                           const AX25SG_Frame_t  *frame,
                           uint8_t *outBuf, uint16_t outMaxLen);

/**
 * @brief Atajo para construir y serializar un string de texto como trama UI (APRS/AX.25).
 *
 * @param client        Puntero al cliente configurado.
 * @param str           Cadena de texto a transmitir.
 * @param destCallsign  Callsign destino (p.ej. "APRS", "CQ").
 * @param destSSID      SSID destino (0-15).
 * @param outBuf        Buffer de salida.
 * @param outMaxLen     Tamaño del buffer de salida.
 * @return  Cantidad de bytes escritos en outBuf, 0 si error.
 */
uint16_t AX25SG_BuildUIFrame(const AX25SG_Client_t *client,
                             const char *str,
                             const char *destCallsign, uint8_t destSSID,
                             uint8_t *outBuf, uint16_t outMaxLen);

#endif /* AX25_SUBGHZ_H */
