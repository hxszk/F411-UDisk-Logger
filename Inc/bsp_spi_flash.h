#ifndef  __BSP_SPI_FLASH_H__
#define __BSP_SPI_FLASH_H__
#include "stm32f4xx_hal.h"

#define SPI_FLASH_SECTOR_NUM                   0x800    //W25Q64 Block num (128) * sector num per block(16)
#define SPI_FLASH_SECTOR_SIZE                  0x1000   //W25Q64 sector size(4096 bytes)

extern SPI_HandleTypeDef hspi1;

extern void MX_SPI1_Init(void);
extern void MX_SPI_DMA_Init(void);


extern uint32_t SPI_FLASH_ReadID(void);
extern int32_t SPI_FLASH_ReadData(uint8_t *pu8RxBuffer, uint32_t u32addr, uint16_t lens);
extern int32_t SPI_FLASH_WriteData(uint8_t* pu8Buf, uint32_t u32Addr, uint32_t u32Lens);

#endif // ! __BSP_SPI_FLASH_H__ 