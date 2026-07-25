#ifndef APRS_CONFIG_H
#define APRS_CONFIG_H

/* Configuración de Callsigns para AX.25 */
#define APRS_MYCALL     "NOCALL"
#define APRS_MYSSID     11
#define APRS_DESTCALL   "APRS"
#define APRS_DESTSSID   0
#define APRS_PATH1CALL  "WIDE1"
#define APRS_PATH1SSID  1
#define APRS_PATH2CALL  "WIDE2"
#define APRS_PATH2SSID  1

#define AX25_MAX_INFO_LEN 256
#define AX25_HEADER_FLAGS 16
#define AX25_FOOTER_FLAGS 4

#endif /* APRS_CONFIG_H */
