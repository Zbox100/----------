#include "MAX30102.h"
#include "i2c_bus.h"
uint16_t fifo_red;
uint16_t fifo_ir;
	/**
  ******************************************************************************
  * @file    Max30102.c
  * @author  智能手环项目组
  * @brief   MAX30102 驱动实现
  
  ******************************************************************************
  */
void MAX30102_GPIO(void)
{	
	RCC_APB2PeriphClockCmd(MAX30102_INTPin_CLK,ENABLE);
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Mode		=	GPIO_Mode_IPU;
	GPIO_InitStruct.GPIO_Pin		=	MAX30102_INTPin_Pin;
	GPIO_Init(MAX30102_INTPin_PORT,&GPIO_InitStruct);
	
}

uint8_t Max30102_reset(void)
{
	return (I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_MODE_CONFIG, 0x40) == I2C_OK) ? 1 : 0;
}

uint8_t Max30102_ReadReg(uint8_t reg)
{
	uint8_t value = 0;
	(void)I2C_ReadReg(&I2C_Bus_MAX, MAX30102_ADDR7, reg, &value);
	return value;
}

void MAX30102_Config(void)
{
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_INTR_ENABLE_1, 0xc0);
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_INTR_ENABLE_2, 0x00);
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_FIFO_WR_PTR, 0x00);
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_OVF_COUNTER, 0x00);
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_FIFO_RD_PTR, 0x00);

	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_FIFO_CONFIG, 0x0f);   //sample avg = 1, fifo rollover=false, fifo almost full = 17
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_MODE_CONFIG, 0x03);   //0x02 for Red only, 0x03 for SpO2 mode
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_SPO2_CONFIG, 0x27);   //SPO2_ADC range = 4096nA, 50Hz, 400uS
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_LED1_PA, 0x32);       //~10mA for LED1
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_LED2_PA, 0x32);       //~10mA for LED2
	(void)I2C_WriteReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_PILOT_PA, 0x7f);      //~25mA for Pilot LED
}

void max30102_read_fifo(void)
{
  uint16_t un_temp;
  uint8_t  status;
  fifo_red=0;
  fifo_ir=0;
  uint8_t ach_i2c_data[6];
  
  //read and clear status register
  (void)I2C_ReadReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_INTR_STATUS_1, &status);
  (void)I2C_ReadReg(&I2C_Bus_MAX, MAX30102_ADDR7, REG_INTR_STATUS_2, &status);
  
  (void)I2C_ReadRegs(&I2C_Bus_MAX, MAX30102_ADDR7, REG_FIFO_DATA, ach_i2c_data, 6);
	
  un_temp=ach_i2c_data[0];
  un_temp<<=14;
  fifo_red+=un_temp;
  un_temp=ach_i2c_data[1];
  un_temp<<=6;
  fifo_red+=un_temp;
  un_temp=ach_i2c_data[2];
	un_temp>>=2;
  fifo_red+=un_temp;
  
  un_temp=ach_i2c_data[3];
  un_temp<<=14;
  fifo_ir+=un_temp;
  un_temp=ach_i2c_data[4];
  un_temp<<=6;
  fifo_ir+=un_temp;
  un_temp=ach_i2c_data[5];
	un_temp>>=2;
  fifo_ir+=un_temp;
	
	if(fifo_ir<=10000)
	{
		fifo_ir=0;
	}
	if(fifo_red<=10000)
	{
		fifo_red=0;
	}
}


