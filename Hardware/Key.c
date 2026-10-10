#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "Oled.h"
#define  Enter 1
#define  Up    2
#define  Down  3

/**
  ******************************************************************************
  * @file    Key.c
  * @author  张照军
  * @version V1.0.0
  * @date    2024年4月23日14:24:21
  * @brief   按键与按键中断相关函数配置
  ******************************************************************************
  */

/**
  * 函数名：Key_Init
  * 参数：void
  * 返回值：void
  * 具体解释：该函数用于初始化STM32的GPIOB和GPIOA端口，配置PB13、PB14、PB15和PA8为下拉输入模式，
  *           配置PB12和PB1为推挽输出模式，并将PB1置高电平，PB12置低电平。这是按键和外部中断的基础配置。
  */
void Key_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);		//开启GPIOB的时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);		//开启GPIOA的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);        // 使能AFIO时钟
	/*GPIO初始化*/

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
  
	GPIO_InitStructure.GPIO_Pin =GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);						
    
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
	GPIO_InitStructure.GPIO_Pin =GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);						
    
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;			
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12|GPIO_Pin_1;	
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;			
	
	GPIO_Init(GPIOB, &GPIO_InitStructure);					
    GPIO_SetBits(GPIOB,GPIO_Pin_1);
    GPIO_ResetBits(GPIOB,GPIO_Pin_12);
}


/**
  * 函数名：EXTI8_Config
  * 参数：void
  * 返回值：void
  * 具体解释：该函数用于配置外部中断EXTI线8，将其与GPIOA的PA8引脚连接，并设置为上升沿触发。
  *           同时配置NVIC，使能EXTI9_5_IRQn中断通道，设置中断优先级。这是用于检测按键中断的配置。
  */
void EXTI8_Config(void)
{
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource8); 
    EXTI_InitStructure.EXTI_Line = EXTI_Line8;            
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;   
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising; 
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;             
    EXTI_Init(&EXTI_InitStructure);                       
    
    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;              // 选择EXTI线0中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x00;    // 设置抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0x00;           // 设置响应优先级
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;                 // 使能中断通道
    NVIC_Init(&NVIC_InitStructure);                                 // 初始化NVIC
}



/**
  * 函数名：Key_GetNum
  * 参数：void
  * 返回值：uint8_t - 按下按键的键码值，范围：0~2，返回0代表没有按键按下
  * 具体解释：该函数用于检测GPIOB的PB13、PB14、PB15引脚是否为高电平，如果为高电平，则表示对应的按键被按下。
  *           函数会等待按键释放，并返回对应的键码值（Enter、Up或Down）。这是一个阻塞式操作，直到按键松手。
  */
uint8_t Key_GetNum(void)
{
    uint16_t KeyNum = 6;
	
	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == 1)			
	{
		Delay_fnms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == 1);	
		Delay_fnms(20);
		KeyNum = Enter;												
	}
	
	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14) == 1)			
	{
		Delay_fnms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14) == 1);	
		Delay_fnms(20);
		KeyNum = Up;												
	}
	
    if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15) == 1)			
	{
		Delay_fnms(20);
		while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15) == 1);	
		Delay_fnms(20);
		KeyNum = Down;												
	}
	return KeyNum;			
}
