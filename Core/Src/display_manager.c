#include "display_manager.h"
#include "main.h"
#include "stm32f7xx_hal.h"

#define DISPLAY_PWM_HZ       250U
#define DISPLAY_PWM_STEPS    100U
#define DISPLAY_PWM_TICK_HZ  (DISPLAY_PWM_HZ * DISPLAY_PWM_STEPS)
#define SETTINGS_FLASH_START  0x080C0000U
#define SETTINGS_FLASH_END    0x08100000U
#define SETTINGS_MAGIC        0x53455431U
#define SETTINGS_VERSION      1U
#define SETTINGS_SAVE_DELAY   1500U

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t packed;
    uint32_t checksum;
} DisplaySettingsRecord;

static volatile uint8_t brightness = 80U;
static volatile uint8_t sleeping = 0U;
static volatile uint8_t pwmHigh = 0U;
static uint16_t timeoutSeconds = 0U;
static uint32_t lastActivityTick = 0U;
static uint8_t initialized = 0U;
static uint8_t settingsDirty = 0U;
static uint32_t settingsChangedTick = 0U;
static uint32_t settingsSequence = 0U;
static uint32_t settingsNextAddress = SETTINGS_FLASH_START;

static uint32_t settingsChecksum(uint32_t sequence, uint32_t packed)
{
    return SETTINGS_MAGIC ^ sequence ^ packed ^ 0xA5963C5AU;
}

static uint8_t settingsRecordEmpty(const DisplaySettingsRecord *record)
{
    return (record->magic == 0xFFFFFFFFU && record->sequence == 0xFFFFFFFFU &&
            record->packed == 0xFFFFFFFFU && record->checksum == 0xFFFFFFFFU);
}

static void loadSettings(void)
{
    uint32_t address;
    uint8_t found = 0U;
    for (address = SETTINGS_FLASH_START;
         address + sizeof(DisplaySettingsRecord) <= SETTINGS_FLASH_END;
         address += sizeof(DisplaySettingsRecord))
    {
        const DisplaySettingsRecord *record = (const DisplaySettingsRecord *)address;
        if (settingsRecordEmpty(record) != 0U)
        {
            settingsNextAddress = address;
            break;
        }
        settingsNextAddress = address + sizeof(DisplaySettingsRecord);
        if (record->magic == SETTINGS_MAGIC &&
            record->checksum == settingsChecksum(record->sequence, record->packed) &&
            ((record->packed >> 24) & 0xFFU) == SETTINGS_VERSION)
        {
            uint8_t storedBrightness = (uint8_t)record->packed;
            uint16_t storedTimeout = (uint16_t)((record->packed >> 8) & 0xFFFFU);
            if (storedBrightness >= 25U && storedBrightness <= 100U &&
                (storedTimeout == 0U || storedTimeout == 30U || storedTimeout == 60U) &&
                (found == 0U || record->sequence >= settingsSequence))
            {
                brightness = storedBrightness;
                timeoutSeconds = storedTimeout;
                settingsSequence = record->sequence;
                found = 1U;
            }
        }
    }
}

static void saveSettings(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sectorError = 0U;
    uint32_t packed = ((uint32_t)SETTINGS_VERSION << 24) |
                      ((uint32_t)timeoutSeconds << 8) | brightness;
    uint32_t sequence = settingsSequence + 1U;
    uint32_t checksum = settingsChecksum(sequence, packed);
    HAL_StatusTypeDef status = HAL_OK;

    HAL_FLASH_Unlock();
    if (settingsNextAddress + sizeof(DisplaySettingsRecord) > SETTINGS_FLASH_END)
    {
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        erase.Sector = FLASH_SECTOR_7;
        erase.NbSectors = 1U;
        status = HAL_FLASHEx_Erase(&erase, &sectorError);
        settingsNextAddress = SETTINGS_FLASH_START;
    }

    /* Commit the magic last. An interrupted write can never be accepted as a
       valid record, and the next boot skips the partial slot. */
    if (status == HAL_OK) status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, settingsNextAddress + 4U, sequence);
    if (status == HAL_OK) status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, settingsNextAddress + 8U, packed);
    if (status == HAL_OK) status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, settingsNextAddress + 12U, checksum);
    if (status == HAL_OK) status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, settingsNextAddress, SETTINGS_MAGIC);
    HAL_FLASH_Lock();

    if (status == HAL_OK)
    {
        SCB_InvalidateDCache_by_Addr((uint32_t *)(settingsNextAddress & ~31U), 32);
        settingsNextAddress += sizeof(DisplaySettingsRecord);
        settingsSequence = sequence;
        settingsDirty = 0U;
    }
}

static void configurePwmOutput(void)
{
    HAL_NVIC_DisableIRQ(TIM7_IRQn);
    TIM7->CR1 &= ~TIM_CR1_CEN;
    TIM7->DIER = 0U;
    TIM7->SR = 0U;

    if (sleeping != 0U)
    {
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
    }
    else if (brightness >= 100U)
    {
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
        pwmHigh = 1U;
        TIM7->ARR = brightness - 1U;
        TIM7->CNT = 0U;
        TIM7->SR = 0U;
        TIM7->DIER = TIM_DIER_UIE;
        TIM7->CR1 |= TIM_CR1_CEN;
        HAL_NVIC_EnableIRQ(TIM7_IRQn);
    }
}

void DisplayManager_Init(void)
{
    uint32_t timerClock;
    if (initialized != 0U) return;

    loadSettings();
    __HAL_RCC_TIM7_CLK_ENABLE();
    timerClock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_HCLK_DIV1) timerClock *= 2U;

    TIM7->CR1 = 0U;
    TIM7->PSC = (timerClock / DISPLAY_PWM_TICK_HZ) - 1U;
    TIM7->ARR = brightness - 1U;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0U;
    HAL_NVIC_SetPriority(TIM7_IRQn, 10U, 0U);

    lastActivityTick = HAL_GetTick();
    initialized = 1U;
    configurePwmOutput();
}

void DisplayManager_SetBrightness(uint8_t value)
{
    if (value < 25U) value = 25U;
    if (value > 100U) value = 100U;
    brightness = value;
    settingsDirty = 1U;
    settingsChangedTick = HAL_GetTick();
    if (initialized != 0U) configurePwmOutput();
}

uint8_t DisplayManager_GetBrightness(void)
{
    return brightness;
}

void DisplayManager_SetTimeout(uint16_t seconds)
{
    timeoutSeconds = (seconds == 30U || seconds == 60U) ? seconds : 0U;
    lastActivityTick = HAL_GetTick();
    settingsDirty = 1U;
    settingsChangedTick = lastActivityTick;
}

uint16_t DisplayManager_GetTimeout(void)
{
    return timeoutSeconds;
}

void DisplayManager_Tick(void)
{
    if (initialized == 0U) DisplayManager_Init();
    if (settingsDirty != 0U &&
        (uint32_t)(HAL_GetTick() - settingsChangedTick) >= SETTINGS_SAVE_DELAY)
    {
        saveSettings();
    }
    if (sleeping == 0U && timeoutSeconds != 0U &&
        (uint32_t)(HAL_GetTick() - lastActivityTick) >= (uint32_t)timeoutSeconds * 1000U)
    {
        sleeping = 1U;
        configurePwmOutput();
    }
}

uint8_t DisplayManager_NotifyTouch(void)
{
    uint8_t wasSleeping = sleeping;
    lastActivityTick = HAL_GetTick();
    if (sleeping != 0U)
    {
        sleeping = 0U;
        configurePwmOutput();
    }
    return wasSleeping;
}

uint8_t DisplayManager_IsSleeping(void)
{
    return sleeping;
}

void TIM7_IRQHandler(void)
{
    uint32_t phaseTicks;
    if ((TIM7->SR & TIM_SR_UIF) == 0U) return;
    TIM7->SR &= ~TIM_SR_UIF;

    if (pwmHigh != 0U)
    {
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
        pwmHigh = 0U;
        phaseTicks = DISPLAY_PWM_STEPS - brightness;
    }
    else
    {
        HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
        pwmHigh = 1U;
        phaseTicks = brightness;
    }
    TIM7->ARR = phaseTicks - 1U;
}
