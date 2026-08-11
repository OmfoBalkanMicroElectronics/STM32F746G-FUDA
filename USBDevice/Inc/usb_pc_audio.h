#ifndef CLEANMP3PLAYER_USB_PC_AUDIO_H
#define CLEANMP3PLAYER_USB_PC_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void USBPCAudio_Init(void);
uint8_t USBPCAudio_IsConnected(void);
uint8_t USBPCAudio_IsConfigured(void);
void USBPCAudio_SendPlayPause(void);
void USBPCAudio_SendNext(void);
void USBPCAudio_SendPrevious(void);
void USBPCAudio_AudioHalfCallback(void);
void USBPCAudio_AudioCompleteCallback(void);
void USBPCAudio_FS_IRQHandler(void);
/* Run deferred WM8994/SAI work from a normal FreeRTOS task, never OTG_FS ISR. */
void USBPCAudio_Process(void);
void USBPCAudio_SetPlaybackEnabled(uint8_t enabled);
uint32_t USBPCAudio_GetRevision(void);

#ifdef __cplusplus
}
#endif

#endif
