#ifndef __I2C_BUS_H
#define __I2C_BUS_H

#include "i2c_ll.h"

/**
  ******************************************************************************
  * @file    i2c_bus.h
  * @brief   软件 I2C 协议层：7 位地址、寄存器读写、统一错误码。
  * @note    设备层只与这一层打交道，不再直接操作 Start/Stop/电平。
  ******************************************************************************
  */

/**
  * @brief 统一操作结果。
  */
typedef enum
{
    I2C_OK = 0,      /**< 成功 */
    I2C_ERR_NACK,    /**< 从机未应答 */
    I2C_ERR_PARAM,   /**< 入参非法（空指针或长度为 0） */
    I2C_ERR_BUS      /**< 总线异常（保留，本实现暂不产生） */
} I2C_Status_t;

/** @brief OLED 总线：SCL=PB8，SDA=PB9，无额外延时（保持改造前的快时序） */
extern const SoftI2C_Bus_t I2C_Bus_OLED;

/** @brief ADXL345 总线：SCL=PA5，SDA=PA4，每阶段 3µs，读取时切 IPU */
extern const SoftI2C_Bus_t I2C_Bus_ADXL;

/** @brief MAX30102 总线：SCL=PA11，SDA=PA12，每阶段 1µs */
extern const SoftI2C_Bus_t I2C_Bus_MAX;

/**
  * @brief  向设备连续写若干字节（首字节前的地址由本函数发送）。
  * @param  bus   总线参数。
  * @param  addr7 设备 7 位地址。
  * @param  buf   待写数据。
  * @param  len   字节数。
  * @retval I2C_OK / I2C_ERR_NACK / I2C_ERR_PARAM
  */
I2C_Status_t I2C_WriteBytes(const SoftI2C_Bus_t *bus, uint8_t addr7,
                            const uint8_t *buf, uint16_t len);

/**
  * @brief  写一个寄存器。
  * @param  bus   总线参数。
  * @param  addr7 设备 7 位地址。
  * @param  reg   寄存器地址。
  * @param  val   写入值。
  * @retval I2C_OK / I2C_ERR_NACK / I2C_ERR_PARAM
  */
I2C_Status_t I2C_WriteReg(const SoftI2C_Bus_t *bus, uint8_t addr7,
                          uint8_t reg, uint8_t val);

/**
  * @brief  读一个寄存器。
  * @param  bus   总线参数。
  * @param  addr7 设备 7 位地址。
  * @param  reg   寄存器地址。
  * @param  out   读出的值（不可为 NULL）。
  * @retval I2C_OK / I2C_ERR_NACK / I2C_ERR_PARAM
  */
I2C_Status_t I2C_ReadReg(const SoftI2C_Bus_t *bus, uint8_t addr7,
                         uint8_t reg, uint8_t *out);

/**
  * @brief  从指定寄存器开始连续读取若干字节（地址自动递增）。
  * @param  bus   总线参数。
  * @param  addr7 设备 7 位地址。
  * @param  reg   起始寄存器地址。
  * @param  buf   接收缓冲（不可为 NULL）。
  * @param  len   字节数。
  * @retval I2C_OK / I2C_ERR_NACK / I2C_ERR_PARAM
  */
I2C_Status_t I2C_ReadRegs(const SoftI2C_Bus_t *bus, uint8_t addr7,
                          uint8_t reg, uint8_t *buf, uint16_t len);

#endif /* __I2C_BUS_H */
