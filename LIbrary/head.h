#ifndef __HEAD_H_
#define __HEAD_H_
/**
  ******************************************************************************
  * @file    head.h
  * @author  张照军
  * @version V1.0.0
  * @date    2024年4月23日14:22:13
  * @brief   extern 变量声明
  ******************************************************************************
  */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern int global_var;

/* 串口接收链路队列:ISR 只投递字节,去向由常驻任务 Usart_Rx_f 决定 */
extern QueueHandle_t UsartRxHandler;     /* USART1 ISR -> Usart_Rx_f */
extern QueueHandle_t UsartMenuHandler;   /* Usart_Rx_f  -> Menu_handling_f */
extern QueueHandle_t UsartTimeHandler;   /* Usart_Rx_f  -> SetTime_w */
#endif
