#ifndef CLEANMP3PLAYER_USBD_CONF_H
#define CLEANMP3PLAYER_USBD_CONF_H

#include "stm32f7xx.h"
#include "stm32f7xx_hal.h"
#include "FreeRTOS.h"
#include <string.h>

#define USBD_MAX_NUM_INTERFACES              3U
#define USBD_MAX_NUM_CONFIGURATION           1U
#define USBD_MAX_STR_DESC_SIZ                0x100U
#define USBD_DEBUG_LEVEL                     0U
#define USBD_SELF_POWERED                    1U
#define USBD_MAX_POWER                       0x32U
#define USBD_SUPPORT_USER_STRING_DESC        0U
#define USBD_LPM_ENABLED                     0U
#define USBD_CLASS_USER_STRING_DESC          0U
#define USBD_MAX_SUPPORTED_CLASS             2U

/* Must exactly match hidReportDesc[] in usb_pc_audio.c.  CubeF7's default
   CustomHID value is 163 bytes; advertising that for our 29-byte Consumer
   Control descriptor makes Windows fetch past the real report descriptor. */
#define USBD_CUSTOM_HID_REPORT_DESC_SIZE     29U
#define USBD_CUSTOMHID_OUTREPORT_BUF_SIZE    2U
#define USBD_CUSTOM_HID_FS_BINTERVAL         0x05U

/* Composite FS device: UAC1 speaker + HID Consumer Control. */
#define USE_USBD_COMPOSITE                   1U
#define USBD_CMPSIT_ACTIVATE_AUDIO           1U
#define USBD_CMPSIT_ACTIVATE_CUSTOMHID       1U

/* USB control/class callbacks run from OTG_FS IRQ in this integration.
   Do not enter the FreeRTOS heap from that IRQ.  The single Audio class
   allocates one USBD_AUDIO_HandleTypeDef, so provide one static arena. */
void *USBPCAudio_StaticMalloc(uint32_t size);
void USBPCAudio_StaticFree(void *ptr);
#define USBD_malloc                          USBPCAudio_StaticMalloc
#define USBD_free                            USBPCAudio_StaticFree
#define USBD_memset                          memset
#define USBD_memcpy                          memcpy
#define USBD_Delay                           HAL_Delay

#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)

#endif
