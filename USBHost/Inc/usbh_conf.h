#ifndef CLEANMP3PLAYER_USBH_CONF_H
#define CLEANMP3PLAYER_USBH_CONF_H

#include "stm32f7xx.h"
#include "FreeRTOS.h"
#include <string.h>

#define USBH_MAX_NUM_ENDPOINTS          2U
#define USBH_MAX_NUM_INTERFACES         2U
#define USBH_MAX_NUM_CONFIGURATION      1U
#define USBH_MAX_NUM_SUPPORTED_CLASS    1U
#define USBH_KEEP_CFG_DESCRIPTOR        0U
#define USBH_MAX_SIZE_CONFIGURATION     0x200U
#define USBH_MAX_DATA_BUFFER            0x200U
#define USBH_DEBUG_LEVEL                0U

/* The application owns a dedicated CMSIS-RTOS2 task and explicitly calls
   USBH_Process(). Keeping the ST host core in standalone mode avoids mixing
   its legacy CMSIS-RTOS v1 worker thread with the rest of this project. */
#define USBH_USE_OS                     0U

#define USBH_malloc                     pvPortMalloc
#define USBH_free                       vPortFree
#define USBH_memset                     memset
#define USBH_memcpy                     memcpy

#define USBH_UsrLog(...)
#define USBH_ErrLog(...)
#define USBH_DbgLog(...)

#endif
