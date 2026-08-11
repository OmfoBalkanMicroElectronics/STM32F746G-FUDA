#ifndef INTERNET_RADIO_H
#define INTERNET_RADIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    INTERNET_RADIO_STOPPED = 0,
    INTERNET_RADIO_WAIT_NETWORK,
    INTERNET_RADIO_RESOLVING,
    INTERNET_RADIO_CONNECTING,
    INTERNET_RADIO_HEADERS,
    INTERNET_RADIO_BUFFERING,
    INTERNET_RADIO_STREAMING,
    INTERNET_RADIO_ERROR
} InternetRadioState;

typedef enum
{
    INTERNET_RADIO_CODEC_MP3 = 0,
    INTERNET_RADIO_CODEC_AAC
} InternetRadioCodec;

typedef struct
{
    uint32_t revision;
    uint32_t bufferedBytes;
    uint32_t receivedBytes;
    uint32_t reconnectCount;
    uint8_t state;
    uint8_t stationIndex;
    uint8_t stationCount;
    uint8_t codec;
    char stationName[40];
    char status[64];
} InternetRadioSnapshot;

void InternetRadio_Init(void);
void InternetRadio_Start(void);
void InternetRadio_Stop(void);
void InternetRadio_Select(uint8_t index);
void InternetRadio_Next(void);
void InternetRadio_Previous(void);
void InternetRadio_Service(uint8_t linkUp, uint8_t hasAddress);
uint32_t InternetRadio_Available(void);
uint32_t InternetRadio_Read(uint8_t *destination, uint32_t capacity);
void InternetRadio_GetSnapshot(InternetRadioSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
