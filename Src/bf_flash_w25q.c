/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Winbond w25q series stacked die flash driver.
 * Handles homogeneous stack of identical dies by calling die drivers.
 *
 * Author: jflyper
 */

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"
#include "bf_flash_w25q.h"

#define W25Q_PAGESIZE                                   256
#define W25Q_SECTORSIZE                                 4096

#define W25Q_INSTRUCTION_RDID                           0x9F
#define W25Q_INSTRUCTION_READ_BYTES                     0x03
#define W25Q_INSTRUCTION_READ_STATUS_REG                0x05
#define W25Q_INSTRUCTION_WRITE_STATUS_REG               0x01
#define W25Q_INSTRUCTION_WRITE_ENABLE                   0x06
#define W25Q_INSTRUCTION_WRITE_DISABLE                  0x04
#define W25Q_INSTRUCTION_PAGE_PROGRAM                   0x02
#define W25Q_INSTRUCTION_SECTOR_ERASE                   0xD8
#define W25Q_INSTRUCTION_BULK_ERASE                     0xC7

#define W25Q_STATUS_FLAG_WRITE_IN_PROGRESS              0x01
#define W25Q_STATUS_FLAG_WRITE_ENABLED                  0x02

#define W25Q256_INSTRUCTION_ENTER_4BYTE_ADDRESS_MODE    0xB7


// IMPORTANT: Timeout values are currently required to be set to the highest value required by any of the supported flash chips by this driver.

// The timeout we expect between being able to issue page program instructions
#define DEFAULT_TIMEOUT_MILLIS       6
#define SECTOR_ERASE_TIMEOUT_MILLIS  5000

// etracer65 notes: For bulk erase The 25Q16 takes about 3 seconds and the 25Q128 takes about 49
#define BULK_ERASE_TIMEOUT_MILLIS    50000

static uint32_t maxClkSPIHz;
static uint32_t maxReadClkSPIHz;

// Table of recognised FLASH devices
struct {
    uint32_t        jedecID;
    uint16_t        maxClkSPIMHz;
    uint16_t        maxReadClkSPIMHz;
    flashSector_t   sectors;
    uint16_t        pagesPerSector;
} w25qFlashConfig[] = {
    // Macronix MX25L3206E
    // Datasheet: https://docs.rs-online.com/5c85/0900766b814ac6f9.pdf
    { 0xC22016, 86, 33, 64, 256 },
    // Macronix MX25L6406E
    // Datasheet: https://www.macronix.com/Lists/Datasheet/Attachments/7370/MX25L6406E,%203V,%2064Mb,%20v1.9.pdf
    { 0xC22017, 86, 33, 128, 256 },
    // Macronix MX25L25635E
    // Datasheet: https://www.macronix.com/Lists/Datasheet/Attachments/7331/MX25L25635E,%203V,%20256Mb,%20v1.3.pdf
    { 0xC22019, 80, 50, 512, 256 },
    // Micron M25P16
    // Datasheet: https://www.micron.com/-/media/client/global/documents/products/data-sheet/nor-flash/serial-nor/m25p/m25p16.pdf
    { 0x202015, 25, 20, 32, 256 },
    // Micron N25Q064
    // Datasheet: https://www.micron.com/-/media/client/global/documents/products/data-sheet/nor-flash/serial-nor/n25q/n25q_64a_3v_65nm.pdf
    { 0x20BA17, 108, 54, 128, 256 },
    // Micron N25Q128
    // Datasheet: https://www.micron.com/-/media/client/global/documents/products/data-sheet/nor-flash/serial-nor/n25q/n25q_128mb_1_8v_65nm.pdf
    { 0x20ba18, 108, 54, 256, 256 },
    // Winbond W25Q16
    // Datasheet: https://www.winbond.com/resource-files/w25q16dv_revi_nov1714_web.pdf
    { 0xEF4015, 104, 50, 32, 256 },
    // Winbond W25Q32
    // Datasheet: https://www.winbond.com/resource-files/w25q32jv%20dtr%20revf%2002242017.pdf?__locale=zh_TW
    { 0xEF4016, 133, 50, 64, 256 },
    // Winbond W25Q64
    // Datasheet: https://www.winbond.com/resource-files/w25q64jv%20spi%20%20%20revc%2006032016%20kms.pdf
    { 0xEF4017, 133, 50, 128, 256 }, // W25Q64JV-IQ/JQ 
    { 0xEF7017, 133, 50, 128, 256 }, // W25Q64JV-IM/JM*
    // Winbond W25Q128
    // Datasheet: https://www.winbond.com/resource-files/w25q128fv%20rev.l%2008242015.pdf
    { 0xEF4018, 104, 50, 256, 256 },
    // Winbond W25Q128_DTR
    // Datasheet: https://www.winbond.com/resource-files/w25q128jv%20dtr%20revb%2011042016.pdf
    { 0xEF7018, 66, 50, 256, 256 },
    // Winbond W25Q256
    // Datasheet: https://www.winbond.com/resource-files/w25q256jv%20spi%20revb%2009202016.pdf
    { 0xEF4019, 133, 50, 512, 256 },
    // Cypress S25FL064L
    // Datasheet: https://www.cypress.com/file/316661/download
    { 0x016017, 133, 50, 128, 256 },
    // Cypress S25FL128L
    // Datasheet: https://www.cypress.com/file/316171/download
    { 0x016018, 133, 50, 256, 256 },
    // BergMicro W25Q32
    // Datasheet: https://www.winbond.com/resource-files/w25q32jv%20dtr%20revf%2002242017.pdf?__locale=zh_TW
    { 0xE04016, 133, 50, 1024, 16 },
    // End of list
    { 0x000000, 0, 0, 0, 0 }
};

const flashVTable_t w25q_vTable;


SPI_HandleTypeDef hspi1;


/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
    //Error_Handler();
    }
}

/**
  * Enable DMA controller clock
  */
void MX_SPI_DMA_Init(void)
{
    /* DMA controller clock enable */
    __HAL_RCC_DMA2_CLK_ENABLE();

    /* DMA interrupt init */
    /* DMA2_Stream0_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    /* DMA2_Stream2_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

}


  /* 选择FLASH: CS低电平 */
#define W25Q_ENABLE()      (GPIOA->BSRR = GPIO_PIN_4 << 16U)
  /*取消选择FLASH: CS高电平 */
#define W25Q_DISABLE()     (GPIOA->BSRR = GPIO_PIN_4)


 /**
  * @brief  向FLASH发送 写使能 命令
  * @param  none
  * @retval none
  */
static void w25q_writeEnable(flashDevice_t *fdevice)
{
    uint8_t data = W25Q_INSTRUCTION_WRITE_ENABLE;

    W25Q_ENABLE();
    HAL_SPI_Transmit(&hspi1, &data, 1, 100);
    W25Q_DISABLE();

    // Assume that we're about to do some writing, so the device is just about to become busy
    fdevice->couldBeBusy = true;
}

static uint8_t w25q_readStatus()
{
    uint8_t txdata = W25Q_INSTRUCTION_READ_STATUS_REG;
    uint8_t rxdata = 0;

    W25Q_ENABLE();

    if (HAL_SPI_Transmit(&hspi1, &txdata, 1, 100))
    {
        W25Q_DISABLE();
        return 0;   //SPI Transmit error
    }

    HAL_SPI_Receive(&hspi1, &rxdata, 1, 100);

    W25Q_DISABLE();

    return rxdata;
}

static bool w25q_isReady(flashDevice_t *fdevice)
{
    // If couldBeBusy is false, don't bother to poll the flash chip for its status
    fdevice->couldBeBusy = fdevice->couldBeBusy && ((w25q_readStatus() & W25Q_STATUS_FLAG_WRITE_IN_PROGRESS) != 0);

    return !fdevice->couldBeBusy;
}

static void w25q_setTimeout(flashDevice_t *fdevice, uint32_t timeoutMillis)
{
    uint32_t now = HAL_GetTick();
    fdevice->timeoutAt = now + timeoutMillis;
}

static bool w25q_waitForReady(flashDevice_t *fdevice)
{
    while (!w25q_isReady(fdevice)) {
        uint32_t now = HAL_GetTick();
        if (((int32_t)now - (int32_t)(fdevice->timeoutAt)) >= 0) {
            return false;
        }
    }

    fdevice->timeoutAt = 0;
    return true;
}

static uint32_t w25q_readChipId()
{
    uint32_t jedecID = 0;
    uint8_t txData = W25Q_INSTRUCTION_RDID;
    uint8_t rxData[3] = {0};

    W25Q_ENABLE();
    /* 发送JEDEC指令，读取ID */
    HAL_SPI_Transmit(&hspi1, &txData, 1, 100);

    if(!HAL_SPI_Receive(&hspi1, rxData, 3, 100))
    {//成功
        jedecID = rxData[0] << 16 | rxData[1] << 8 | rxData[0];
    }

    W25Q_DISABLE();

    return jedecID;
}

/**
 * Read chip identification and geometry information (into global `geometry`).
 *
 * Returns true if we get valid ident, false if something bad happened like there is no M25P16.
 */
static bool w25q_detect(flashDevice_t *fdevice)
{
    uint32_t chipID = 0;
    uint8_t index;
    flashGeometry_t *geometry = &fdevice->geometry;

    chipID = w25q_readChipId();
    
    for (index = 0; w25qFlashConfig[index].jedecID; index++) {
        if (w25qFlashConfig[index].jedecID == chipID) {
            maxClkSPIHz = w25qFlashConfig[index].maxClkSPIMHz * 1000000;
            maxReadClkSPIHz = w25qFlashConfig[index].maxReadClkSPIMHz * 1000000;
            geometry->sectors = w25qFlashConfig[index].sectors;
            geometry->pagesPerSector = w25qFlashConfig[index].pagesPerSector;
            break;
        }
    }

    if (w25qFlashConfig[index].jedecID == 0) {
        // Unsupported chip or not an SPI NOR flash
        geometry->sectors = 0;
        geometry->pagesPerSector = 0;
        geometry->sectorSize = 0;
        geometry->totalSize = 0;
        return false;
    }
    geometry->flashType = FLASH_TYPE_NOR;
    geometry->pageSize = W25Q_PAGESIZE;
    geometry->sectorSize = geometry->pagesPerSector * geometry->pageSize;
    geometry->totalSize = geometry->sectorSize * geometry->sectors;

    fdevice->couldBeBusy = true; // Just for luck we'll assume the chip could be busy even though it isn't specced to be
    fdevice->vTable = &w25q_vTable;

    return true;
}

//w25q硬件初始化
bool w25q_Init(flashDevice_t *fdevice)
{
    //上电后直接初始化SPI，不需要在此处再次初始化
    MX_SPI1_Init();

    //检测是否是W25q类的Flash设备
    if (w25q_detect(fdevice)) {
        return true;
    }
    return false;
}

static void w25q_eraseSector(flashDevice_t *fdevice, uint32_t address)
{
    uint8_t txdata[4] = 
    {
        W25Q_INSTRUCTION_SECTOR_ERASE, 
        (uint8_t)((address >> 16) & 0xFF),
        (uint8_t)((address >>  8) & 0xFF),
        (uint8_t)((address      ) & 0xFF)
    };

    w25q_waitForReady(fdevice);
    w25q_writeEnable(fdevice);

    W25Q_ENABLE();
    HAL_SPI_Transmit(&hspi1, txdata, 4, 100);
    W25Q_DISABLE();

    w25q_setTimeout(fdevice, SECTOR_ERASE_TIMEOUT_MILLIS);
}

static void w25q_eraseCompletely(flashDevice_t *fdevice)
{
    uint8_t txdata[1] = 
    {
        W25Q_INSTRUCTION_BULK_ERASE
    };

    w25q_waitForReady(fdevice);
    w25q_writeEnable(fdevice);

    W25Q_ENABLE();
    HAL_SPI_Transmit(&hspi1, txdata, 1, 100);
    W25Q_DISABLE();

    w25q_setTimeout(fdevice, BULK_ERASE_TIMEOUT_MILLIS);
}

static void w25q_pageProgramBegin(flashDevice_t *fdevice, uint32_t address)
{
    UNUSED(fdevice);

    fdevice->currentWriteAddress = address;
}

static void w25q_pageProgramContinue(flashDevice_t *fdevice, const uint8_t *data, int length)
{
    //只是为了强制取消const，让编译器不要报警，因为HAL库的问题
    uint8_t *constData = (uint8_t *)data;

    uint8_t txdata[4] = 
    {
        W25Q_INSTRUCTION_PAGE_PROGRAM, 
        (uint8_t)((fdevice->currentWriteAddress >> 16) & 0xFF),
        (uint8_t)((fdevice->currentWriteAddress >>  8) & 0xFF),
        (uint8_t)((fdevice->currentWriteAddress      ) & 0xFF)
    };

    w25q_waitForReady(fdevice);
    w25q_writeEnable(fdevice);

    //write command
    W25Q_ENABLE();
    HAL_SPI_Transmit(&hspi1, txdata, 4, 100);
    W25Q_DISABLE();

    //write data
    W25Q_ENABLE();
    HAL_SPI_Transmit(&hspi1, constData, length, 100);
    W25Q_DISABLE();

    fdevice->currentWriteAddress += length;
    w25q_setTimeout(fdevice, DEFAULT_TIMEOUT_MILLIS);
}

static void w25q_pageProgramFinish(flashDevice_t *fdevice)
{
    UNUSED(fdevice);

}

/**
 * Write bytes to a flash page. Address must not cross a page boundary.
 *
 * Bits can only be set to zero, not from zero back to one again. In order to set bits to 1, use the erase command.
 *
 * Length must be smaller than the page size.
 *
 * This will wait for the flash to become ready before writing begins.
 *
 * Datasheet indicates typical programming time is 0.8ms for 256 bytes, 0.2ms for 64 bytes, 0.05ms for 16 bytes.
 * (Although the maximum possible write time is noted as 5ms).
 *
 * If you want to write multiple buffers (whose sum of sizes is still not more than the page size) then you can
 * break this operation up into one beginProgram call, one or more continueProgram calls, and one finishProgram call.
 */
static void w25q_pageProgram(flashDevice_t *fdevice, uint32_t address, const uint8_t *data, int length)
{
    w25q_pageProgramBegin(fdevice, address);

    w25q_pageProgramContinue(fdevice, data, length);

    w25q_pageProgramFinish(fdevice);
}

/**
 * Read `length` bytes into the provided `buffer` from the flash starting from the given `address` (which need not lie
 * on a page boundary).
 *
 * The number of bytes actually read is returned, which can be zero if an error or timeout occurred.
 */
static int w25q_readBytes(flashDevice_t *fdevice, uint32_t address, uint8_t *buffer, int length)
{
    uint8_t txdata[4] = 
    {
        W25Q_INSTRUCTION_READ_BYTES, 
        (uint8_t)((address >> 16) & 0xFF),
        (uint8_t)((address >>  8) & 0xFF),
        (uint8_t)((address      ) & 0xFF)
    };

    if (!w25q_waitForReady(fdevice))
    {
        return 0;
    }

    w25q_writeEnable(fdevice);

    //write command
    W25Q_ENABLE();
    HAL_SPI_Transmit(&hspi1, txdata, 4, 100);
    W25Q_DISABLE();

    //read data
    W25Q_ENABLE();
    HAL_SPI_Receive(&hspi1, buffer, length, 100);
    W25Q_DISABLE();

    w25q_setTimeout(fdevice, DEFAULT_TIMEOUT_MILLIS);

    return length;
}

/**
 * Fetch information about the detected flash chip layout.
 *
 * Can be called before calling m25p16_init() (the result would have totalSize = 0).
 */
static const flashGeometry_t* w25q_getGeometry(flashDevice_t *fdevice)
{
    return &fdevice->geometry;
}

const flashVTable_t w25q_vTable = {
    .isReady = w25q_isReady,
    .waitForReady = w25q_waitForReady,
    .eraseSector = w25q_eraseSector,
    .eraseCompletely = w25q_eraseCompletely,
    .pageProgramBegin = w25q_pageProgramBegin,
    .pageProgramContinue = w25q_pageProgramContinue,
    .pageProgramFinish = w25q_pageProgramFinish,
    .pageProgram = w25q_pageProgram,
    .readBytes = w25q_readBytes,
    .getGeometry = w25q_getGeometry,
};
