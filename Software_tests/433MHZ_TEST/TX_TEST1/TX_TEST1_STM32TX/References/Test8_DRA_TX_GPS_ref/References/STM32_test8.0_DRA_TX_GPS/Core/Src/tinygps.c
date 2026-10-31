#include "tinygps.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define GPS_SENTENCE_GPRMC 1
#define GPS_SENTENCE_GPGGA 2
#define GPS_SENTENCE_GPGLL 3
#define GPS_SENTENCE_OTHER 0

void TinyGPSPlus_Init(TinyGPSPlus *gps)
{
    memset(gps, 0, sizeof(TinyGPSPlus));
}

static int fromHex(char a)
{
    if (a >= 'A' && a <= 'F') return a - 'A' + 10;
    if (a >= 'a' && a <= 'f') return a - 'a' + 10;
    return a - '0';
}

static void parseDegrees(const char *term, RawDegrees *deg)
{
    deg->deg = 181;
    if (!isdigit((unsigned char)*term) && *term != '.') return;
    uint32_t leftOfDecimal = (uint32_t)atol(term);
    while (isdigit((unsigned char)*term)) ++term;
    if (*term != '.') return;
    deg->deg = (uint16_t)(leftOfDecimal / 100);
    uint16_t minutes = (uint16_t)(leftOfDecimal % 100);
    uint32_t multiplier = 10000000UL;
    uint32_t tenMillionthsOfMinutes = minutes * multiplier;
    while (isdigit((unsigned char)*++term))
    {
        multiplier /= 10;
        tenMillionthsOfMinutes += (*term - '0') * multiplier;
    }
    deg->billionths = (5 * tenMillionthsOfMinutes + 1) / 3;
    deg->negative = false;
}

static bool endOfTermHandler(TinyGPSPlus *gps)
{
    if (gps->isChecksumTerm)
    {
        uint8_t checksum = 16 * fromHex(gps->term[0]) + fromHex(gps->term[1]);
        if (checksum == gps->parity)
        {
            gps->passedChecksumCount++;
            if (gps->sentenceHasFix)
            {
                if (gps->curSentenceType == GPS_SENTENCE_GPRMC || gps->curSentenceType == GPS_SENTENCE_GPGGA)
                {
                    gps->location.rawLatData = gps->location.rawNewLatData;
                    gps->location.rawLngData = gps->location.rawNewLngData;
                    gps->location.valid = (gps->location.rawNewLatData.deg <= 90 && gps->location.rawNewLngData.deg <= 180);
                    gps->location.updated = true;
                }
            }
            return true;
        }
        gps->failedChecksumCount++;
        return false;
    }

    if (gps->curTermNumber == 0)
    {
        gps->curSentenceType = GPS_SENTENCE_OTHER;
        if (strlen(gps->term) >= 5 && gps->term[0] == 'G')
        {
            if (strcmp(&gps->term[2], "RMC") == 0) gps->curSentenceType = GPS_SENTENCE_GPRMC;
            else if (strcmp(&gps->term[2], "GGA") == 0) gps->curSentenceType = GPS_SENTENCE_GPGGA;
            else if (strcmp(&gps->term[2], "GLL") == 0) gps->curSentenceType = GPS_SENTENCE_GPGLL;
        }
        return false;
    }

    if (gps->curSentenceType != GPS_SENTENCE_OTHER && gps->term[0])
    {
        uint32_t combine = ((uint32_t)gps->curSentenceType << 5) | gps->curTermNumber;
        switch (combine)
        {
            case (GPS_SENTENCE_GPRMC << 5) | 2:
            case (GPS_SENTENCE_GPGLL << 5) | 6:
                gps->sentenceHasFix = (gps->term[0] == 'A');
                break;
            case (GPS_SENTENCE_GPGGA << 5) | 6:
                gps->sentenceHasFix = (gps->term[0] > '0');
                break;
            case (GPS_SENTENCE_GPRMC << 5) | 3:
            case (GPS_SENTENCE_GPGGA << 5) | 2:
            case (GPS_SENTENCE_GPGLL << 5) | 1:
                parseDegrees(gps->term, &gps->location.rawNewLatData);
                break;
            case (GPS_SENTENCE_GPRMC << 5) | 4:
            case (GPS_SENTENCE_GPGGA << 5) | 3:
            case (GPS_SENTENCE_GPGLL << 5) | 2:
                gps->location.rawNewLatData.negative = (gps->term[0] == 'S');
                break;
            case (GPS_SENTENCE_GPRMC << 5) | 5:
            case (GPS_SENTENCE_GPGGA << 5) | 4:
            case (GPS_SENTENCE_GPGLL << 5) | 3:
                parseDegrees(gps->term, &gps->location.rawNewLngData);
                break;
            case (GPS_SENTENCE_GPRMC << 5) | 6:
            case (GPS_SENTENCE_GPGGA << 5) | 5:
            case (GPS_SENTENCE_GPGLL << 5) | 4:
                gps->location.rawNewLngData.negative = (gps->term[0] == 'W');
                break;
            default:
                break;
        }
    }
    return false;
}

bool TinyGPSPlus_Encode(TinyGPSPlus *gps, char c)
{
    gps->encodedCharCount++;
    switch (c)
    {
        case ',':
            gps->parity ^= (uint8_t)c;
            /* fall through */
        case '\r':
        case '\n':
        case '*':
        {
            bool isValidSentence = false;
            if (gps->curTermOffset < sizeof(gps->term))
            {
                gps->term[gps->curTermOffset] = 0;
                isValidSentence = endOfTermHandler(gps);
            }
            gps->curTermNumber++;
            gps->curTermOffset = 0;
            gps->isChecksumTerm = (c == '*');
            return isValidSentence;
        }
        case '$':
            gps->curTermNumber = gps->curTermOffset = 0;
            gps->parity = 0;
            gps->curSentenceType = GPS_SENTENCE_OTHER;
            gps->isChecksumTerm = false;
            gps->sentenceHasFix = false;
            break;
        default:
            if (gps->curTermOffset < sizeof(gps->term) - 1)
            {
                gps->term[gps->curTermOffset++] = c;
            }
            if (!gps->isChecksumTerm) gps->parity ^= c;
            break;
    }
    return false;
}

double TinyGPSLocation_Lat(TinyGPSLocation *loc)
{
    loc->updated = false;
    double ret = (double)loc->rawLatData.deg + ((double)loc->rawLatData.billionths / 1000000000.0);
    return loc->rawLatData.negative ? -ret : ret;
}

double TinyGPSLocation_Lng(TinyGPSLocation *loc)
{
    loc->updated = false;
    double ret = (double)loc->rawLngData.deg + ((double)loc->rawLngData.billionths / 1000000000.0);
    return loc->rawLngData.negative ? -ret : ret;
}
