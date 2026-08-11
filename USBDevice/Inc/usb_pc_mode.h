#ifndef CLEANMP3PLAYER_USB_PC_MODE_H
#define CLEANMP3PLAYER_USB_PC_MODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void USBPC_Init(void);
uint8_t USBPC_IsConnected(void);
uint8_t USBPC_IsConfigured(void);
uint32_t USBPC_GetRevision(void);
void USBPC_SendPlayPause(void);
void USBPC_SendNext(void);
void USBPC_SendPrevious(void);
void USBPC_AudioSync(uint8_t offset);

#ifdef __cplusplus
}
#endif

#endif
