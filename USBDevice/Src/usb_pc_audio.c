#include "usb_pc_audio.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_audio.h"
#include "usbd_customhid.h"
#include "usbd_composite_builder.h"
#include "stm32746g_discovery_audio.h"

USBD_HandleTypeDef hUsbDeviceFS;
extern PCD_HandleTypeDef hpcd_fs;

static volatile uint8_t pcConnected;
static volatile uint8_t pcConfigured;
static volatile uint32_t pcRevision;
static uint8_t audioClassId;
static uint8_t hidClassId;

/* Composite classes allocate from OTG_FS IRQ context, so never use the
   FreeRTOS heap here.  Keep dedicated static arenas for Audio and HID. */
static uint32_t usbAudioStaticArena[16000U / sizeof(uint32_t)];
static uint32_t usbHidStaticArena[512U / sizeof(uint32_t)];
static volatile uint8_t usbAudioStaticUsed;
static volatile uint8_t usbHidStaticUsed;

void *USBPCAudio_StaticMalloc(uint32_t size)
{
    if (size <= sizeof(usbHidStaticArena) && usbHidStaticUsed == 0U)
    {
        usbHidStaticUsed = 1U;
        return (void *)usbHidStaticArena;
    }
    if (size <= sizeof(usbAudioStaticArena) && usbAudioStaticUsed == 0U)
    {
        usbAudioStaticUsed = 1U;
        return (void *)usbAudioStaticArena;
    }
    return NULL;
}
void USBPCAudio_StaticFree(void *ptr)
{
    if (ptr == (void *)usbAudioStaticArena) usbAudioStaticUsed = 0U;
    else if (ptr == (void *)usbHidStaticArena) usbHidStaticUsed = 0U;
}

/* USB class callbacks execute from OTG_FS IRQ context.  Never call the BSP
   audio/I2C path there: the project protects I2C with FreeRTOS primitives.
   Defer all codec/SAI operations to the media task via USBPCAudio_Process(). */
static volatile uint32_t pendingAudioInit;
static volatile uint32_t pendingAudioDeInit;
static volatile uint32_t pendingAudioCmd;
static volatile uint32_t pendingAudioSize;
static volatile uint8_t *pendingAudioBuffer;
static volatile uint32_t pendingAudioFreq;
static volatile uint32_t pendingAudioVolume;
static volatile uint32_t pendingVolumeValid;
static volatile uint8_t pendingVolume;
static volatile uint32_t pendingMuteValid;
static volatile uint8_t pendingMute;
static volatile uint8_t playbackEnabled;
static uint8_t audioHardwareActive;

volatile uint32_t usbPcDiagConnected;
volatile uint32_t usbPcDiagConfigured;
volatile uint32_t usbPcDiagRevision;
volatile uint32_t usbPcDiagIrqCount;

static int8_t AudioInit(uint32_t freq, uint32_t volume, uint32_t options)
{
    (void)options;
    pendingAudioFreq = freq;
    pendingAudioVolume = volume;
    pendingAudioInit = 1U;
    /* SET_CONFIGURATION has now reached and initialized the Audio class. */
    if (pcConfigured == 0U)
    {
        pcConfigured = 1U;
        usbPcDiagConfigured = 1U;
        pcRevision++;
    }
    return 0;
}
static int8_t AudioDeInit(uint32_t options)
{
    (void)options;
    pendingAudioDeInit = 1U;
    return 0;
}
static int8_t AudioCmd(uint8_t *buf, uint32_t size, uint8_t cmd)
{
    pendingAudioBuffer = buf;
    pendingAudioSize = size;
    pendingAudioCmd = (uint32_t)cmd + 1U;
    return 0;
}
static int8_t AudioVolume(uint8_t vol) { pendingVolume = vol; pendingVolumeValid = 1U; return 0; }
static int8_t AudioMute(uint8_t cmd) { pendingMute = cmd; pendingMuteValid = 1U; return 0; }
static int8_t AudioPeriodic(uint8_t *buf, uint32_t size, uint8_t cmd) { (void)buf; (void)size; (void)cmd; return 0; }
static int8_t AudioState(void) { return 0; }
static USBD_AUDIO_ItfTypeDef audioFops = { AudioInit, AudioDeInit, AudioCmd, AudioVolume, AudioMute, AudioPeriodic, AudioState };

/* Consumer Control: Play/Pause, Scan Next Track, Scan Previous Track. */
static uint8_t hidReportDesc[] = {
    0x05,0x0C, 0x09,0x01, 0xA1,0x01,
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x03,
    0x09,0xCD, 0x09,0xB5, 0x09,0xB6, 0x81,0x02,
    0x75,0x05, 0x95,0x01, 0x81,0x03,
    0xC0
};
static int8_t HidInit(void) { return 0; }
static int8_t HidDeInit(void) { return 0; }
static int8_t HidOut(uint8_t event, uint8_t state) { (void)event; (void)state; return 0; }
static USBD_CUSTOM_HID_ItfTypeDef hidFops = { hidReportDesc, HidInit, HidDeInit, HidOut };

static void sendConsumer(uint8_t mask)
{
    uint8_t report = mask;
    uint8_t release = 0U;
    if (pcConfigured == 0U || hidClassId == 0xFFU) return;
    (void)USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, &report, 1U, hidClassId);
    HAL_Delay(2U);
    (void)USBD_CUSTOM_HID_SendReport(&hUsbDeviceFS, &release, 1U, hidClassId);
}

void USBPCAudio_Init(void)
{
    static uint8_t audioEp[1] = { 0x01U };
    static uint8_t hidEp[2] = { 0x82U, 0x02U };
    if (USBD_Init(&hUsbDeviceFS, &AUDIO_Desc, 0U) != USBD_OK) return;

    /* Correct CubeF7 composite registration path.  Audio keeps OUT EP1;
       Consumer HID uses IN EP2 / OUT EP2, so endpoint numbers do not collide. */
    if (USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_AUDIO, CLASS_TYPE_AUDIO,
                                    audioEp) != USBD_OK) return;
    audioClassId = (uint8_t)USBD_CMPSIT_GetClassID(&hUsbDeviceFS, CLASS_TYPE_AUDIO, 0U);
    if (audioClassId == 0xFFU) return;
    hUsbDeviceFS.classId = audioClassId;
    if (USBD_AUDIO_RegisterInterface(&hUsbDeviceFS, &audioFops) != USBD_OK) return;

    if (USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_CUSTOM_HID, CLASS_TYPE_CHID,
                                    hidEp) != USBD_OK) return;
    hidClassId = (uint8_t)USBD_CMPSIT_GetClassID(&hUsbDeviceFS, CLASS_TYPE_CHID, 0U);
    if (hidClassId == 0xFFU) return;
    hUsbDeviceFS.classId = hidClassId;
    if (USBD_CUSTOM_HID_RegisterInterface(&hUsbDeviceFS, &hidFops) != USBD_OK) return;
    hUsbDeviceFS.classId = audioClassId;

    if (USBD_Start(&hUsbDeviceFS) == USBD_OK)
    {
        pcConnected = 1U;
        pcRevision++;
    }
}

uint8_t USBPCAudio_IsConnected(void)
{
    pcConfigured = hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED ? 1U : 0U;
    usbPcDiagConnected = pcConnected;
    usbPcDiagConfigured = pcConfigured;
    usbPcDiagRevision = pcRevision;
    return pcConnected;
}
uint8_t USBPCAudio_IsConfigured(void) { (void)USBPCAudio_IsConnected(); return pcConfigured; }
uint32_t USBPCAudio_GetRevision(void) { (void)USBPCAudio_IsConnected(); return pcRevision; }
void USBPCAudio_SendPlayPause(void) { sendConsumer(0x01U); }
void USBPCAudio_SendNext(void) { sendConsumer(0x02U); }
void USBPCAudio_SendPrevious(void) { sendConsumer(0x04U); }
void USBPCAudio_SetPlaybackEnabled(uint8_t enabled)
{
    uint8_t wasEnabled = playbackEnabled;
    playbackEnabled = enabled ? 1U : 0U;

    /* UAC1 Init/START normally happen only once at SET_CONFIGURATION.  If the
       user leaves PC mode while the cable remains attached, Windows keeps the
       stream configured and will not issue another START when PC mode is
       selected again.  Re-arm the saved format/ring-buffer state ourselves. */
    if (playbackEnabled != 0U && wasEnabled == 0U && pcConfigured != 0U)
    {
        if (pendingAudioFreq == 0U) pendingAudioFreq = USBD_AUDIO_FREQ;
        pendingAudioInit = 1U;
        if (pendingAudioBuffer != NULL && pendingAudioSize != 0U)
        {
            pendingAudioCmd = (uint32_t)AUDIO_CMD_START + 1U;
        }
    }
}

void USBPCAudio_Process(void)
{
    uint32_t cmd;
    uint32_t size;
    uint8_t *buf;

    if (playbackEnabled == 0U)
    {
        /* Keep the Windows UAC endpoint alive while another local source is
           selected, but never let incoming USB packets touch the codec. */
        pendingAudioInit = 0U;
        pendingAudioCmd = 0U;
        pendingVolumeValid = 0U;
        pendingMuteValid = 0U;
        if (audioHardwareActive != 0U)
        {
            (void)BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
            audioHardwareActive = 0U;
        }
        pendingAudioDeInit = 0U;
        return;
    }

    if (pendingAudioDeInit != 0U)
    {
        pendingAudioDeInit = 0U;
        if (audioHardwareActive != 0U)
        {
            (void)BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
            audioHardwareActive = 0U;
        }
    }
    if (pendingAudioInit != 0U)
    {
        uint32_t freq = pendingAudioFreq;
        uint8_t vol = (uint8_t)pendingAudioVolume;
        pendingAudioInit = 0U;
        if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_AUTO, vol, freq) == AUDIO_OK)
        {
            (void)BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
            audioHardwareActive = 1U;
        }
    }

    cmd = pendingAudioCmd;
    if (cmd != 0U && audioHardwareActive != 0U)
    {
        buf = (uint8_t *)pendingAudioBuffer;
        size = pendingAudioSize;
        pendingAudioCmd = 0U;
        cmd--;
        if (cmd == AUDIO_CMD_START) (void)BSP_AUDIO_OUT_Play((uint16_t *)buf, 2U * size);
        else if (cmd == AUDIO_CMD_PLAY) (void)BSP_AUDIO_OUT_ChangeBuffer((uint16_t *)buf, 2U * size);
        else if (cmd == AUDIO_CMD_STOP)
        {
            (void)BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
            audioHardwareActive = 0U;
        }
    }
    else if (cmd != 0U)
    {
        pendingAudioCmd = 0U;
    }

    if (pendingVolumeValid != 0U && audioHardwareActive != 0U)
    {
        uint8_t vol = pendingVolume;
        pendingVolumeValid = 0U;
        (void)BSP_AUDIO_OUT_SetVolume(vol);
    }
    if (pendingMuteValid != 0U && audioHardwareActive != 0U)
    {
        uint8_t mute = pendingMute;
        pendingMuteValid = 0U;
        (void)BSP_AUDIO_OUT_SetMute(mute);
    }
}

void USBPCAudio_AudioHalfCallback(void) { if (pcConfigured != 0U) USBD_AUDIO_Sync(&hUsbDeviceFS, AUDIO_OFFSET_HALF); }
void USBPCAudio_AudioCompleteCallback(void) { if (pcConfigured != 0U) USBD_AUDIO_Sync(&hUsbDeviceFS, AUDIO_OFFSET_FULL); }
void USBPCAudio_FS_IRQHandler(void) { usbPcDiagIrqCount++; HAL_PCD_IRQHandler(&hpcd_fs); }
