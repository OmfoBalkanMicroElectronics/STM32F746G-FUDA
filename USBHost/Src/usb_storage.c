#include "usb_storage.h"
#include "cmsis_os2.h"
#include "ff_gen_drv.h"
#include "usbh_core.h"
#include "usbh_msc.h"
#include "usbh_diskio_dma.h"
#include "stm32f7xx_hal.h"

USBH_HandleTypeDef hUSBHost;
extern HCD_HandleTypeDef hhcd;

static volatile USBStorageState usbState = USB_STORAGE_DISCONNECTED;
static volatile uint32_t usbRevision;

/* Deliberately non-static: these are lightweight ST-LINK/HOTPLUG diagnostics
   for first-board bring-up and can be removed once USB MSC is proven stable. */
volatile uint32_t usbDiagState;
volatile uint32_t usbDiagRevision;
volatile uint32_t usbDiagHostState;
volatile uint32_t usbDiagLastUserEvent;
volatile uint32_t usbDiagReady;
volatile uint32_t usbDiagIrqCount;

static char usbPath[4] = {0};
static uint8_t driverLinked;
static uint8_t hostStarted;

static void USBStorage_UserProcess(USBH_HandleTypeDef *host, uint8_t id)
{
    (void)host;
    usbDiagLastUserEvent = id;
    switch (id)
    {
        case HOST_USER_CONNECTION:
            usbState = USB_STORAGE_CONNECTED;
            usbRevision++;
            break;

        case HOST_USER_CLASS_ACTIVE:
            /* MSC class activation means enumeration and SCSI inquiry completed.
               The task additionally checks USBH_MSC_IsReady before publishing READY. */
            usbState = USB_STORAGE_CONNECTED;
            usbRevision++;
            break;

        case HOST_USER_DISCONNECTION:
            usbState = USB_STORAGE_DISCONNECTED;
            usbRevision++;
            break;

        default:
            break;
    }
}

void USBStorage_Init(void)
{
    if (driverLinked == 0U)
    {
        /* MediaPlayer_Init links SD first, so USB becomes logical drive 1:. */
        if (FATFS_LinkDriver(&USBH_Driver, usbPath) != 0U)
        {
            usbState = USB_STORAGE_ERROR;
            usbRevision++;
            return;
        }
        driverLinked = 1U;
    }

    if (USBH_Init(&hUSBHost, USBStorage_UserProcess, 0U) != USBH_OK ||
        USBH_RegisterClass(&hUSBHost, USBH_MSC_CLASS) != USBH_OK ||
        USBH_Start(&hUSBHost) != USBH_OK)
    {
        usbState = USB_STORAGE_ERROR;
        usbRevision++;
        return;
    }

    hostStarted = 1U;
}

void USBStorage_Task(void *argument)
{
    (void)argument;
    for (;;)
    {
        if (hostStarted != 0U)
        {
            USBH_Process(&hUSBHost);
            usbDiagHostState = (uint32_t)hUSBHost.gState;
            if (usbState == USB_STORAGE_CONNECTED && USBH_MSC_IsReady(&hUSBHost) != 0U)
            {
                usbState = USB_STORAGE_READY;
                usbRevision++;
            }
        }
        usbDiagState = (uint32_t)usbState;
        usbDiagRevision = usbRevision;
        usbDiagReady = usbState == USB_STORAGE_READY ? 1U : 0U;
        osDelay(1U);
    }
}

void USBStorage_IRQHandler(void)
{
    usbDiagIrqCount++;
    if (hostStarted != 0U) HAL_HCD_IRQHandler(&hhcd);
}

USBStorageState USBStorage_GetState(void)
{
    return usbState;
}

uint8_t USBStorage_IsReady(void)
{
    return usbState == USB_STORAGE_READY ? 1U : 0U;
}

const char *USBStorage_GetPath(void)
{
    return usbPath;
}

uint32_t USBStorage_GetRevision(void)
{
    return usbRevision;
}
