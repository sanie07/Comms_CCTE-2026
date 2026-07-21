/**
 * @file    digipeater.c
 * @brief   APRS digipeater: WIDE1-1 simple + WIDE2-n traced path processing.
 *
 * Implements two standard APRS digipeater aliases:
 *
 *   WIDE1-1 (simple, direct-only)
 *     Path element "WIDE1" with SSID=1 and H-bit=0 is matched.
 *     Action: set H-bit on that element (mark as used). Frame bytes unchanged.
 *
 *   WIDE2-n (traced)
 *     Path element "WIDE2" with SSID=n (1<=n<=APRS_DIGI_WIDE2_MAX_N) matched.
 *     Action: insert own callsign (with H-bit) before the element, then
 *     decrement its SSID by 1. If new SSID == 0 also set H-bit on it.
 *
 * Duplicate filter: FNV-1a hash of (dest[7]+src[7]+info_field).
 *   10 hash slots, configurable time window (APRS_DIGI_DEDUPE_SECS).
 *
 * One static pending-TX slot: second frame arriving while TX is in progress
 * is silently dropped (single-channel radio; scheduler prevents collisions).
 *
 * All processing runs in ISR context (called from TIM2 via AX25_DigiCallback).
 * Only memcpy, simple arithmetic, and volatile flag writes are used.
 */

#include "digipeater.h"
#include "aprs_config.h"
#include "ax25.h"
#include "main.h"   /* HAL_GetTick */
#include <string.h>
#include <stdbool.h>

/* ================================================================
 * AX.25 address encoding helpers
 * Each callsign byte is (ASCII char) << 1.
 * SSID byte: bit7=H-bit, bit6=res(1), bit5=res(1), bits4-1=SSID, bit0=ext
 * ================================================================ */

/* Encoded "WIDE1" callsign bytes (left-shifted ASCII) */
static const uint8_t WIDE1_CALL[6] = {
    (uint8_t)('W' << 1), (uint8_t)('I' << 1), (uint8_t)('D' << 1),
    (uint8_t)('E' << 1), (uint8_t)('1' << 1), (uint8_t)(' ' << 1)
};

/* Encoded "WIDE2" callsign bytes */
static const uint8_t WIDE2_CALL[6] = {
    (uint8_t)('W' << 1), (uint8_t)('I' << 1), (uint8_t)('D' << 1),
    (uint8_t)('E' << 1), (uint8_t)('2' << 1), (uint8_t)(' ' << 1)
};

/* Encoded own callsign (6 call bytes + SSID byte), built in Digi_Init */
static uint8_t s_myCall[7];

/* ================================================================
 * Duplicate frame filter
 * ================================================================ */
#define DEDUPE_SLOTS  10U

static struct
{
    uint32_t hash;
    uint32_t expireTick;
} s_dedupe[DEDUPE_SLOTS];

static uint8_t s_dedupeIdx = 0U;

/**
 * FNV-1a 32-bit hash (fast, no division, suitable for ISR).
 * Hashes dest(7) + src(7) + info field to identify semantically
 * identical packets regardless of path changes.
 */
static uint32_t computeHash(const uint8_t *frame, uint16_t len)
{
    uint32_t h = 2166136261UL; /* FNV offset basis */

    /* Hash dest (7 bytes) + src (7 bytes) */
    uint16_t n = (len < 14U) ? len : 14U;
    for (uint16_t i = 0U; i < n; i++)
    {
        h ^= frame[i];
        h *= 16777619UL;
    }

    /* Find end of address field (first SSID byte with extension bit = 1) */
    uint16_t ssidByteIdx = 6U; /* SSID byte of dest */
    while (ssidByteIdx < len && (frame[ssidByteIdx] & 0x01U) == 0U)
        ssidByteIdx += 7U;

    /* Info field: ssidByteIdx+1 = ctrl, +2 = pid, +3 = first info byte */
    uint16_t infoStart = ssidByteIdx + 3U;
    for (uint16_t i = infoStart; i < len; i++)
    {
        h ^= frame[i];
        h *= 16777619UL;
    }

    return h;
}

static bool isDuplicate(uint32_t hash)
{
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0U; i < DEDUPE_SLOTS; i++)
    {
        if ((s_dedupe[i].hash == hash) && (now < s_dedupe[i].expireTick))
            return true;
    }
    return false;
}

static void storeDedupe(uint32_t hash)
{
    s_dedupe[s_dedupeIdx].hash       = hash;
    s_dedupe[s_dedupeIdx].expireTick = HAL_GetTick() +
                                       ((uint32_t)APRS_DIGI_DEDUPE_SECS * 1000UL);
    s_dedupeIdx = (uint8_t)((s_dedupeIdx + 1U) % DEDUPE_SLOTS);
}

/* ================================================================
 * Pending TX slot (one frame maximum)
 * ================================================================ */
/* Max frame: dest(7)+src(7)+path(7*2)+ctrl(1)+pid(1)+info(256)+inserted_call(7)
 * = 7+7+14+2+256+7 = 293. Round up to 300. */
#define DIGI_FRAME_BUF_SIZE  300U

static uint8_t          s_pendingFrame[DIGI_FRAME_BUF_SIZE];
static uint16_t         s_pendingLen  = 0U;
static volatile bool    s_pending     = false;

/* ================================================================
 * Public API
 * ================================================================ */

void Digi_Init(void)
{
    /* Build encoded own callsign from APRS_MYCALL + APRS_MYSSID */
    const char *call = APRS_MYCALL;
    uint8_t callLen  = (uint8_t)strlen(call);

    for (uint8_t i = 0U; i < 6U; i++)
    {
        char c = (i < callLen) ? call[i] : ' ';
        s_myCall[i] = (uint8_t)((uint8_t)c << 1U);
    }
    /* SSID byte: reserved bits 6,5 = 1, H-bit = 1 (we set it upon insertion),
     * SSID bits 4-1, extension bit 0 = 0 (not last address in chain).      */
    s_myCall[6] = (uint8_t)(0xE0U |                               /* res+H  */
                             (uint8_t)((APRS_MYSSID & 0x0FU) << 1U)); /* SSID */
    /* Extension bit stays 0 (will be ORed in only if we are the last addr). */

    memset(s_dedupe, 0, sizeof(s_dedupe));
    s_dedupeIdx = 0U;
    s_pending   = false;
}

bool Digi_IsPending(void)
{
    return s_pending;
}

bool Digi_GetFrame(uint8_t **buf, uint16_t *len)
{
    if (!s_pending)
        return false;
    *buf = s_pendingFrame;
    *len = s_pendingLen;
    return true;
}

void Digi_ClearPending(void)
{
    s_pending = false;
}

/* ================================================================
 * Digi_ProcessFrame — core digipeater logic
 * ================================================================ */
void Digi_ProcessFrame(const uint8_t *frame, uint16_t len)
{
    /* Minimum: dest(7)+src(7)+ctrl(1)+pid(1) = 16 */
    if (len < 16U)
        return;

    /* Drop if a frame is already waiting for TX */
    if (s_pending)
        return;

    uint32_t hash = computeHash(frame, len);

    if (isDuplicate(hash))
        return;

    /* ---- Locate end of address field ---- */
    /* Walk SSID bytes (every 7th byte starting at byte 6) until ext bit = 1 */
    uint16_t ssidByteIdx = 6U;
    while (ssidByteIdx < len && (frame[ssidByteIdx] & 0x01U) == 0U)
        ssidByteIdx += 7U;

    if (ssidByteIdx >= len)
        return; /* Malformed frame */

    /* ---- Check that a path exists ---- */
    /* If src SSID byte (byte 13) has ext=1, no path is present */
    if (ssidByteIdx <= 13U)
        return; /* No path elements -- nothing to digipeat */

    /* ---- Walk path elements to find first un-digipeated one ---- */
    /* Path starts at byte 14 */
    uint16_t elementIdx = 14U;
    bool     found      = false;
    uint8_t  aliasType  = 0U; /* 1 = WIDE1-1, 2 = WIDE2-n */
    uint8_t  aliasN     = 0U; /* N in WIDE2-N */
    (void)aliasN;              /* Suppress -Wunused-but-set-variable */

    while ((elementIdx + 6U) <= ssidByteIdx)
    {
        uint8_t elemSsid = frame[elementIdx + 6U];
        uint8_t hBit     = (elemSsid >> 7U) & 0x01U;

        if (hBit == 0U)
        {
            /* This element has not yet been digipeated */
            uint8_t ssidN = (elemSsid >> 1U) & 0x0FU;

#if APRS_DIGI_WIDE1_ENABLE
            if (!found &&
                (memcmp(&frame[elementIdx], WIDE1_CALL, 6U) == 0) &&
                (ssidN == 1U))
            {
                aliasType = 1U;
                aliasN    = 1U;
                found     = true;
            }
#endif

#if APRS_DIGI_WIDE2_ENABLE
            if (!found &&
                (memcmp(&frame[elementIdx], WIDE2_CALL, 6U) == 0) &&
                (ssidN >= 1U) && (ssidN <= (uint8_t)APRS_DIGI_WIDE2_MAX_N))
            {
                aliasType = 2U;
                aliasN    = ssidN;
                found     = true;
            }
#endif
            break; /* Only the first un-digipeated element matters */
        }

        elementIdx += 7U;
    }

    if (!found)
        return;

    /* ---- Build modified frame into pending buffer ---- */
    uint8_t  *out    = s_pendingFrame;
    uint16_t  outLen = 0U;

    if (aliasType == 1U)
    {
        /* WIDE1-1 simple: copy entire frame and set H-bit on the element */
        if (len > DIGI_FRAME_BUF_SIZE)
            return;

        memcpy(out, frame, len);
        out[elementIdx + 6U] |= 0x80U; /* Set H-bit */
        outLen = len;
    }
    else if (aliasType == 2U)
    {
        /* WIDE2-n traced:
         *   1. Copy bytes 0..(elementIdx-1) unchanged.
         *   2. Insert own callsign with H-bit.
         *   3. Copy bytes elementIdx..end unchanged.
         *   4. Decrement SSID of the WIDE2-n element; set H-bit if SSID→0.
         */
        if ((len + 7U) > DIGI_FRAME_BUF_SIZE)
            return;

        /* Part before the matched element */
        memcpy(out, frame, elementIdx);
        outLen = elementIdx;

        /* Insert own callsign.
         * Extension bit (bit 0) stays 0 -- another element follows. */
        out[outLen + 0U] = s_myCall[0];
        out[outLen + 1U] = s_myCall[1];
        out[outLen + 2U] = s_myCall[2];
        out[outLen + 3U] = s_myCall[3];
        out[outLen + 4U] = s_myCall[4];
        out[outLen + 5U] = s_myCall[5];
        out[outLen + 6U] = s_myCall[6] & (uint8_t)(~0x01U); /* ext = 0 */
        outLen += 7U;

        /* Rest of the original frame (WIDE2-n element and everything after) */
        memcpy(&out[outLen], &frame[elementIdx], len - elementIdx);
        outLen += len - elementIdx;

        /* Decrement SSID of the WIDE2-n element (now at elementIdx+7 in out).
         * SSID is in bits 4-1 of the SSID byte: subtract 2 from the byte. */
        uint16_t newElemSsidIdx = elementIdx + 6U + 7U; /* +7 for inserted call */
        out[newElemSsidIdx] -= 2U; /* Decrement SSID by 1 (stored <<1) */

        /* If SSID is now 0 (bits 4-1 all zero), set H-bit */
        if ((out[newElemSsidIdx] & 0x1EU) == 0U)
            out[newElemSsidIdx] |= 0x80U;
    }

    if (outLen == 0U)
        return;

    storeDedupe(hash);
    s_pendingLen = outLen;
    s_pending    = true; /* Signal App_Run that a frame is ready */
}
