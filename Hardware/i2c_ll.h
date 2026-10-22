#ifndef __I2C_LL_H
#define __I2C_LL_H

#include "stm32f10x.h"

/**
  ******************************************************************************
  * @file    i2c_ll.h
  * @brief   软件 I2C 最低层：只负责引脚电平与"字节/应答位"的时序。
  *          本层不认识寄存器、不认识设备地址，也不保存任何全局状态。
  ******************************************************************************
  */

/**
  * @brief 一条软件 I2C 总线的硬件参数。
  * @note  三条总线各自一个实例，通过入参传递，避免像旧实现那样用全局 ack/err。
  */
typedef struct
{
    GPIO_TypeDef *scl_port;    /**< SCL 所在端口 */
    uint16_t      scl_pin;     /**< SCL 引脚 */
    GPIO_TypeDef *sda_port;    /**< SDA 所在端口 */
    uint16_t      sda_pin;     /**< SDA 引脚 */
    uint32_t      rcc_apb2;    /**< 端口时钟，如 RCC_APB2Periph_GPIOB */
    uint8_t       delay_us;    /**< 每个电平阶段(建立/高/低)的延时(µs)，0 表示不额外延时 */
    uint8_t       scl_pp;      /**< 1: SCL 推挽输出；0: 开漏输出 */
    uint8_t       sda_read_ipu;/**< 1: 读取时把 SDA 切为 IPU（模块无外部上拉时） */
} SoftI2C_Bus_t;

/**
  * @brief  初始化一条软件 I2C 总线。
  * @param  bus 总线参数（不可为 NULL）。
  * @retval 无
  */
void SoftI2C_Init(const SoftI2C_Bus_t *bus);

/**
  * @brief  产生起始信号（SCL 高电平期间 SDA 由高变低）。
  * @param  bus 总线参数。
  * @retval 无
  */
void SoftI2C_Start(const SoftI2C_Bus_t *bus);

/**
  * @brief  产生停止信号（SCL 高电平期间 SDA 由低变高）。
  * @param  bus 总线参数。
  * @retval 无
  */
void SoftI2C_Stop(const SoftI2C_Bus_t *bus);

/**
  * @brief  发送一个字节并读回从机应答位。
  * @param  bus  总线参数。
  * @param  byte 待发送字节（高位先发）。
  * @retval 0：收到 ACK；1：收到 NACK。
  */
uint8_t SoftI2C_WriteByte(const SoftI2C_Bus_t *bus, uint8_t byte);

/**
  * @brief  接收一个字节，并由主机发送应答/非应答。
  * @param  bus 总线参数。
  * @param  ack 1：主机回 ACK（继续读）；0：主机回 NACK（最后一字节）。
  * @retval 读到的字节。
  */
uint8_t SoftI2C_ReadByte(const SoftI2C_Bus_t *bus, uint8_t ack);

#endif /* __I2C_LL_H */
