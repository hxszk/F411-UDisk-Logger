#include "stm32f4xx_hal.h"
#include "stdio.h"

SPI_HandleTypeDef hspi1;

#define W25X_WriteEnable                0x06
#define W25X_WriteDisable               0x04
#define W25X_ReadStatusReg1             0x05
#define W25X_ReadStatusReg2             0x35
#define W25X_ReadStatusReg3             0x15
#define W25X_WriteStatusReg1            0x01
#define W25X_WriteStatusReg2            0x31
#define W25X_WriteStatusReg3            0x11
#define W25X_ReadData                   0x03
#define W25X_FastReadData               0x0B
#define W25X_FastReadDual               0x3B
#define W25X_PageProgram                0x02
#define W25X_BlockErase                 0xD8
#define W25X_SectorErase                0x20
#define W25X_ChipErase                  0xC7
#define W25X_PowerDown                  0xB9
#define W25X_ReleasePowerDown           0xAB
#define W25X_DeviceID                   0xAB
#define W25X_ManufactDeviceID           0x90
#define W25X_JedecDeviceID              0x9F

#define W25X_WIP_Flag                   0x01  /* Write In Progress (WIP) flag */

#define W25X_PageSize                   256
#define W25X_SectorSize                 4096

  /* 选择FLASH: CS低电平 */
#define SPI_FLASH_CS_ENABLE()      (GPIOA->BSRR = GPIO_PIN_4 << 16U)
  /*取消选择FLASH: CS高电平 */
#define SPI_FLASH_CS_DISABLE()     (GPIOA->BSRR = GPIO_PIN_4)

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
void MX_SPI1_Init(void)
{

    /* USER CODE BEGIN SPI1_Init 0 */

    /* USER CODE END SPI1_Init 0 */

    /* USER CODE BEGIN SPI1_Init 1 */

    /* USER CODE END SPI1_Init 1 */
    /* SPI1 parameter configuration*/
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
    /* USER CODE BEGIN SPI1_Init 2 */

    /* USER CODE END SPI1_Init 2 */

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

 /**
  * @brief  向FLASH发送 写使能 命令
  * @param  none
  * @retval none
  */
void SPI_FLASH_WriteEnable(void)
{
    uint8_t data = W25X_WriteEnable;

    SPI_FLASH_CS_ENABLE();
    HAL_SPI_Transmit(&hspi1, &data, 1, 100);
    SPI_FLASH_CS_DISABLE();
}

 /**
  * @brief  等待WIP(BUSY)标志被置0，即等待到FLASH内部数据写入完毕
  * @param  u16TimeOut 如果busy的话，延迟1ms再读，重复读取次数
  * @retval 0：成功，other：失败
  */
int32_t SPI_FLASH_WaitForWriteEnd(uint16_t u16TimeOut)
{
    uint8_t data = W25X_ReadStatusReg1;
    uint8_t u8Status = 0;
    uint32_t temp = 0;

    SPI_FLASH_CS_ENABLE();
    
    if (HAL_SPI_Transmit(&hspi1, &data, 1, 100))
    {
        SPI_FLASH_CS_DISABLE();
        return 1;   //SPI Transmit error
    }

    while (u16TimeOut--)
    {
        if(!HAL_SPI_Receive(&hspi1, &u8Status, 1, 100))
        {
            if ((u8Status & W25X_WIP_Flag) == 0)
            {
                SPI_FLASH_CS_DISABLE();
                return 0;   //成功
            }
        }
        else
        {
            SPI_FLASH_CS_DISABLE();
            return 2;   //SPI Read error
        }

        //HAL_Delay(1); //USB调用时应该是在中断中调用的，所以这里不能用HAL_Delay;
        temp = 100000;
        while(temp--);   
    }
    

    SPI_FLASH_CS_DISABLE();
    return 3;   //timeout error
}


 /**
  * @brief  读取FLASH ID
  * @param 	无
  * @retval FLASH ID
  */
uint32_t SPI_FLASH_ReadID(void)
{
    uint32_t jedecID = 0;
    uint8_t txData = W25X_JedecDeviceID;
    uint8_t rxData[3] = {0};

    SPI_FLASH_CS_ENABLE();
    /* 发送JEDEC指令，读取ID */
    HAL_SPI_Transmit(&hspi1, &txData, 1, 1000);

    if(!HAL_SPI_Receive(&hspi1, rxData, 3, 1000))
    {//成功
        jedecID = rxData[0] << 16 | rxData[1] << 8 | rxData[0];
    }

    SPI_FLASH_CS_DISABLE();

    return jedecID;
}

 /**
  * @brief  从FLASH中读取数据，阻塞的方式
  * @param  pu8RxBuffer： 用来保存读取数据的内存地址
  *         u32addr： 要读flash的地址
  *         lens：    读取的数据长度
  * @retval 0：成功 ，other：失败
  */
int32_t SPI_FLASH_ReadData(uint8_t *pu8RxBuffer, uint32_t u32addr, uint16_t lens)
{
    int32_t ret = 0;
    uint8_t txData[4] = {0};
    txData[0] = W25X_ReadData;
    txData[1] = (uint8_t)((u32addr >> 16) & 0xFF);
    txData[2] = (uint8_t)((u32addr >> 8)  & 0xFF);
    txData[3] = (uint8_t)((u32addr) & 0xFF);

    SPI_FLASH_CS_ENABLE();

    HAL_SPI_Transmit(&hspi1, txData, 4, 1000);

    if(!HAL_SPI_Receive(&hspi1, pu8RxBuffer, lens, 1000))
    {
        printf(" ");
    }

    SPI_FLASH_CS_DISABLE();

    return ret;
}

/**
  * @brief  Rx Transfer completed callback.
  * @param  hspi pointer to a SPI_HandleTypeDef structure that contains
  *               the configuration information for SPI module.
  * @retval None
  */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    /*SPI1通过DMA接收数据完成*/
    if(hspi == &hspi1)
    {

    }

}

 /**
  * @brief  擦除FLASH扇区
  * @param  u32Addr：要擦除的扇区地址
  *         u32Count: 要擦除扇区的数量
  * @retval  0: 成功，other：失败代码
  */
int32_t SPI_FLASH_SectorErase(uint32_t u32Addr, uint32_t u32Count)
{
    HAL_StatusTypeDef retval = HAL_OK;
    uint8_t u8Data[4] = {W25X_SectorErase, 0, 0, 0};

    while(u32Count)
    {
        u8Data[1] = (uint8_t)((u32Addr >> 16) & 0xFF);
        u8Data[2] = (uint8_t)((u32Addr >>  8) & 0xFF);
        u8Data[3] = (uint8_t)((u32Addr      ) & 0xFF);

        /* 发送FLASH写使能命令 */
        SPI_FLASH_WriteEnable();
        SPI_FLASH_WaitForWriteEnd(100);
        /* 擦除扇区 */
        /* 选择FLASH: CS低电平 */
        SPI_FLASH_CS_ENABLE();
        /* 发送扇区擦除指令*/
        retval = HAL_SPI_Transmit(&hspi1, u8Data, 4, 100);
        
        SPI_FLASH_CS_DISABLE();

        if (!retval)
        {
            retval = SPI_FLASH_WaitForWriteEnd(500);
        }

        u32Count--;
        u32Addr += W25X_SectorSize;
    }

    return retval;
}

 /**
  * @brief  对FLASH按页写入数据，调用本函数写入数据前需要先擦除扇区
  * @param	pu8Buf，要写入数据的指针
  * @param u32Addr，写入地址
  * @param  u16Lens，写入数据长度，必须小于等于SPI_FLASH_PerWritePageSize
  * @retval 0: 成功，other：失败代码
  */
int32_t SPI_FLASH_PageWrite(uint8_t* pu8Buf, uint32_t u32Addr, uint16_t u16Lens)
{
    HAL_StatusTypeDef retval;
    uint8_t u8Data[4] = {
        W25X_PageProgram, 
        (uint8_t)((u32Addr >> 16) & 0xFF),
        (uint8_t)((u32Addr >>  8) & 0xFF),
        (uint8_t)((u32Addr      ) & 0xFF)
        };

    /* 发送FLASH写使能命令 */
    SPI_FLASH_WriteEnable();

    /* 选择FLASH: CS低电平 */
    SPI_FLASH_CS_ENABLE();

    /* 发送扇区写入指令*/
    retval = HAL_SPI_Transmit(&hspi1, u8Data, 4, 100);
    if (retval)
    {
        return retval;
    }

    if (u16Lens > W25X_PageSize)
    {
        u16Lens = W25X_PageSize;
    }

    retval = HAL_SPI_Transmit(&hspi1, pu8Buf, u16Lens, 500);
    if (retval)
    {
        return retval;
    }

    SPI_FLASH_CS_DISABLE();

    retval = SPI_FLASH_WaitForWriteEnd(500);

    return retval;
}

/**
  * @brief  对FLASH写入数据，调用本函数写入数据前需要先擦除扇区
  * @param	pu8Buf，要写入数据的指针
  * @param  u32Addr，写入地址
  * @param  u32Lens，写入数据长度
  * @retval 无
  */
int32_t SPI_FLASH_WriteData(uint8_t* pu8Buf, uint32_t u32Addr, uint32_t u32Lens)
{
    HAL_StatusTypeDef retval = HAL_OK;

    uint16_t u16SecoterCount = 0, u16Temp = 0, u16PreResidue = 0;

    if (!u32Lens)
    {
        return retval;
    }

    /*计算所占的Sector的长度*/
    u16PreResidue = W25X_SectorSize - (u32Addr % W25X_SectorSize);
    u16SecoterCount = 1;
    if (u32Lens > u16PreResidue)
    {
        u16SecoterCount += ((u32Lens - u16PreResidue - 1) % W25X_SectorSize) + 1; 
    }

    /*擦除所有的Sector*/
    if (SPI_FLASH_SectorErase(u32Addr, u16SecoterCount))
    {
        return 1;   //擦除扇区失败
    }

    /*写入数据*/
    /*计算，前余量，并写入页*/
    u16PreResidue  = (W25X_PageSize - (u32Addr % W25X_PageSize));    
    
    if (SPI_FLASH_PageWrite(pu8Buf, u32Addr, u16PreResidue))
    {
        return 2;   //写入数据失败
    }

    /*计算剩余的数量，并写入*/
    if (u32Lens > u16PreResidue)
    {
        pu8Buf  += u16PreResidue;
        u32Addr += u16PreResidue;
        u32Lens -= u16PreResidue;

        while(u32Lens)
        {
            u16Temp = (uint16_t)(u32Lens > W25X_PageSize ? W25X_PageSize : u32Lens);
            if (SPI_FLASH_PageWrite(pu8Buf, u32Addr, u16Temp))
            {
                return 2;   //写入数据失败
            }

            pu8Buf  += u16Temp;
            u32Addr += u16Temp;
            u32Lens -= u16Temp;
        }
    }
    
    return 0; //写入成功
}
