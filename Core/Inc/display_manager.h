#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void DisplayManager_Init(void);
void DisplayManager_Tick(void);
void DisplayManager_SetBrightness(uint8_t value);
uint8_t DisplayManager_GetBrightness(void);
void DisplayManager_SetTimeout(uint16_t seconds);
uint16_t DisplayManager_GetTimeout(void);
uint8_t DisplayManager_NotifyTouch(void);
uint8_t DisplayManager_IsSleeping(void);

#ifdef __cplusplus
}
#endif

#endif
