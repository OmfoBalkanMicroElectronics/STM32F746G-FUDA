#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"
#include <stdio.h>

#define USBD_VID 0x0483U
#define USBD_PID 0x5740U
#define USBD_LANGID_STRING 0x409U
#define USBD_MANUFACTURER_STRING "CleanMP3Player"
#define USBD_PRODUCT_STRING "CleanMP3Player PC Audio + Media Keys"
#define USBD_CONFIGURATION_STRING "UAC1 + HID"
#define USBD_INTERFACE_STRING "PC Audio"

static uint8_t *DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static uint8_t *InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);

USBD_DescriptorsTypeDef AUDIO_Desc = {
 DeviceDescriptor, LangIDStrDescriptor, ManufacturerStrDescriptor, ProductStrDescriptor,
 SerialStrDescriptor, ConfigStrDescriptor, InterfaceStrDescriptor
};

__ALIGN_BEGIN static uint8_t devDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
 0x12, USB_DESC_TYPE_DEVICE, 0x00,0x02, 0xEF,0x02,0x01, USB_MAX_EP0_SIZE,
 LOBYTE(USBD_VID),HIBYTE(USBD_VID), LOBYTE(USBD_PID),HIBYTE(USBD_PID),
 0x00,0x02, USBD_IDX_MFC_STR,USBD_IDX_PRODUCT_STR,USBD_IDX_SERIAL_STR, USBD_MAX_NUM_CONFIGURATION
};
__ALIGN_BEGIN static uint8_t langDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {USB_LEN_LANGID_STR_DESC,USB_DESC_TYPE_STRING,LOBYTE(USBD_LANGID_STRING),HIBYTE(USBD_LANGID_STRING)};
__ALIGN_BEGIN static uint8_t strDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN static uint8_t serialDesc[USB_SIZ_STRING_SERIAL] __ALIGN_END = {USB_SIZ_STRING_SERIAL,USB_DESC_TYPE_STRING};

static uint8_t *DeviceDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;*length=sizeof(devDesc);return devDesc;}
static uint8_t *LangIDStrDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;*length=sizeof(langDesc);return langDesc;}
static uint8_t *ManufacturerStrDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;USBD_GetString((uint8_t*)USBD_MANUFACTURER_STRING,strDesc,length);return strDesc;}
static uint8_t *ProductStrDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;USBD_GetString((uint8_t*)USBD_PRODUCT_STRING,strDesc,length);return strDesc;}
static uint8_t *ConfigStrDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;USBD_GetString((uint8_t*)USBD_CONFIGURATION_STRING,strDesc,length);return strDesc;}
static uint8_t *InterfaceStrDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){(void)speed;USBD_GetString((uint8_t*)USBD_INTERFACE_STRING,strDesc,length);return strDesc;}
static uint8_t *SerialStrDescriptor(USBD_SpeedTypeDef speed,uint16_t *length){uint32_t d0,d1;(void)speed;d0=*(uint32_t*)DEVICE_ID1;d1=*(uint32_t*)DEVICE_ID2;d0+=*(uint32_t*)DEVICE_ID3;if(d0!=0U){IntToUnicode(d0,&serialDesc[2],8);IntToUnicode(d1,&serialDesc[18],4);}*length=USB_SIZ_STRING_SERIAL;return serialDesc;}
static void IntToUnicode(uint32_t value,uint8_t *pbuf,uint8_t len){for(uint8_t i=0;i<len;i++){uint8_t d=(uint8_t)(value>>28);pbuf[2*i]=(uint8_t)(d<10?d+'0':d-10+'A');pbuf[2*i+1]=0;value<<=4;}}
