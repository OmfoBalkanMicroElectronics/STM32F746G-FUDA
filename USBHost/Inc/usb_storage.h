#ifndef CLEANMP3PLAYER_USB_STORAGE_H
#define CLEANMP3PLAYER_USB_STORAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    USB_STORAGE_DISCONNECTED = 0,
    USB_STORAGE_CONNECTED,
    USB_STORAGE_READY,
    USB_STORAGE_ERROR
} USBStorageState;

void USBStorage_Init(void);
void USBStorage_Task(void *argument);
void USBStorage_IRQHandler(void);
USBStorageState USBStorage_GetState(void);
uint8_t USBStorage_IsReady(void);
const char *USBStorage_GetPath(void);
uint32_t USBStorage_GetRevision(void);

#ifdef __cplusplus
}
#endif

#endif
