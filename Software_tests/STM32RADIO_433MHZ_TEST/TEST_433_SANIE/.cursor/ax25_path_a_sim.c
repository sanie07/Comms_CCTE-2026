/* Host simulation of STM Path-A TX + ESP Path-A RX parser.
 * Compiles the real AX25_SubGHz.c against a copy of the ESP decoder. */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../test1_433/Core/Inc/AX25_SubGHz.h"

#define LOG_PATH "/home/hernan/Desktop/Comms_CCTE-2026/Software_tests/TEST_433_SANIE/.cursor/debug-40d333.log"
#define SESSION "40d333"
#define SX1278_FSK_MAX_PAYLOAD 63U

static FILE *g_log;

static void dbg(const char *hid, const char *loc, const char *msg, const char *data_json)
{
    long long ts = (long long)time(NULL) * 1000LL;
    if (g_log == NULL) {
        return;
    }
    fprintf(g_log,
            "{\"sessionId\":\"%s\",\"runId\":\"post-fix\",\"hypothesisId\":\"%s\","
            "\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,\"timestamp\":%lld}\n",
            SESSION, hid, loc, msg, data_json, ts);
    fflush(g_log);
}

static uint8_t bitrev(uint8_t b)
{
    b = (uint8_t)((b & 0xF0u) >> 4 | (b & 0x0Fu) << 4);
    b = (uint8_t)((b & 0xCCu) >> 2 | (b & 0x33u) << 2);
    b = (uint8_t)((b & 0xAAu) >> 1 | (b & 0x55u) << 1);
    return b;
}

static uint16_t ax25_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1U) {
                crc = (uint16_t)((crc >> 1U) ^ 0x8408U);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }
    return (uint16_t)(~crc);
}

static int find_flag(const uint8_t *data, int len, int start)
{
    for (int i = start; i < len; i++) {
        if (data[i] == 0x7E) {
            return i;
        }
    }
    return -1;
}

typedef struct {
    int flag0;
    int start;
    int end;
    int frame_len;
    int too_short_after_flags;
    int frame_too_short;
    int crc_ok;
    char dest[16];
    char src[16];
    char info[80];
    char status[48];
} parse_result_t;

static void print_callsign_buf(const uint8_t *addr, char *out, size_t out_len)
{
    size_t n = 0;
    for (int i = 0; i < 6 && n + 1 < out_len; i++) {
        char c = (char)(addr[i] >> 1);
        if (c != ' ') {
            out[n++] = c;
        }
    }
    snprintf(out + n, out_len - n, "-%u", (unsigned)((addr[6] >> 1) & 0x0F));
}

static parse_result_t parse_ax25_ui(const uint8_t *data, int len)
{
    parse_result_t r;
    memset(&r, 0, sizeof(r));
    r.flag0 = -1;
    r.start = -1;
    r.end = -1;
    strcpy(r.status, "ok");

    r.start = 0;
    r.end = len;
    while (r.start < r.end && data[r.start] == 0x7E) {
        r.start++;
    }
    while (r.end > r.start && data[r.end - 1] == 0x7E) {
        r.end--;
    }
    r.flag0 = (len > 0 && data[0] == 0x7E) ? 0 : find_flag(data, len, 0);
    r.frame_len = r.end - r.start;
    if (r.frame_len < 16) {
        r.too_short_after_flags = (r.flag0 >= 0);
        r.frame_too_short = 1;
        strcpy(r.status, "frame_too_short");
        return r;
    }

    uint16_t got_fcs = (uint16_t)data[r.start + r.frame_len - 2] |
                       ((uint16_t)data[r.start + r.frame_len - 1] << 8);
    uint16_t exp_fcs = ax25_crc16(&data[r.start], (size_t)(r.frame_len - 2));
    r.crc_ok = (got_fcs == exp_fcs);
    print_callsign_buf(&data[r.start], r.dest, sizeof(r.dest));
    print_callsign_buf(&data[r.start + 7], r.src, sizeof(r.src));

    int info_len = r.frame_len - 18;
    if (info_len > 0) {
        int n = info_len;
        if (n >= (int)sizeof(r.info)) {
            n = (int)sizeof(r.info) - 1;
        }
        for (int i = 0; i < n; i++) {
            char c = (char)data[r.start + 16 + i];
            r.info[i] = isprint((unsigned char)c) ? c : '.';
        }
    }
    if (!r.crc_ok) {
        strcpy(r.status, "fcs_fail");
    }
    return r;
}

static void hex_to_json(const uint8_t *data, int len, char *out, size_t out_len)
{
    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < len && pos + 3 < out_len; i++) {
        int n = snprintf(out + pos, out_len - pos, "%02X", data[i]);
        if (n < 0) {
            break;
        }
        pos += (size_t)n;
    }
}

static int count_leading_7e(const uint8_t *data, int len)
{
    int n = 0;
    while (n < len && data[n] == 0x7E) {
        n++;
    }
    return n;
}

static int count_trailing_7e(const uint8_t *data, int len)
{
    int n = 0;
    while (n < len && data[len - 1 - n] == 0x7E) {
        n++;
    }
    return n;
}

static int count_reversed_vs_raw(const uint8_t *payload, int plen, const uint8_t *raw, int rlen)
{
    int match_plain = 0;
    int match_rev = 0;
    int n = plen < rlen ? plen : rlen;
    for (int i = 0; i < n; i++) {
        if (payload[i] == raw[i]) {
            match_plain++;
        }
        if (payload[i] == bitrev(raw[i])) {
            match_rev++;
        }
    }
    (void)match_plain;
    return match_rev;
}

int main(void)
{
    g_log = fopen(LOG_PATH, "a");
    if (g_log == NULL) {
        perror("fopen log");
        return 1;
    }

    AX25SG_Client_t client;
    if (AX25SG_Init(&client, "CUBE1 ", 0, 0) != 0) {
        dbg("H3", "ax25_path_a_sim.c:init", "AX25SG_Init failed", "{}");
        fclose(g_log);
        return 1;
    }

    uint8_t tx[AX25SG_MAX_FRAME_BUF];
    const char *info = "GPS:-25.263700,-57.575900,400.0,SAT:8";
    uint16_t tx_len = AX25SG_BuildUIFrame(&client, info, "FIUNA1", 1, tx, sizeof(tx));

    char hex[1024];
    hex_to_json(tx, (int)tx_len, hex, sizeof(hex));

    char data1[1400];
    snprintf(data1, sizeof(data1),
             "{\"tx_len\":%u,\"sx1278_limit\":%u,\"exceeds_limit\":%s,"
             "\"leading_7e\":%d,\"trailing_7e\":%d,\"first_byte\":\"%02X\","
             "\"hex\":\"%s\"}",
             (unsigned)tx_len, (unsigned)SX1278_FSK_MAX_PAYLOAD,
             tx_len > SX1278_FSK_MAX_PAYLOAD ? "true" : "false",
             count_leading_7e(tx, (int)tx_len),
             count_trailing_7e(tx, (int)tx_len),
             tx_len ? tx[0] : 0,
             hex);
    dbg("H2", "ax25_path_a_sim.c:build", "STM Path A payload (preambleLen=0)", data1);
    dbg("H3", "ax25_path_a_sim.c:build", "payload vs SX1278 63-byte cap", data1);

    /* Rebuild raw (unpacked) frame by temporarily using memcpy path:
     * reconstruct expected raw AX.25 via a second client is not available,
     * so invert bit-reversal of the non-flag body if H1 holds. */
    uint8_t raw_guess[256];
    int body_len = (int)tx_len - count_trailing_7e(tx, (int)tx_len);
    if (body_len < 0) {
        body_len = 0;
    }
    for (int i = 0; i < body_len && i < (int)sizeof(raw_guess); i++) {
        raw_guess[i] = bitrev(tx[i]);
    }

    int rev_count = 0;
    int ident_count = 0;
    for (int i = 0; i < body_len; i++) {
        if (tx[i] == bitrev(tx[i])) {
            ident_count++; /* palindromic bytes look identical either way */
        }
        if (tx[i] != raw_guess[i]) {
            rev_count++;
        }
    }
    char data_h1[256];
    snprintf(data_h1, sizeof(data_h1),
             "{\"body_len\":%d,\"bytes_changed_by_bitrev\":%d,"
             "\"palindromic_bytes\":%d,\"sample_tx0\":\"%02X\",\"sample_unrev0\":\"%02X\"}",
             body_len, rev_count, ident_count,
             body_len ? tx[0] : 0, body_len ? raw_guess[0] : 0);
    dbg("H1", "ax25_path_a_sim.c:bitrev", "LSB-first packing vs identity", data_h1);

    parse_result_t pr = parse_ax25_ui(tx, (int)tx_len);
    char data_parse[512];
    snprintf(data_parse, sizeof(data_parse),
             "{\"status\":\"%s\",\"flag0\":%d,\"start\":%d,\"end\":%d,"
             "\"frame_len\":%d,\"too_short_after_flags\":%d,\"crc_ok\":%s,"
             "\"dest\":\"%s\",\"src\":\"%s\",\"info\":\"%s\"}",
             pr.status, pr.flag0, pr.start, pr.end, pr.frame_len,
             pr.too_short_after_flags, pr.crc_ok ? "true" : "false",
             pr.dest, pr.src, pr.info);
    dbg("H2", "ax25_path_a_sim.c:parse_tx", "ESP parser on STM payload as-is", data_parse);
    dbg("H4", "ax25_path_a_sim.c:parse_tx", "FCS on STM payload as-is", data_parse);

    /* What parser would do if TX sent unreversed raw+flags */
    AX25SG_Client_t c2;
    AX25SG_Init(&c2, "CUBE1 ", 0, 1); /* 1 leading flag */
    uint8_t tx_flag[AX25SG_MAX_FRAME_BUF];
    uint16_t tx_flag_len = AX25SG_BuildUIFrame(&c2, info, "FIUNA1", 1, tx_flag, sizeof(tx_flag));
    parse_result_t pr_flag = parse_ax25_ui(tx_flag, (int)tx_flag_len);
    char hex2[1024];
    hex_to_json(tx_flag, (int)tx_flag_len, hex2, sizeof(hex2));
    char data_flag[1600];
    snprintf(data_flag, sizeof(data_flag),
             "{\"tx_len\":%u,\"leading_7e\":%d,\"status\":\"%s\",\"crc_ok\":%s,"
             "\"dest\":\"%s\",\"src\":\"%s\",\"info\":\"%s\",\"hex\":\"%s\"}",
             (unsigned)tx_flag_len, count_leading_7e(tx_flag, (int)tx_flag_len),
             pr_flag.status, pr_flag.crc_ok ? "true" : "false",
             pr_flag.dest, pr_flag.src, pr_flag.info, hex2);
    dbg("H1", "ax25_path_a_sim.c:parse_leading_flag", "parser with 1 leading flag still bit-reversed", data_flag);
    dbg("H4", "ax25_path_a_sim.c:parse_leading_flag", "FCS with leading flag, still reversed", data_flag);

    /* Unreverse Path A body and parse as ESP would if TX memcpy'd raw bytes */
    uint8_t unrev[256];
    int u = 0;
    unrev[u++] = 0x7E;
    for (int i = 0; i < body_len && u < (int)sizeof(unrev) - 2; i++) {
        unrev[u++] = bitrev(tx[i]);
    }
    unrev[u++] = 0x7E;
    parse_result_t pr_ok = parse_ax25_ui(unrev, u);
    char data_ok[512];
    snprintf(data_ok, sizeof(data_ok),
             "{\"status\":\"%s\",\"crc_ok\":%s,\"dest\":\"%s\",\"src\":\"%s\",\"info\":\"%s\",\"len\":%d}",
             pr_ok.status, pr_ok.crc_ok ? "true" : "false",
             pr_ok.dest, pr_ok.src, pr_ok.info, u);
    dbg("H1", "ax25_path_a_sim.c:unreverse", "parser on bit-unreversed raw AX.25", data_ok);
    dbg("H4", "ax25_path_a_sim.c:unreverse", "FCS on unreversed raw AX.25", data_ok);

    printf("tx_len=%u leading_7e=%d trailing_7e=%d first=%02X status=%s crc=%d dest=%s src=%s info=%s\n",
           (unsigned)tx_len, count_leading_7e(tx, (int)tx_len),
           count_trailing_7e(tx, (int)tx_len), tx_len ? tx[0] : 0,
           pr.status, pr.crc_ok, pr.dest, pr.src, pr.info);
    printf("with_leading_flag status=%s crc=%d dest=%s src=%s info=%s\n",
           pr_flag.status, pr_flag.crc_ok, pr_flag.dest, pr_flag.src, pr_flag.info);
    printf("unreversed status=%s crc=%d dest=%s src=%s info=%s\n",
           pr_ok.status, pr_ok.crc_ok, pr_ok.dest, pr_ok.src, pr_ok.info);

    (void)count_reversed_vs_raw;
    fclose(g_log);
    return 0;
}
