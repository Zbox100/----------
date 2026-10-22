#include "stm32f10x.h"
#include "Delay.h"
#include "usart.h"
#include "Oled.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "DHT11.h"

/* DHT11 数据队列:由 DHT11_SCAN 覆盖写入,显示任务只保留最新值 */
extern QueueHandle_t DHT11Handler;

/* 串口综合显示用的最新温湿度快照(与队列并行,不影响显示任务取数) */
volatile DHT11_Data_TypeDef g_dht11_latest;
volatile uint8_t g_dht11_valid = 0;

/* DHT11 数据线:PA7 */
#define DHT11_DATA_PIN       GPIO_Pin_7
#define DHT11_DATA_GPIO_PORT GPIOA
#define DHT11_DATA_GPIO_CLK  RCC_APB2Periph_GPIOA

/* 读取数据线电平 */
uint8_t DHT11_DATA_IN(void)
{
    return GPIO_ReadInputDataBit(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

void DHT_GPIO_Config_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Pin   = DHT11_DATA_PIN;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStruct);
}

void DHT_GPIO_Config_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 上拉输入:模块未接或总线空闲时保持高电平 */
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStruct.GPIO_Pin   = DHT11_DATA_PIN;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStruct);
}

void Pull_Low(void)
{
    /* 拉低 PA7 */
    GPIO_ResetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

void Pull_High(void)
{
    /* 释放 PA7 为高 */
    GPIO_SetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

/****************************************************************************
* 函数名 : DHT11_WaitLevel
* 功能   : 等待数据线到达指定电平
* 参数   : level 目标电平;timeout 轮询次数上限
* 返回值 : 1=等到,0=超时
* 说明   : 原实现为无上限 while(电平),传感器异常时会永久阻塞
***************************************************************************/
static uint8_t DHT11_WaitLevel(uint8_t level, uint16_t timeout)
{
    while((DHT11_DATA_IN() == level) && (--timeout));
    return (uint8_t)(timeout != 0);
}

static uint8_t Read_Byte(void)
{
uint8_t i, temp=0;
for(i=0;i<8;i++)
{
if(!DHT11_WaitLevel(Bit_RESET,500)) return 0xFF; //等 50us 低电平起始位,超时返回 0xFF
/*DHT11 用 23~27us 高电平表示 0,68~74us 高电平表示 1 */
    Delay_fnus(40); //延时 40us 采样:0 已回落为低,1 仍为高
if(DHT11_DATA_IN()==Bit_SET) // 40us 后仍为高电平,表示数据 1
{
DHT11_WaitLevel(Bit_SET,500); //等本位 1 的高电平结束
temp|=(uint8_t)(0x01<<(7-i)); //按 MSB 先出置位
}
else // 40us 后为低电平,表示数据 0
{
temp&=(uint8_t)~(0x01<<(7-i)); //按 MSB 先出清零
}
}
return temp;
}


/***************************************************************************
* 函数名 : DHT11_GPIO_Config
* 功能   : 初始化 DHT11 数据线为输出
***************************************************************************/
void DHT11_GPIO_Config(void)
{
GPIO_InitTypeDef GPIO_InitStructure;
RCC_APB2PeriphClockCmd(DHT11_DATA_GPIO_CLK, ENABLE);
GPIO_InitStructure.GPIO_Pin = DHT11_DATA_PIN;
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; //推挽输出
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStructure);

GPIO_SetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}



/*************************************************************************
* 函数名 : Read_DHT11
* 功能   : 读取一次 40bit 数据
* 数据   : 8bit 湿度整数 + 8bit 湿度小数 + 8bit 温度整数 + 8bit 温度小数 + 8bit 校验和
* 返回值 : SUCCESS / ERROR
***********************************************************************/
#define SUCCESS 0
#define ERROR 1

uint8_t Read_DHT11(DHT11_Data_TypeDef *DHT11_Data)
{
    DHT_GPIO_Config_Output(); //切换为输出
    Pull_Low(); //主机拉低
    vTaskDelay(pdMS_TO_TICKS(18));  //起始低电平至少 18ms;用阻塞延时让出 CPU
    Pull_High(); //释放总线,保持高电平约 30us
    Delay_fnus(30); //延时 30us
    DHT_GPIO_Config_Input(); //切换为输入,接收从机响应

    if(DHT11_DATA_IN() == Bit_RESET) //从机拉低表示有响应
    {
        if(!DHT11_WaitLevel(Bit_RESET,2000)){DHT_GPIO_Config_Output();Pull_High();return ERROR;} //等 83us 低电平响应结束,超时则释放总线并返回失败
        DHT11_WaitLevel(Bit_SET,2000); //等 87us 高电平响应结束

        /*开始接收数据*/
        DHT11_Data->humi_int = Read_Byte();
        DHT11_Data->humi_deci = Read_Byte();
        DHT11_Data->temp_int = Read_Byte();
        DHT11_Data->temp_deci = Read_Byte();
        DHT11_Data->check_sum = Read_Byte();

        DHT_GPIO_Config_Output(); //读完恢复为输出
        Pull_High(); //释放总线

        /*校验读取结果是否正确*/
        if(DHT11_Data->check_sum == DHT11_Data->humi_int + DHT11_Data->humi_deci + DHT11_Data->temp_int + DHT11_Data->temp_deci)
            return SUCCESS;
        else
            return ERROR;
    }
    else
    {
        return ERROR;
    }
}

/***************************************************************************
* 函数名 : DHT11_SCAN
* 功能   : 读取一次温湿度,成功则覆盖写入队列
***************************************************************************/
void DHT11_SCAN(void)
{
	DHT11_Data_TypeDef DHT11_Data;

    
	if(Read_DHT11(&DHT11_Data)==SUCCESS)
		{
            g_dht11_latest = DHT11_Data;    //供串口综合显示读取
            g_dht11_valid  = 1;
            xQueueOverwrite(DHT11Handler,&DHT11_Data);

         }

}
