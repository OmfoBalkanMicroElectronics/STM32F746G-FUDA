#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    NETWORK_PING_IDLE = 0,
    NETWORK_PING_WAITING,
    NETWORK_PING_OK,
    NETWORK_PING_TIMEOUT,
    NETWORK_PING_ERROR
} NetworkPingState;

typedef enum
{
    NETWORK_SPEED_IDLE = 0,
    NETWORK_SPEED_DOWNLOADING,
    NETWORK_SPEED_UPLOADING,
    NETWORK_SPEED_DONE,
    NETWORK_SPEED_ERROR,
    NETWORK_SPEED_NO_LINK
} NetworkSpeedState;

typedef struct
{
    uint32_t revision;
    uint32_t pingMilliseconds;
    uint8_t linkUp;
    uint8_t hasAddress;
    uint8_t pingState;
    uint8_t linkMbps;
    uint8_t fullDuplex;
    uint8_t speedState;
    uint8_t limitWarning;
    uint32_t downCentiMbps;
    uint32_t upCentiMbps;
    char ipAddress[16];
    char diagnostic[40];
} NetworkSnapshot;

void NetworkManager_Init(void);
void NetworkManager_Task(void *argument);
void NetworkManager_GetSnapshot(NetworkSnapshot *snapshot);
void NetworkManager_StartSpeedTest(void);

#ifdef __cplusplus
}
#endif

#endif
