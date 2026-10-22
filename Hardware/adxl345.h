#ifndef __ADXL345_H
#define __ADXL345_H

#include "stm32f10x.h"

/**
  ******************************************************************************
  * @file    adxl345.h
  * @brief   ADXL345 三轴加速度计设备层接口。
  * @note    软件 I2C 的引脚与字节原语不在本层，见 i2c_ll.h / i2c_bus.h。
  ******************************************************************************
  */

extern float temp_X;    /**< X 轴加速度(mg) */
extern float temp_Y;    /**< Y 轴加速度(mg) */
extern float temp_Z;    /**< Z 轴加速度(mg) */

/**
  * @brief  初始化 ADXL345：初始化其软件 I2C 总线并写入配置寄存器。
  * @retval 无
  */
void Init_ADXL345(void);

/**
  * @brief  写一个 ADXL345 寄存器。
  * @param  REG_Address 寄存器地址
  * @param  REG_data    写入值
  * @retval 无
  */
void Single_Write_ADXL345(uint8_t REG_Address, uint8_t REG_data);

/**
  * @brief  读一个 ADXL345 寄存器。
  * @param  REG_Address 寄存器地址
  * @retval 寄存器值；通信失败时返回 0
  */
uint8_t Single_Read_ADXL345(uint8_t REG_Address);

/**
  * @brief  连续读取 0x32~0x37 的 6 个数据寄存器。
  * @retval 无
  */
void Multiple_Read_ADXL345(void);

/**
  * @brief  读取三轴加速度并换算为 mg，结果写入 temp_X/temp_Y/temp_Z。
  * @retval 无
  */
void ReadData_x(void);

#endif /* __ADXL345_H */
