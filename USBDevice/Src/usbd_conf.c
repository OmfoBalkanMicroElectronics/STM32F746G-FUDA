#include "stm32f7xx_hal.h"
#include "usbd_core.h"

PCD_HandleTypeDef hpcd_fs;

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd)
{
    GPIO_InitTypeDef gpio={0};
    if(pcd->Instance!=USB_OTG_FS) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_11|GPIO_PIN_12;
    gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_HIGH;
    gpio.Alternate=GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA,&gpio);
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn,6U,0U);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd){if(pcd->Instance==USB_OTG_FS){HAL_NVIC_DisableIRQ(OTG_FS_IRQn);__HAL_RCC_USB_OTG_FS_CLK_DISABLE();}}
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd){USBD_LL_SetupStage((USBD_HandleTypeDef*)hpcd->pData,(uint8_t*)hpcd->Setup);}
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd,uint8_t epnum){USBD_LL_DataOutStage((USBD_HandleTypeDef*)hpcd->pData,epnum,hpcd->OUT_ep[epnum].xfer_buff);}
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd,uint8_t epnum){USBD_LL_DataInStage((USBD_HandleTypeDef*)hpcd->pData,epnum,hpcd->IN_ep[epnum].xfer_buff);}
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd){USBD_LL_SOF((USBD_HandleTypeDef*)hpcd->pData);}
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd){USBD_LL_SetSpeed((USBD_HandleTypeDef*)hpcd->pData,USBD_SPEED_FULL);USBD_LL_Reset((USBD_HandleTypeDef*)hpcd->pData);}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd){USBD_LL_Suspend((USBD_HandleTypeDef*)hpcd->pData);}
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd){USBD_LL_Resume((USBD_HandleTypeDef*)hpcd->pData);}
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd,uint8_t epnum){USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)hpcd->pData,epnum);}
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd,uint8_t epnum){USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)hpcd->pData,epnum);}
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd){USBD_LL_DevConnected((USBD_HandleTypeDef*)hpcd->pData);}
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd){USBD_LL_DevDisconnected((USBD_HandleTypeDef*)hpcd->pData);}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev){hpcd_fs.Instance=USB_OTG_FS;hpcd_fs.Init.dev_endpoints=6;hpcd_fs.Init.speed=PCD_SPEED_FULL;hpcd_fs.Init.dma_enable=DISABLE;hpcd_fs.Init.phy_itface=PCD_PHY_EMBEDDED;hpcd_fs.Init.Sof_enable=DISABLE;hpcd_fs.Init.low_power_enable=DISABLE;hpcd_fs.Init.lpm_enable=DISABLE;hpcd_fs.Init.vbus_sensing_enable=DISABLE;hpcd_fs.Init.use_dedicated_ep1=DISABLE;hpcd_fs.pData=pdev;pdev->pData=&hpcd_fs;HAL_PCD_Init(&hpcd_fs);HAL_PCDEx_SetRxFiFo(&hpcd_fs,0x80);HAL_PCDEx_SetTxFiFo(&hpcd_fs,0,0x60);HAL_PCDEx_SetTxFiFo(&hpcd_fs,2,0x20);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev){HAL_PCD_DeInit((PCD_HandleTypeDef*)pdev->pData);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev){HAL_PCD_Start((PCD_HandleTypeDef*)pdev->pData);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev){HAL_PCD_Stop((PCD_HandleTypeDef*)pdev->pData);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev,uint8_t ep,uint8_t type,uint16_t mps){HAL_PCD_EP_Open((PCD_HandleTypeDef*)pdev->pData,ep,mps,type);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev,uint8_t ep){HAL_PCD_EP_Close((PCD_HandleTypeDef*)pdev->pData,ep);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev,uint8_t ep){HAL_PCD_EP_Flush((PCD_HandleTypeDef*)pdev->pData,ep);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev,uint8_t ep){HAL_PCD_EP_SetStall((PCD_HandleTypeDef*)pdev->pData,ep);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,uint8_t ep){HAL_PCD_EP_ClrStall((PCD_HandleTypeDef*)pdev->pData,ep);return USBD_OK;}
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev,uint8_t ep){PCD_HandleTypeDef*h=(PCD_HandleTypeDef*)pdev->pData;return((ep&0x80U)==0x80U)?h->IN_ep[ep&0x7FU].is_stall:h->OUT_ep[ep&0x7FU].is_stall;}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,uint8_t addr){HAL_PCD_SetAddress((PCD_HandleTypeDef*)pdev->pData,addr);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev,uint8_t ep,uint8_t*pbuf,uint32_t size){HAL_PCD_EP_Transmit((PCD_HandleTypeDef*)pdev->pData,ep,pbuf,size);return USBD_OK;}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,uint8_t ep,uint8_t*pbuf,uint32_t size){HAL_PCD_EP_Receive((PCD_HandleTypeDef*)pdev->pData,ep,pbuf,size);return USBD_OK;}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev,uint8_t ep){return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*)pdev->pData,ep);}
void USBD_LL_Delay(uint32_t delay){HAL_Delay(delay);}
