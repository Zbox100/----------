#include "stm32f10x.h"
#include "Delay.h"
#include "usart.h"
#include "Oled.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
/*部分引用*/
extern QueueHandle_t DTH11Handler;
// DHT11的数据引脚连接到GPIOA的GPIO_Pin_7
#define DHT11_DATA_PIN GPIO_Pin_7
#define DHT11_DATA_GPIO_PORT                   GPIOA
#define DHT11_DATA_GPIO_CLK RCC_APB2Periph_GPIOA
// 读取GPIO的状态

uint8_t DHT11_DATA_IN(void)
{
    return GPIO_ReadInputDataBit(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

void DHT_GPIO_Config_Output( void )
{
	GPIO_InitTypeDef GPIO_InitStruct;	
	
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStruct.GPIO_Pin = DHT11_DATA_PIN;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStruct);
}
void DHT_GPIO_Config_Input( void )
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;                  //DHT11数据线需上拉:原浮空输入在模块未接时会读到随机电平
	GPIO_InitStruct.GPIO_Pin = DHT11_DATA_PIN;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStruct);
}

void Pull_Low(void)
{
    // 拉低PA7引脚
    GPIO_ResetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

void Pull_High(void)
{
    // 拉高PA7引脚
    GPIO_SetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

/****************************************************************************
* 描 述 : 从DHT11读取一个字节，MSB先行
* 入 参 : 无
* 返回值 : uint8_t
***************************************************************************/
/* 带超时的电平等待:轮询直到引脚等于level,最多空转timeout次;返回1=等到,0=超时。替代原来无上限的while(电平)死等 */
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
if(!DHT11_WaitLevel(Bit_RESET,500)) return 0xFF; //等待50us低电平起始位结束(带超时;超时返回0xFF让校验和必然失败)
/*DHT11 以23~27us的高电平表示“0”，以68~74us的高电平表示“1” */
    Delay_fnus(40); //延时40us,这个延时需要大于数据0持续的时间即可
if(DHT11_DATA_IN()==Bit_SET) // 40us后仍为高电平表示数据“1”
{
DHT11_WaitLevel(Bit_SET,500); //等待数据位1的高电平结束(带超时)
temp|=(uint8_t)(0x01<<(7-i)); //把第7-i位置1，MSB先行
}
else // 40us后为低电平表示数据“0”
{
temp&=(uint8_t)~(0x01<<(7-i)); //把第7-i位置0，MSB先行
}
}
return temp;
}


/***************************************************************************
* 描 述 : 初始化DHT11信号引脚为输出
* 入 参 : 无
* 返回值 : 无
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
typedef struct
{
    uint8_t humi_int;    // 湿度的整数部分
    uint8_t humi_deci;   // 湿度的小数部分
    uint8_t temp_int;    // 温度的整数部分
    uint8_t temp_deci;   // 温度的小数部分
    uint8_t check_sum;   // 校验和
} DHT11_Data_TypeDef;



/*************************************************************************
* 描 述 : 一次完整的数据传输为40bit，高位先出
* 入 参 : 8bit 湿度整数 + 8bit 湿度小数 + 8bit 温度整数 + 8bit 温度小数 + 8bit 校验和
* 返回值 : uint8_t
***********************************************************************/
#define SUCCESS 0
#define ERROR 1

uint8_t Read_DHT11(DHT11_Data_TypeDef *DHT11_Data)
{
    DHT_GPIO_Config_Output() ;// 输出模式
    Pull_Low(); // 主机拉低
    vTaskDelay(pdMS_TO_TICKS(18));  //18ms起始低电平:改用阻塞延时让出CPU(原Delay_fnms为纯忙等)
    Pull_High(); // 总线拉高 主机延时30us
    Delay_fnus(30); // 延时30us
    DHT_GPIO_Config_Input(); // 主机设为输入 判断从机响应信号

    if(DHT11_DATA_IN() == Bit_RESET) // 判断从机是否有低电平响应信号 如不响应则跳出，响应则向下运行
    {
        if(!DHT11_WaitLevel(Bit_RESET,2000)){DHT_GPIO_Config_Output();Pull_High();return ERROR;} //等待从机83us低电平响应结束(带超时;超时释放总线并判失败)
        DHT11_WaitLevel(Bit_SET,2000); //等待从机87us高电平响应结束(带超时)

        /*开始接收数据*/
        DHT11_Data->humi_int = Read_Byte();
        DHT11_Data->humi_deci = Read_Byte();
        DHT11_Data->temp_int = Read_Byte();
        DHT11_Data->temp_deci = Read_Byte();
        DHT11_Data->check_sum = Read_Byte();

        DHT_GPIO_Config_Output(); // 读取结束，引脚改为输出模式
        Pull_High(); // 主机拉高

        /*检查读取的数据是否正确*/
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
* 描 述 : 串口实时显示温湿度
* 入 参 : 无
* 返回值 : 无
***************************************************************************/
void DHT11_SCAN(void)
{
	DHT11_Data_TypeDef DHT11_Data;

    
	if(Read_DHT11(&DHT11_Data)==SUCCESS)
		{
			UsartPrintf(USART1,"\r\n读取DHT11成功!\r\n\r\n湿度为%d.%d ％RH ，温度为 %d.%d ℃ \r\n",
											DHT11_Data.humi_int, DHT11_Data.humi_deci,
											DHT11_Data.temp_int, DHT11_Data.temp_deci);
            xQueueSend(DTH11Handler,&DHT11_Data,portMAX_DELAY);

         }

}


