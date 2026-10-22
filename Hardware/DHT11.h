#ifndef __DHT11_H_
#define __DHT11_H_

typedef struct
{
    uint8_t humi_int;    // 湿度的整数部分
    uint8_t humi_deci;   // 湿度的小数部分
    uint8_t temp_int;    // 温度的整数部分
    uint8_t temp_deci;   // 温度的小数部分
    uint8_t check_sum;   // 校验和
} DHT11_Data_TypeDef;

/* 串口综合显示用的最新温湿度快照 */
extern volatile DHT11_Data_TypeDef g_dht11_latest;
extern volatile uint8_t g_dht11_valid;


uint8_t DHT11_DATA_IN(void);
void DHT_GPIO_Config_Output( void );
void DHT_GPIO_Config_Input( void );
void Pull_Low(void);
void Pull_High(void);
static uint8_t Read_Byte(void);
void DHT11_GPIO_Config(void);
uint8_t Read_DHT11(DHT11_Data_TypeDef *DHT11_Data);
void DHT11_SCAN(void);

#endif
