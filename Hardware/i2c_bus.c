#include <stddef.h>
#include "i2c_bus.h"

/**
  ******************************************************************************
  * @file    i2c_bus.c
  * @brief   软件 I2C 协议层实现：7 位地址、寄存器读写、统一错误码。
  * @note    三条总线各自独立，不共享任何状态；没有全局 ack/err。
  ******************************************************************************
  */

/* 引脚与时序沿用改造前各设备的值，只做搬移，不做"顺手优化"：
   OLED  : PB8/PB9，开漏，无额外延时
   ADXL  : PA5/PA4，SCL 推挽 + SDA 开漏，读取时切 IPU，每阶段 3µs
   MAX   : PA11/PA12，开漏，每阶段 1µs                                */
const SoftI2C_Bus_t I2C_Bus_OLED =
{
    GPIOB, GPIO_Pin_8,  GPIOB, GPIO_Pin_9,  RCC_APB2Periph_GPIOB, 0, 0, 0
};

const SoftI2C_Bus_t I2C_Bus_ADXL =
{
    GPIOA, GPIO_Pin_5,  GPIOA, GPIO_Pin_4,  RCC_APB2Periph_GPIOA, 3, 1, 1
};

const SoftI2C_Bus_t I2C_Bus_MAX =
{
    GPIOA, GPIO_Pin_11, GPIOA, GPIO_Pin_12, RCC_APB2Periph_GPIOA, 0, 0, 0
};

I2C_Status_t I2C_WriteBytes(const SoftI2C_Bus_t *bus, uint8_t addr7,
                            const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if((bus == NULL) || (buf == NULL) || (len == 0))
    {
        return I2C_ERR_PARAM;
    }

    SoftI2C_Start(bus);
    if(SoftI2C_WriteByte(bus, (uint8_t)(addr7 << 1)) != 0)
    {
        SoftI2C_Stop(bus);
        return I2C_ERR_NACK;
    }

    for(i = 0; i < len; i++)
    {
        if(SoftI2C_WriteByte(bus, buf[i]) != 0)
        {
            SoftI2C_Stop(bus);
            return I2C_ERR_NACK;
        }
    }

    SoftI2C_Stop(bus);
    return I2C_OK;
}

I2C_Status_t I2C_WriteReg(const SoftI2C_Bus_t *bus, uint8_t addr7,
                          uint8_t reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;
    buf[1] = val;

    return I2C_WriteBytes(bus, addr7, buf, 2);
}

I2C_Status_t I2C_ReadRegs(const SoftI2C_Bus_t *bus, uint8_t addr7,
                          uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if((bus == NULL) || (buf == NULL) || (len == 0))
    {
        return I2C_ERR_PARAM;
    }

    /* 写寄存器地址 */
    SoftI2C_Start(bus);
    if(SoftI2C_WriteByte(bus, (uint8_t)(addr7 << 1)) != 0)
    {
        SoftI2C_Stop(bus);
        return I2C_ERR_NACK;
    }
    if(SoftI2C_WriteByte(bus, reg) != 0)
    {
        SoftI2C_Stop(bus);
        return I2C_ERR_NACK;
    }

    /* 重复起始 + 读方向 */
    SoftI2C_Start(bus);
    if(SoftI2C_WriteByte(bus, (uint8_t)((addr7 << 1) | 0x01)) != 0)
    {
        SoftI2C_Stop(bus);
        return I2C_ERR_NACK;
    }

    for(i = 0; i < len; i++)
    {
        /* 最后一个字节回 NACK，其余回 ACK */
        buf[i] = SoftI2C_ReadByte(bus, (i + 1 < len) ? 1 : 0);
    }

    SoftI2C_Stop(bus);
    return I2C_OK;
}

I2C_Status_t I2C_ReadReg(const SoftI2C_Bus_t *bus, uint8_t addr7,
                         uint8_t reg, uint8_t *out)
{
    return I2C_ReadRegs(bus, addr7, reg, out, 1);
}
