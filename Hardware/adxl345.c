#include "stm32f10x.h"
#include "adxl345.h"
#include "i2c_bus.h"

/**
  ******************************************************************************
  * @file    adxl345.c
  * @brief   ADXL345 设备层：只保留寄存器读写与数据换算。
  * @note    软件 I2C 的引脚与时序统一由 i2c_ll / i2c_bus 提供；
  *          本文件不再包含任何 Start/Stop/位翻转代码。
  ******************************************************************************
  */

/* ADXL345 的 7 位 I2C 地址(原 8 位写法 0xA6)
   ALT ADDRESS 引脚接地为 0x53，接 VCC 为 0x1D */
#define ADXL345_ADDR7   0x53

unsigned char BUF[8];                        /* 三轴数据缓冲:0x32~0x37 共 6 字节 */
unsigned char ge, shi, bai, qian, wan;       /* 历史遗留的显示分解变量，保留以兼容 */
float temp_X, temp_Y, temp_Z;                /* 三轴加速度(mg) */

/**
  * @brief  写一个 ADXL345 寄存器。
  * @param  REG_Address 寄存器地址
  * @param  REG_data    写入值
  * @retval 无（通信失败时不重试，由上层按读数是否合理判断）
  */
void Single_Write_ADXL345(uint8_t REG_Address, uint8_t REG_data)
{
    (void)I2C_WriteReg(&I2C_Bus_ADXL, ADXL345_ADDR7, REG_Address, REG_data);
}

/**
  * @brief  读一个 ADXL345 寄存器。
  * @param  REG_Address 寄存器地址
  * @retval 寄存器值；通信失败时返回 0
  */
uint8_t Single_Read_ADXL345(uint8_t REG_Address)
{
    uint8_t data = 0;
    (void)I2C_ReadReg(&I2C_Bus_ADXL, ADXL345_ADDR7, REG_Address, &data);
    return data;
}

/**
  * @brief  连续读取 0x32~0x37 的 6 个数据寄存器到 BUF。
  * @retval 无
  */
void Multiple_Read_ADXL345(void)
{
    (void)I2C_ReadRegs(&I2C_Bus_ADXL, ADXL345_ADDR7, 0x32, BUF, 6);
}

/**
  * @brief  初始化 ADXL345：先初始化总线，再写配置寄存器。
  * @retval 无
  */
void Init_ADXL345(void)
{
    SoftI2C_Init(&I2C_Bus_ADXL);            /* 总线引脚 PA5(SCL)/PA4(SDA) */

    Single_Write_ADXL345(0x31, 0x0B);       /* 量程 ±16g，13 位模式 */
    Single_Write_ADXL345(0x2C, 0x0A);       /* 输出数据速率 100Hz */
    Single_Write_ADXL345(0x2D, 0x08);       /* 选择电源模式：测量 */
    Single_Write_ADXL345(0x2E, 0x80);       /* 使能 DATA_READY 中断 */
    Single_Write_ADXL345(0x1E, 0x02);       /* X 偏移 */
    Single_Write_ADXL345(0x1F, 0x01);       /* Y 偏移 */
    Single_Write_ADXL345(0x20, 0xFF);       /* Z 偏移 */
}

/**
  * @brief  读取三轴加速度并换算为 mg。
  * @retval 无
  * @note   必须用 int16_t 显式强转，否则负值会被当成大正数。
  */
void ReadData_x(void)
{
    int16_t dis_data;

    Multiple_Read_ADXL345();

    dis_data = (int16_t)((BUF[1] << 8) | BUF[0]);
    temp_X = (float)dis_data * 3.9f;

    dis_data = (int16_t)((BUF[3] << 8) | BUF[2]);
    temp_Y = (float)dis_data * 3.9f;

    dis_data = (int16_t)((BUF[5] << 8) | BUF[4]);
    temp_Z = (float)dis_data * 3.9f;
}
