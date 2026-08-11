#include "stm32f7xx_hal.h"
#include "usbh_core.h"

/* STM32746G-DISCO CN12: USB OTG HS in ULPI mode. The GPIO/clock mapping is
   taken from STM32CubeF7 V1.17.4 Projects/STM32746G-Discovery/Applications/
   USB_Host/MSC_RTOS. */
HCD_HandleTypeDef hhcd;

void HAL_HCD_MspInit(HCD_HandleTypeDef *hcd)
{
    GPIO_InitTypeDef gpio = {0};
    if (hcd->Instance != USB_OTG_HS) return;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_HS;

    gpio.Pin = GPIO_PIN_5;                 /* ULPI CLK */
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_3;                 /* ULPI D0 */
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5 |
               GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13; /* D1..D7 */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_0;                 /* ULPI STP */
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_4;                 /* ULPI NXT */
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = GPIO_PIN_2;                 /* ULPI DIR */
    HAL_GPIO_Init(GPIOC, &gpio);

    __HAL_RCC_USB_OTG_HS_ULPI_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

void HAL_HCD_MspDeInit(HCD_HandleTypeDef *hcd)
{
    if (hcd->Instance == USB_OTG_HS)
    {
        HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
        __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
        __HAL_RCC_USB_OTG_HS_ULPI_CLK_DISABLE();
    }
}

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_IncTimer((USBH_HandleTypeDef *)hcd->pData);
}

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_Connect((USBH_HandleTypeDef *)hcd->pData);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_Disconnect((USBH_HandleTypeDef *)hcd->pData);
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_PortEnabled((USBH_HandleTypeDef *)hcd->pData);
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hcd)
{
    USBH_LL_PortDisabled((USBH_HandleTypeDef *)hcd->pData);
}

void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *hcd, uint8_t chnum,
                                         HCD_URBStateTypeDef state)
{
    (void)hcd;
    (void)chnum;
    (void)state;
}

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
    hhcd.Instance = USB_OTG_HS;
    hhcd.Init.Host_channels = 11U;
    hhcd.Init.dma_enable = 0U;
    hhcd.Init.low_power_enable = 0U;
    hhcd.Init.phy_itface = HCD_PHY_ULPI;
    hhcd.Init.Sof_enable = 0U;
    hhcd.Init.speed = HCD_SPEED_HIGH;
    hhcd.Init.vbus_sensing_enable = 0U;
    hhcd.Init.use_external_vbus = 1U;
    hhcd.Init.lpm_enable = 0U;
    hhcd.pData = phost;
    phost->pData = &hhcd;

    if (HAL_HCD_Init(&hhcd) != HAL_OK) return USBH_FAIL;
    USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd));
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
    return HAL_HCD_DeInit((HCD_HandleTypeDef *)phost->pData) == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
    return HAL_HCD_Start((HCD_HandleTypeDef *)phost->pData) == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
    return HAL_HCD_Stop((HCD_HandleTypeDef *)phost->pData) == HAL_OK ? USBH_OK : USBH_FAIL;
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
    switch (HAL_HCD_GetCurrentSpeed((HCD_HandleTypeDef *)phost->pData))
    {
        case 0U: return USBH_SPEED_HIGH;
        case 2U: return USBH_SPEED_LOW;
        default: return USBH_SPEED_FULL;
    }
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
    return HAL_HCD_ResetPort((HCD_HandleTypeDef *)phost->pData) == HAL_OK ? USBH_OK : USBH_FAIL;
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return HAL_HCD_HC_GetXferCount((HCD_HandleTypeDef *)phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe,
                                    uint8_t epnum, uint8_t dev_address,
                                    uint8_t speed, uint8_t ep_type, uint16_t mps)
{
    HAL_HCD_HC_Init((HCD_HandleTypeDef *)phost->pData, pipe, epnum, dev_address,
                    speed, ep_type, mps);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    HAL_HCD_HC_Halt((HCD_HandleTypeDef *)phost->pData, pipe);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe,
                                     uint8_t direction, uint8_t ep_type,
                                     uint8_t token, uint8_t *buffer,
                                     uint16_t length, uint8_t do_ping)
{
    HAL_HCD_HC_SubmitRequest((HCD_HandleTypeDef *)phost->pData, pipe, direction,
                             ep_type, token, buffer, length, do_ping);
    return USBH_OK;
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState((HCD_HandleTypeDef *)phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
    (void)phost;
    (void)state;
    /* CN12 HS uses the board's external ULPI/VBUS path; CubeF7 does not toggle
       the FS power switch for this configuration. */
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t toggle)
{
    HCD_HandleTypeDef *hcd = (HCD_HandleTypeDef *)phost->pData;
    if (hcd->hc[pipe].ep_is_in != 0U) hcd->hc[pipe].toggle_in = toggle;
    else hcd->hc[pipe].toggle_out = toggle;
    return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    HCD_HandleTypeDef *hcd = (HCD_HandleTypeDef *)phost->pData;
    return hcd->hc[pipe].ep_is_in != 0U ? hcd->hc[pipe].toggle_in : hcd->hc[pipe].toggle_out;
}

void USBH_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}
