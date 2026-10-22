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
#include "event_groups.h"

extern int global_var;

/* 页面/退出事件组(B2):退出消息从"全局标志 g_exit_req"改为位广播,
   并顺带用页面位取代 Menu 与 Show 之间的 vTaskResume/vTaskSuspend 切换。
   可用位为 bit0~bit23(高8位被内核控制位占用),这里用 5 位。 */
#define EVT_EXIT        ( ( EventBits_t ) 0x0001U )   /* 退出请求:广播给全部 Show 任务 */
#define EVT_PAGE_TIME   ( ( EventBits_t ) 0x0002U )   /* 时间页在前台 */
#define EVT_PAGE_STEP   ( ( EventBits_t ) 0x0004U )   /* 计步页在前台 */
#define EVT_PAGE_DHT    ( ( EventBits_t ) 0x0008U )   /* 温湿度页在前台 */
#define EVT_PAGE_MAX    ( ( EventBits_t ) 0x0010U )   /* 血氧页在前台 */
#define EVT_PAGE_ALL    ( EVT_PAGE_TIME | EVT_PAGE_STEP | EVT_PAGE_DHT | EVT_PAGE_MAX )

extern EventGroupHandle_t PageEventGroup;

/* ���ڽ�����·����:ISR ֻͶ���ֽ�,ȥ���ɳ�פ���� Usart_Rx_f ���� */
extern QueueHandle_t UsartRxHandler;     /* USART1 ISR -> Usart_Rx_f */
extern QueueHandle_t UsartMenuHandler;   /* Usart_Rx_f  -> Menu_handling_f */
extern QueueHandle_t UsartTimeHandler;   /* Usart_Rx_f  -> SetTime_w */
#endif
