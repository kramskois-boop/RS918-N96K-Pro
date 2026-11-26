/*  -*-  mode: c; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4; coding: utf-8  -*-  */
/************************************************************************************
**                                                                                 **
**                               mcHF QRP Transceiver                              **
**                             K Atanassov - M0NKA 2014                            **
**                                                                                 **
**---------------------------------------------------------------------------------**
**                                                                                 **
**  File name:                                                                     **
**  Description:                                                                   **
**  Last Modified:                                                                 **
**  Licence:		GNU GPLv3                                                      **
************************************************************************************/

// Common
#include "uhsdr_board.h"
#include "uhsdr_hw_i2c.h"
#include "i2c.h"

uint16_t MCHF_I2C_DeviceReady(I2C_HandleTypeDef* hi2c, uchar I2CAddr)
{
	return HAL_I2C_IsDeviceReady(hi2c, I2CAddr,100,100);
}

uint16_t MCHF_I2C_WriteRegister(I2C_HandleTypeDef* hi2c, uchar I2CAddr,uint16_t addr,uint16_t addr_size, uchar RegisterValue)
{
    HAL_StatusTypeDef i2cRet = HAL_I2C_Mem_Write(hi2c,I2CAddr,addr,addr_size,&RegisterValue,1,100);

    return  i2cRet != HAL_OK?0xFF00:0;
}

uint16_t MCHF_I2C_WriteBlock(I2C_HandleTypeDef* hi2c, uchar I2CAddr, uint16_t addr, uint16_t addr_size, const uint8_t* data, uint32_t size)
{
    HAL_StatusTypeDef i2cRet = HAL_I2C_Mem_Write(hi2c,I2CAddr,addr,addr_size,(uint8_t*)data,size,100);

    return  i2cRet != HAL_OK?0xFF00:0;
}

uint16_t MCHF_I2C_ReadRegister(I2C_HandleTypeDef* hi2c, uchar I2CAddr, uint16_t addr, uint16_t addr_size, uint8_t *RegisterValue)
{
    HAL_StatusTypeDef i2cRet = HAL_I2C_Mem_Read(hi2c,I2CAddr,addr,addr_size,RegisterValue,1,100);

    return  i2cRet != HAL_OK?0xFF00:0;
}

uint16_t MCHF_I2C_ReadBlock(I2C_HandleTypeDef* hi2c, uchar I2CAddr,uint16_t addr, uint16_t addr_size, uint8_t *data, uint32_t size)
{

    HAL_StatusTypeDef i2cRet = HAL_I2C_Mem_Read ( hi2c, I2CAddr, addr, addr_size, data, size, 100 );

    return  i2cRet != HAL_OK?0xFF00:0;
}





/**
 * @brief init I2C
 * @param speed in Hertz !!!
 */
void MchfHw_I2C_ChangeSpeed(I2C_HandleTypeDef* hi2c)
{

    /// FIXME: Support for more than I2C1 und I2C2
#ifdef STM32F4
    uint8_t speedIdx;

    if   ( hi2c->Instance == I2C1) { speedIdx = I2C_BUS_1; }
    else    					   { speedIdx = I2C_BUS_2; }


    HAL_I2C_DeInit(hi2c);
    // FIXME: F7PORT: I2C Clock Timing works differently on the F7, we need to supply correct register values instead of a simple speed value
    //
    // overclocking 3//  20000 pour 168Mhz/ 18666 pour 180Mhz/ 17500 pour 192Mhz/ 15555 pour 216Mhz
    // il parait que la I2C via les fonction HAL_ s'adapte a la nouvelle frequence du processeur overclockee a 216Mhz
    // donc pas besoins de changer la valeur de la frequence I2C_speed, on garde les vrais valeurs en Hz comme avant , on ne change pas de coeff
    hi2c->Init.ClockSpeed = ts.i2c_speed[speedIdx] * 20000;  // 15556;  // I2C_BUS_SPEED_MULT; // 17500;   //  I2C_BUS_SPEED_MULT; // on vient de l'adapter pour 192Mhz au lieu du 20000 du 168Mhz
#endif

    HAL_I2C_Init(hi2c);

}




void MchfHw_I2C_Reset( I2C_HandleTypeDef* hi2c )
{
    MchfHw_I2C_ChangeSpeed(hi2c);
}

/*
 * @brief full init of I2C1 including GPIO and bus clock
 */
void mchf_hw_i2c1_init()
{
    MchfHw_I2C_ChangeSpeed( &hi2c1 );
}

/*
 * @brief full init of I2C2 including GPIO and bus clock
 */
void mchf_hw_i2c2_init()
{
    MchfHw_I2C_ChangeSpeed(&hi2c2);
}

void mchf_hw_i2c1_reset(void)
{
    MchfHw_I2C_Reset(&hi2c1);
}

void mchf_hw_i2c2_reset(void)
{
    MchfHw_I2C_Reset(&hi2c2);
}

uint16_t mchf_hw_i2c1_WriteRegister(uchar I2CAddr, uchar RegisterAddr, uchar RegisterValue)
{
    return MCHF_I2C_WriteRegister(&hi2c1, I2CAddr, RegisterAddr, 1, RegisterValue);
}

uint16_t mchf_hw_i2c1_WriteBlock(uchar I2CAddr,uchar RegisterAddr, uchar *data, ulong size)
{
    return MCHF_I2C_WriteBlock(&hi2c1, I2CAddr,RegisterAddr, 1, data, size);
}

uint16_t mchf_hw_i2c1_ReadRegister(uchar I2CAddr,uchar RegisterAddr, uchar *RegisterValue)
{
    return MCHF_I2C_ReadRegister(&hi2c1, I2CAddr,RegisterAddr, 1, RegisterValue);
}

uint16_t mchf_hw_i2c1_ReadData(uchar I2CAddr,uchar RegisterAddr, uchar *data, ulong size)
{
    return  MCHF_I2C_ReadBlock(&hi2c1, I2CAddr, RegisterAddr, 1, data, size);
}
