#ifndef __KEY_H
#define __KEY_H
/**
  ******************************************************************************
  * @file    Key.h
  * @author  张照军
  * @version V1.0.0
  * @date    2024年4月23日14:23:20
  * @brief   按键与按键中断相关函数配置
  ******************************************************************************
  */
void Key_Init(void);
void EXTI8_Config(void);
uint8_t Key_GetNum(void);

#endif
