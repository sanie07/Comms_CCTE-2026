#ifndef TINYGPS_C_H
#define TINYGPS_C_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _GPS_MAX_FIELD_SIZE 15

typedef struct {
    uint16_t deg;
    uint32_t billionths;
    bool negative;
} RawDegrees;

typedef struct {
    bool valid;
    bool updated;
    RawDegrees rawLatData;
    RawDegrees rawLngData;
    RawDegrees rawNewLatData;
    RawDegrees rawNewLngData;
} TinyGPSLocation;

typedef struct {
    uint8_t parity;
    bool isChecksumTerm;
    char term[_GPS_MAX_FIELD_SIZE];
    uint8_t curSentenceType;
    uint8_t curTermNumber;
    uint8_t curTermOffset;
    bool sentenceHasFix;

    uint32_t encodedCharCount;
    uint32_t passedChecksumCount;
    uint32_t failedChecksumCount;

    TinyGPSLocation location;
} TinyGPSPlus;

void TinyGPSPlus_Init(TinyGPSPlus *gps);
bool TinyGPSPlus_Encode(TinyGPSPlus *gps, char c);
double TinyGPSLocation_Lat(TinyGPSLocation *loc);
double TinyGPSLocation_Lng(TinyGPSLocation *loc);

#ifdef __cplusplus
}
#endif

#endif /* TINYGPS_C_H */
