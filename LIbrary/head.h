#ifndef __HEAD_H_
#define __HEAD_H_
/**
  ******************************************************************************
  * @file    head.h
  * @author  ���վ�
  * @version V1.0.0
  * @date    2024��4��23��14:22:13
  * @brief   extern ��������
  ******************************************************************************
  */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

extern int global_var;
extern volatile uint8_t g_exit_req;   /* 退出请求:ISR/蓝牙置位,页面任务消费后清零 */

/* ���ڽ�����·����:ISR ֻͶ���ֽ�,ȥ���ɳ�פ���� Usart_Rx_f ���� */
extern QueueHandle_t UsartRxHandler;     /* USART1 ISR -> Usart_Rx_f */
extern QueueHandle_t UsartMenuHandler;   /* Usart_Rx_f  -> Menu_handling_f */
extern QueueHandle_t UsartTimeHandler;   /* Usart_Rx_f  -> SetTime_w */
#endif
