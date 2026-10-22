#include "i2c_ll.h"
#include "Delay.h"

/**
  ******************************************************************************
  * @file    i2c_ll.c
  * @brief   软件 I2C 最低层实现：引脚电平与字节/应答位时序。
  * @note    翻转统一使用 SPL 的 GPIO_SetBits/GPIO_ResetBits（库函数翻转、非位带），
  *          代价是每次翻转多几条指令，换来实现简单、可移植。
  ******************************************************************************
  */

static void I2C_Delay(const SoftI2C_Bus_t *bus)
{
    if(bus->delay_us != 0)
    {
        Delay_fnus(bus->delay_us);
    }
}

static void I2C_SclWrite(const SoftI2C_Bus_t *bus, uint8_t level)
{
    if(level)
    {
        GPIO_SetBits(bus->scl_port, bus->scl_pin);
    }
    else
    {
        GPIO_ResetBits(bus->scl_port, bus->scl_pin);
    }
}

static void I2C_SdaWrite(const SoftI2C_Bus_t *bus, uint8_t level)
{
    if(level)
    {
        GPIO_SetBits(bus->sda_port, bus->sda_pin);
    }
    else
    {
        GPIO_ResetBits(bus->sda_port, bus->sda_pin);
    }
}

static uint8_t I2C_SdaRead(const SoftI2C_Bus_t *bus)
{
    return (GPIO_ReadInputDataBit(bus->sda_port, bus->sda_pin) != Bit_RESET) ? 1 : 0;
}

/* 无外部上拉的模块：读取期间把 SDA 切成内部上拉输入，读完再切回开漏输出 */
static void I2C_SdaToInput(const SoftI2C_Bus_t *bus)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = bus->sda_pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(bus->sda_port, &gpio);
}

static void I2C_SdaToOutput(const SoftI2C_Bus_t *bus)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = bus->sda_pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_Init(bus->sda_port, &gpio);
}

/* 总线恢复:从机若在一次异常传输后把 SDA 拉低,给 9 个时钟让它释放,再发 Stop。
   在初始化末尾调用一次,可清掉上电/复位瞬间的"总线卡死"状态 */
static void I2C_BusRecover(const SoftI2C_Bus_t *bus)
{
    uint8_t i;

    I2C_SdaWrite(bus, 1);
    I2C_Delay(bus);
    for(i = 0; i < 9; i++)
    {
        I2C_SclWrite(bus, 0);
        I2C_Delay(bus);
        I2C_SclWrite(bus, 1);
        I2C_Delay(bus);
    }
    SoftI2C_Stop(bus);
}

void SoftI2C_Init(const SoftI2C_Bus_t *bus)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(bus->rcc_apb2, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin   = bus->scl_pin;
    gpio.GPIO_Mode  = bus->scl_pp ? GPIO_Mode_Out_PP : GPIO_Mode_Out_OD;
    GPIO_Init(bus->scl_port, &gpio);

    gpio.GPIO_Pin  = bus->sda_pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(bus->sda_port, &gpio);

    /* 空闲态：两根线都释放为高 */
    I2C_SclWrite(bus, 1);
    I2C_SdaWrite(bus, 1);

    I2C_BusRecover(bus);
}

void SoftI2C_Start(const SoftI2C_Bus_t *bus)
{
    I2C_SdaWrite(bus, 1);
    I2C_SclWrite(bus, 1);
    I2C_Delay(bus);
    I2C_SdaWrite(bus, 0);
    I2C_Delay(bus);
    I2C_SclWrite(bus, 0);
    I2C_Delay(bus);
}

void SoftI2C_Stop(const SoftI2C_Bus_t *bus)
{
    I2C_SdaWrite(bus, 0);
    I2C_Delay(bus);
    I2C_SclWrite(bus, 1);
    I2C_Delay(bus);
    I2C_SdaWrite(bus, 1);
    I2C_Delay(bus);
}

uint8_t SoftI2C_WriteByte(const SoftI2C_Bus_t *bus, uint8_t byte)
{
    uint8_t i;
    uint8_t nack;

    for(i = 0; i < 8; i++)
    {
        I2C_SdaWrite(bus, (byte & 0x80) ? 1 : 0);
        I2C_Delay(bus);                 /* 数据建立 */
        I2C_SclWrite(bus, 1);
        I2C_Delay(bus);                 /* 高电平 */
        I2C_SclWrite(bus, 0);
        I2C_Delay(bus);                 /* 低电平 */
        byte <<= 1;
    }

    /* 释放 SDA，给从机留出拉低应答的时间 */
    if(bus->sda_read_ipu)
    {
        I2C_SdaToInput(bus);
    }
    else
    {
        I2C_SdaWrite(bus, 1);
    }
    I2C_Delay(bus);
    I2C_SclWrite(bus, 1);
    I2C_Delay(bus);
    nack = I2C_SdaRead(bus) ? 1 : 0;
    I2C_SclWrite(bus, 0);
    I2C_Delay(bus);

    if(bus->sda_read_ipu)
    {
        I2C_SdaToOutput(bus);
        I2C_SdaWrite(bus, 1);
    }

    return nack;
}

uint8_t SoftI2C_ReadByte(const SoftI2C_Bus_t *bus, uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0;

    if(bus->sda_read_ipu)
    {
        I2C_SdaToInput(bus);
    }
    else
    {
        I2C_SdaWrite(bus, 1);
    }

    for(i = 0; i < 8; i++)
    {
        I2C_Delay(bus);
        I2C_SclWrite(bus, 1);
        I2C_Delay(bus);
        data <<= 1;
        if(I2C_SdaRead(bus))
        {
            data |= 1;
        }
        I2C_SclWrite(bus, 0);
        I2C_Delay(bus);
    }

    if(bus->sda_read_ipu)
    {
        I2C_SdaToOutput(bus);
    }

    /* 主机应答：ack=1 拉低表示继续读，ack=0 释放表示最后一字节 */
    I2C_SdaWrite(bus, ack ? 0 : 1);
    I2C_Delay(bus);
    I2C_SclWrite(bus, 1);
    I2C_Delay(bus);
    I2C_SclWrite(bus, 0);
    I2C_Delay(bus);
    I2C_SdaWrite(bus, 1);

    return data;
}
