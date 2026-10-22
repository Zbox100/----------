#include "usart.h"
#include "delay.h"
#include "head.h"
#include "Oled.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/*
************************************************************
*	函数名称:	Usart1_Init
*
*	函数功能:	串口1初始化
*
*	入口参数:	baud 设定的波特率
*
*	返回参数:	无
*
*	说明:		TX-PA9		RX-PA10,两个引脚均复用为串口
************************************************************
*/
 void Usart1_Init(unsigned int baud)
{

	GPIO_InitTypeDef gpioInitStruct;
	USART_InitTypeDef usartInitStruct;
	NVIC_InitTypeDef nvicInitStruct;
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	
	//PA9	TXD
	gpioInitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
	gpioInitStruct.GPIO_Pin = GPIO_Pin_9;
	gpioInitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &gpioInitStruct);
	
	//PA10	RXD
	gpioInitStruct.GPIO_Mode = GPIO_Mode_IPU         ;
	gpioInitStruct.GPIO_Pin = GPIO_Pin_10;
	gpioInitStruct.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &gpioInitStruct);
	
	usartInitStruct.USART_BaudRate = baud;
	usartInitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;		//无硬件流控
	usartInitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;						//接收和发送
	usartInitStruct.USART_Parity = USART_Parity_No;									//无校验
	usartInitStruct.USART_StopBits = USART_StopBits_1;								//1位停止位
	usartInitStruct.USART_WordLength = USART_WordLength_8b;							//8位数据位
	USART_Init(USART1, &usartInitStruct);
	
	USART_Cmd(USART1, ENABLE);														//使能串口
	
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);									//使能接收中断
	
	nvicInitStruct.NVIC_IRQChannel = USART1_IRQn;
	nvicInitStruct.NVIC_IRQChannelCmd = ENABLE;
	nvicInitStruct.NVIC_IRQChannelPreemptionPriority = 5;		//必须>=5:分组4下与configMAX_SYSCALL_INTERRUPT_PRIORITY(0x50)一致,否则ISR调用FromISR会触发断言
	nvicInitStruct.NVIC_IRQChannelSubPriority = 0;
	NVIC_Init(&nvicInitStruct);

}

/*
************************************************************
*	函数名称:	Usart_SendString
*
*	函数功能:	发送字符串数据
*
*	入口参数:	USARTx 串口句柄
*				str 要发送的数据
*				len 数据长度
*
*	返回参数:	无
*
*	说明:
************************************************************
*/
void Usart_SendString(USART_TypeDef *USARTx, unsigned char *str, unsigned short len)
{

	unsigned short count = 0;
	
	for(; count < len; count++)
	{
		USART_SendData(USARTx, *str++);									//发送数据
		while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);		//等待发送完成
	}

}

/*
************************************************************
*	函数名称:	UsartPrintf
*
*	函数功能:	格式化打印
*
*	入口参数:	USARTx 串口句柄
*				fmt 格式化字符串
*
*	返回参数:	无
*
*	说明:		使用 256 字节 static 缓冲,不占用调用者任务栈
************************************************************
*/
void UsartPrintf(USART_TypeDef *USARTx, char *fmt,...)
{

	static unsigned char UsartPrintfBuf[256];
	va_list ap;
	unsigned char *pStr = UsartPrintfBuf;
	
	va_start(ap, fmt);
	vsnprintf((char *)UsartPrintfBuf, sizeof(UsartPrintfBuf), fmt, ap);							//格式化
	va_end(ap);
	
	while(*pStr != 0)
	{
		USART_SendData(USARTx, *pStr++);
		while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET);
	}

}

/*
************************************************************
*	函数名称:	USART1_IRQHandler
*
*	函数功能:	串口1收发中断
*
*	入口参数:	无
*
*	返回参数:	无
*
*	说明:	中断服务程序
************************************************************
*/

void USART1_IRQHandler(void)
{
    /* 溢出/帧/噪声/校验错误:读DR清标志后直接丢弃,不做业务处理 */
    if((USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET) ||
       (USART_GetFlagStatus(USART1, USART_FLAG_FE)  != RESET) ||
       (USART_GetFlagStatus(USART1, USART_FLAG_NE)  != RESET) ||
       (USART_GetFlagStatus(USART1, USART_FLAG_PE)  != RESET))
    {
        (void)USART_ReceiveData(USART1);
        return;
    }
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uint8_t ch = (uint8_t)USART_ReceiveData(USART1);

        /* ISR 只把字节投进队列,去向(菜单/时间设置/退出)交由 Usart_Rx_f 判断 */
        xQueueSendFromISR(UsartRxHandler, &ch, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    USART_ClearITPendingBit(USART1, USART_IT_RXNE);
}
