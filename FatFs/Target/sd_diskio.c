/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @author  MCD Application Team
  * @brief   SD Disk I/O driver.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "cmsis_os2.h"


/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* use the default SD timout as defined in the platform BSP driver*/
#if defined(SDMMC_DATATIMEOUT)
#define SD_TIMEOUT SDMMC_DATATIMEOUT
#elif defined(SD_DATATIMEOUT)
#define SD_TIMEOUT SD_DATATIMEOUT
#else
#define SD_TIMEOUT 30 * 1000
#endif

#define SD_DEFAULT_BLOCK_SIZE 512
#define SD_READY_TIMEOUT_MS   1000U

static uint8_t SD_WaitReady(uint32_t timeoutMs)
{
  uint32_t start = HAL_GetTick();
  do
  {
    if(BSP_SD_GetCardState() == MSD_OK)
    {
      return 1U;
    }
    /* The media task is above TouchGFX priority. Never busy-spin here: a
       marginal/removed card must not freeze rendering or touch input. */
    if(osKernelGetState() == osKernelRunning)
    {
      osDelay(1U);
    }
  } while((uint32_t)(HAL_GetTick() - start) < timeoutMs);
  return 0U;
}

/*
 * Depending on the usecase, the SD card initialization could be done at the
 * application level, if it is the case define the flag below to disable
 * the BSP_SD_Init() call in the SD_Initialize().
 */

/* #define DISABLE_SD_INIT */

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
/* Live diagnostics readable over GDB.  A failed first command must not leave
   SDMMC1 poisoned for every later FatFs rescan. */
volatile uint32_t sdInitAttempts;
volatile uint32_t sdInitRecoveries;
volatile uint32_t sdWriteCalls;
volatile uint32_t sdWriteBlocks;
volatile uint32_t sdWriteFailures;
volatile uint32_t sdWriteLastSector;
volatile uint32_t sdWriteLastCount;
volatile uint32_t sdReadCalls;
volatile uint32_t sdReadBlocks;
volatile uint32_t sdReadFailures;
volatile uint32_t sdReadRecoveries;
volatile uint32_t sdReadLastSector;
volatile uint32_t sdReadLastCount;

/* Private function prototypes -----------------------------------------------*/
static DSTATUS SD_CheckStatus(BYTE lun);
DSTATUS SD_initialize (BYTE);
DSTATUS SD_status (BYTE);
DRESULT SD_read (BYTE, BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
  DRESULT SD_write (BYTE, const BYTE*, DWORD, UINT);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT SD_ioctl (BYTE, BYTE, void*);
#endif  /* _USE_IOCTL == 1 */

const Diskio_drvTypeDef  SD_Driver =
{
  SD_initialize,
  SD_status,
  SD_read,
#if  _USE_WRITE == 1
  SD_write,
#endif /* _USE_WRITE == 1 */

#if  _USE_IOCTL == 1
  SD_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/
static DSTATUS SD_CheckStatus(BYTE lun)
{
  Stat = STA_NOINIT;

  if(SD_WaitReady(SD_READY_TIMEOUT_MS) != 0U)
  {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

/**
  * @brief  Initializes a Drive
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_initialize(BYTE lun)
{
  uint32_t attempt;
  Stat = STA_NOINIT;
#if !defined(DISABLE_SD_INIT)
  for(attempt = 0U; attempt < 3U; attempt++)
  {
    sdInitAttempts++;
    if(BSP_SD_Init() == MSD_OK)
    {
      Stat = SD_CheckStatus(lun);
      if((Stat & STA_NOINIT) == 0U)
      {
        return Stat;
      }
    }

    /* HAL_SD_Init may fail during its first card command after a long debug
       reset.  Re-running Init without tearing down SDMMC preserves the stale
       command/error state forever, so perform a real peripheral recovery. */
    (void)BSP_SD_DeInit();
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
    sdInitRecoveries++;
    if(osKernelGetState() == osKernelRunning) osDelay(20U);
    else HAL_Delay(20U);
  }
#else
  Stat = SD_CheckStatus(lun);
#endif
  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_status(BYTE lun)
{
  return SD_CheckStatus(lun);
}

/**
  * @brief  Reads Sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
  UINT block;
  uint8_t attempt;
  (void)lun;
  if(buff == NULL || count == 0U) return RES_PARERR;

  sdReadCalls++;
  sdReadLastSector = (uint32_t)sector;
  sdReadLastCount = count;

  /* Some old SDSC cards become unreliable after a random seek when CMD18 is
     used or when a transient ready-state failure leaves SDMMC poisoned.
     Serialize reads as CMD17 and recover the physical transport before the
     error reaches FatFs (FIL.err would otherwise remain latched forever). */
  for(block = 0U; block < count; block++)
  {
    for(attempt = 0U; attempt < 3U; attempt++)
    {
      if(BSP_SD_ReadBlocks((uint32_t *)(buff + block * SD_DEFAULT_BLOCK_SIZE),
                           (uint32_t)sector + block, 1U, SD_TIMEOUT) == MSD_OK &&
         SD_WaitReady(SD_READY_TIMEOUT_MS) != 0U)
      {
        sdReadBlocks++;
        break;
      }

      sdReadRecoveries++;
      (void)BSP_SD_DeInit();
      __HAL_RCC_SDMMC1_FORCE_RESET();
      __HAL_RCC_SDMMC1_RELEASE_RESET();
      if(osKernelGetState() == osKernelRunning) osDelay(5U);
      else HAL_Delay(5U);
      if(BSP_SD_Init() != MSD_OK)
      {
        if(osKernelGetState() == osKernelRunning) osDelay(10U);
        else HAL_Delay(10U);
      }
    }
    if(attempt == 3U)
    {
      sdReadFailures++;
      return RES_ERROR;
    }
  }
  return RES_OK;
}

/**
  * @brief  Writes Sector(s)
  * @param  lun : not used
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
  UINT block;
  (void)lun;
  if(buff == NULL || count == 0U) return RES_PARERR;

  sdWriteCalls++;
  sdWriteLastSector = (uint32_t)sector;
  sdWriteLastCount = count;

  /* Old SDSC cards used by this project reject or stall CMD25 multi-block
     writes even though single-sector CMD24 is reliable.  FatFs may hand us
     4-16 KiB at once, so deliberately serialize it into 512-byte writes.
     This is still comfortably above the 192 kB/s stereo WAV data rate. */
  for(block = 0U; block < count; block++)
  {
    if(BSP_SD_WriteBlocks((uint32_t*)(buff + block * SD_DEFAULT_BLOCK_SIZE),
                          (uint32_t)sector + block, 1U, SD_TIMEOUT) != MSD_OK ||
       SD_WaitReady(SD_READY_TIMEOUT_MS) == 0U)
    {
      sdWriteFailures++;
      return RES_ERROR;
    }
    sdWriteBlocks++;
  }
  return RES_OK;
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  lun : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  DRESULT res = RES_ERROR;
  BSP_SD_CardInfo CardInfo;

  if (Stat & STA_NOINIT) return RES_NOTRDY;

  switch (cmd)
  {
  /* Make sure that no pending write process */
  case CTRL_SYNC :
    res = RES_OK;
    break;

  /* Get number of sectors on the disk (DWORD) */
  case GET_SECTOR_COUNT :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.LogBlockNbr;
    res = RES_OK;
    break;

  /* Get R/W sector size (WORD) */
  case GET_SECTOR_SIZE :
    BSP_SD_GetCardInfo(&CardInfo);
    *(WORD*)buff = CardInfo.LogBlockSize;
    res = RES_OK;
    break;

  /* Get erase block size in unit of sector (DWORD) */
  case GET_BLOCK_SIZE :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
	res = RES_OK;
    break;

  default:
    res = RES_PARERR;
  }

  return res;
}
#endif /* _USE_IOCTL == 1 */


