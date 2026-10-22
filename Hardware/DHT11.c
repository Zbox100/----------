#include "stm32f10x.h"
#include "Delay.h"
#include "usart.h"
#include "Oled.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "DHT11.h"
/*��������*/
extern QueueHandle_t DHT11Handler;
// DHT11�������������ӵ�GPIOA��GPIO_Pin_7
#define DHT11_DATA_PIN GPIO_Pin_7
#define DHT11_DATA_GPIO_PORT                   GPIOA
#define DHT11_DATA_GPIO_CLK RCC_APB2Periph_GPIOA
// ��ȡGPIO��״̬

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
	
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;                  //DHT11������������:ԭ����������ģ��δ��ʱ����������ƽ
	GPIO_InitStruct.GPIO_Pin = DHT11_DATA_PIN;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStruct);
}

void Pull_Low(void)
{
    // ����PA7����
    GPIO_ResetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

void Pull_High(void)
{
    // ����PA7����
    GPIO_SetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}

/****************************************************************************
* �� �� : ��DHT11��ȡһ���ֽڣ�MSB����
* �� �� : ��
* ����ֵ : uint8_t
***************************************************************************/
/* ����ʱ�ĵ�ƽ�ȴ�:��ѯֱ�����ŵ���level,����תtimeout��;����1=�ȵ�,0=��ʱ�����ԭ�������޵�while(��ƽ)���� */
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
if(!DHT11_WaitLevel(Bit_RESET,500)) return 0xFF; //�ȴ�50us�͵�ƽ��ʼλ����(����ʱ;��ʱ����0xFF��У��ͱ�Ȼʧ��)
/*DHT11 ��23~27us�ĸߵ�ƽ��ʾ��0������68~74us�ĸߵ�ƽ��ʾ��1�� */
    Delay_fnus(40); //��ʱ40us,�����ʱ��Ҫ��������0������ʱ�伴��
if(DHT11_DATA_IN()==Bit_SET) // 40us����Ϊ�ߵ�ƽ��ʾ���ݡ�1��
{
DHT11_WaitLevel(Bit_SET,500); //�ȴ�����λ1�ĸߵ�ƽ����(����ʱ)
temp|=(uint8_t)(0x01<<(7-i)); //�ѵ�7-iλ��1��MSB����
}
else // 40us��Ϊ�͵�ƽ��ʾ���ݡ�0��
{
temp&=(uint8_t)~(0x01<<(7-i)); //�ѵ�7-iλ��0��MSB����
}
}
return temp;
}


/***************************************************************************
* �� �� : ��ʼ��DHT11�ź�����Ϊ���
* �� �� : ��
* ����ֵ : ��
***************************************************************************/
void DHT11_GPIO_Config(void)
{
GPIO_InitTypeDef GPIO_InitStructure;
RCC_APB2PeriphClockCmd(DHT11_DATA_GPIO_CLK, ENABLE);
GPIO_InitStructure.GPIO_Pin = DHT11_DATA_PIN;
GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; //�������
GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
GPIO_Init(DHT11_DATA_GPIO_PORT, &GPIO_InitStructure);
	
GPIO_SetBits(DHT11_DATA_GPIO_PORT, DHT11_DATA_PIN);
}



/*************************************************************************
* �� �� : һ�����������ݴ���Ϊ40bit����λ�ȳ�
* �� �� : 8bit ʪ������ + 8bit ʪ��С�� + 8bit �¶����� + 8bit �¶�С�� + 8bit У���
* ����ֵ : uint8_t
***********************************************************************/
#define SUCCESS 0
#define ERROR 1

uint8_t Read_DHT11(DHT11_Data_TypeDef *DHT11_Data)
{
    DHT_GPIO_Config_Output() ;// ���ģʽ
    Pull_Low(); // ��������
    vTaskDelay(pdMS_TO_TICKS(18));  //18ms��ʼ�͵�ƽ:����������ʱ�ó�CPU(ԭDelay_fnmsΪ��æ��)
    Pull_High(); // �������� ������ʱ30us
    Delay_fnus(30); // ��ʱ30us
    DHT_GPIO_Config_Input(); // ������Ϊ���� �жϴӻ���Ӧ�ź�

    if(DHT11_DATA_IN() == Bit_RESET) // �жϴӻ��Ƿ��е͵�ƽ��Ӧ�ź� �粻��Ӧ����������Ӧ����������
    {
        if(!DHT11_WaitLevel(Bit_RESET,2000)){DHT_GPIO_Config_Output();Pull_High();return ERROR;} //�ȴ��ӻ�83us�͵�ƽ��Ӧ����(����ʱ;��ʱ�ͷ����߲���ʧ��)
        DHT11_WaitLevel(Bit_SET,2000); //�ȴ��ӻ�87us�ߵ�ƽ��Ӧ����(����ʱ)

        /*��ʼ��������*/
        DHT11_Data->humi_int = Read_Byte();
        DHT11_Data->humi_deci = Read_Byte();
        DHT11_Data->temp_int = Read_Byte();
        DHT11_Data->temp_deci = Read_Byte();
        DHT11_Data->check_sum = Read_Byte();

        DHT_GPIO_Config_Output(); // ��ȡ���������Ÿ�Ϊ���ģʽ
        Pull_High(); // ��������

        /*����ȡ�������Ƿ���ȷ*/
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
* �� �� : ����ʵʱ��ʾ��ʪ��
* �� �� : ��
* ����ֵ : ��
***************************************************************************/
void DHT11_SCAN(void)
{
	DHT11_Data_TypeDef DHT11_Data;

    
	if(Read_DHT11(&DHT11_Data)==SUCCESS)
		{
			UsartPrintf(USART1,"\r\n��ȡDHT11�ɹ�!\r\n\r\nʪ��Ϊ%d.%d ��RH ���¶�Ϊ %d.%d �� \r\n",
											DHT11_Data.humi_int, DHT11_Data.humi_deci,
											DHT11_Data.temp_int, DHT11_Data.temp_deci);
            xQueueOverwrite(DHT11Handler,&DHT11_Data);

         }

}


