#include "stm32f10x.h"                  // Device header
#include <time.h>
#include "usart.h"
#include "head.h"
#include "Oled.h"
#include "Delay.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
/*部分引用自江科大*/
extern QueueHandle_t QueueGetHandler;

uint16_t MyRTC_Time[] = {2024, 3, 1, 18, 5, 00};	
void MyRTC_Init(void);
void MyRTC_SetTime(void);				//函数声明
void MyRTC_ReadTime(void);


void SetTime_w()
{
    /* 每一步在 OLED 第3行给出明确的待输入字段:否则进入修改后屏幕上只有停止走动的旧时间,
       看起来和"没进去"完全一样(串口提示在 HEX 显示模式下也看不清) */
    static const char * const step_name[8] = {
        "Edit:Start     ",   //0:入口指令 0x05 本身
        "Edit:Year low  ",   //1:年份低两位(BCD)
        "Edit:Year high ",   //2:年份高两位(BCD)
        "Edit:Month     ",   //3:月
        "Edit:Day       ",   //4:日
        "Edit:Hour      ",   //5:时
        "Edit:Minute    ",   //6:分
        "Edit:Second    "    //7:秒,收完自动写RTC
    };
		
    uint8_t key_count = 0; // 用于跟踪接收到的按键数量
    uint16_t wait;         // 等待上位机输入的超时计数(单位:1ms)
    uint8_t rx_byte = 0;   // 从队列取出的原始字节
    uint16_t data = 0;     // BCD 转码用,局部变量(原全局 data 已删除)

    while(1)
    {   
        // 检查是否完成时间设置
        if(global_var == 9)
        {
						OLED_ShowString(4,5,"Exiting...");
						UsartPrintf(USART_DEBUG,"  The modification was successful!\r\n");
						vTaskDelay(pdMS_TO_TICKS(500)); // 等待一段时间
						OLED_ShowString(4,5,"          ");
            break; // 退出循环
        }
				OLED_ShowString(3,1,(char *)step_name[key_count]);   //提示本字节对应字段
				wait = 30000;
				while((wait > 0) && (g_exit_req == 0) && (xQueueReceive(UsartTimeHandler,&rx_byte,pdMS_TO_TICKS(1)) != pdTRUE))
				{
				    wait--;                  //队列接收带1ms超时,等效原来的 vTaskDelay(1)
				}
				if(g_exit_req)          //物理退出键或蓝牙 0:立刻返回,不清标志,由 Time_Show_f 完成退出
				{
				    return;
				}
				if(wait == 0)                //超时未收到数据:放弃本次时间设置并清标志,避免整机死等
				{
				    global_var = 0;
				    return;
				}
				data = rx_byte;              //把队列取到的字节交给下面的 BCD 转码
				if(key_count >= 1)           //回显原始字节,确认上位机发的是 BCD 值而不是 ASCII
				{
				    OLED_ShowString(4,1,"In:0x");
				    OLED_ShowHexNum(4,6,rx_byte,2);
				    OLED_ShowString(4,8,"        ");
				}
        // 根据接收到的数据更新时间
        switch(key_count)
        {
						case 0:
								UsartPrintf(USART_DEBUG,"  Input year's last two bits\r\n");
                break;
            case 1:
                MyRTC_Time[0] = data/16*10+data%16;
								UsartPrintf(USART_DEBUG,"  What you entered is:%d and Input year's front two bits\r\n",MyRTC_Time[0]);
                break;
            case 2:
                MyRTC_Time[0] += (data/16*10+data%16)*100;
								UsartPrintf(USART_DEBUG,"  What you entered is:%d and Input month\r\n",MyRTC_Time[0]);
                break;
            case 3:
                MyRTC_Time[1] = (data/16*10+data%16);
								UsartPrintf(USART_DEBUG,"  What you entered is:%d and Input days\r\n",MyRTC_Time[1]);
                break;
            case 4:
                MyRTC_Time[2] = (data/16*10+data%16);
								UsartPrintf(USART_DEBUG,"  What you entered is:%d and Input hour\r\n",MyRTC_Time[2]);
                break;
            case 5:
                MyRTC_Time[3] = (data/16*10+data%16);
								UsartPrintf(USART_DEBUG,"  What you entered is:%d and Input Minute\r\n",MyRTC_Time[3]);
                break;
            case 6:						
                MyRTC_Time[4] = (data/16*10+data%16);
								UsartPrintf(USART_DEBUG,"  What you entered is:%d and Input second and will exit after\r\n",MyRTC_Time[4]);
								break;
            case 7:
                MyRTC_Time[5] = (data/16*10+data%16);
								UsartPrintf(USART_DEBUG,"  What you entered is:%d \r\n",MyRTC_Time[5]);
                global_var = 9; // 设置退出标志
                break;
            default:
                // 不应该到达这里
                break;
        }
        // 增加按键计数器
        key_count++;
        if(key_count >= 8)
        {
            key_count = 0; // 重置按键计数器
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // 等待一段时间
    }
    MyRTC_SetTime(); // 设置RTC时间
		MyRTC_ReadTime();// 立刻把新时间回读进数组并刷新显示队列,不必等 Time_Work_f 下一拍
		global_var = 0;  // 修改流程结束,清回普通显示态,避免 9 残留到下一轮
		
    return;
}



/**
  * 函    数：RTC初始化
  * 参    数：无
  * 返 回 值：无
  */
void MyRTC_Init(void)
{
	/*开启时钟*/
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);		//开启PWR的时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);		//开启BKP的时钟
	
	/*备份寄存器访问使能*/
	PWR_BackupAccessCmd(ENABLE);							//使用PWR开启对备份寄存器的访问
	
	if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5)			//通过写入备份寄存器的标志位，判断RTC是否是第一次配置
															//if成立则执行第一次的RTC配置
	{
		RCC_LSEConfig(RCC_LSE_ON);							//开启LSE时钟
		{ uint32_t lse_tout = 0x2000000; while((RCC_GetFlagStatus(RCC_FLAG_LSERDY) != SET) && (--lse_tout)); }	//LSE起振等待加超时上限:原无超时死等会在晶振故障时把初始化永久卡死
		
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);				//选择RTCCLK来源为LSE
		RCC_RTCCLKCmd(ENABLE);								//RTCCLK使能
		
		RTC_WaitForSynchro();								//等待同步
		RTC_WaitForLastTask();								//等待上一次操作完成
		
		RTC_SetPrescaler(32768 - 1);						//设置RTC预分频器，预分频后的计数频率为1Hz
		RTC_WaitForLastTask();								//等待上一次操作完成
		
		MyRTC_SetTime();									//设置时间，调用此函数，全局数组里时间值刷新到RTC硬件电路
		
		BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);			//在备份寄存器写入自己规定的标志位，用于判断RTC是不是第一次执行配置
	}
	else													//RTC不是第一次配置
	{
		RTC_WaitForSynchro();								//等待同步
		RTC_WaitForLastTask();								//等待上一次操作完成
	}
}

//如果LSE无法起振导致程序卡死在初始化函数中
//可将初始化函数替换为下述代码，使用LSI当作RTCCLK
//LSI无法由备用电源供电，故主电源掉电时，RTC走时会暂停
/* 
void MyRTC_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP, ENABLE);
	
	PWR_BackupAccessCmd(ENABLE);
	
	if (BKP_ReadBackupRegister(BKP_DR1) != 0xA5A5)
	{
		RCC_LSICmd(ENABLE);
		while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != SET);
		
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
		RCC_RTCCLKCmd(ENABLE);
		
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();
		
		RTC_SetPrescaler(40000 - 1);
		RTC_WaitForLastTask();
		
		MyRTC_SetTime();
		
		BKP_WriteBackupRegister(BKP_DR1, 0xA5A5);
	}
	else
	{
		RCC_LSICmd(ENABLE);				//即使不是第一次配置，也需要再次开启LSI时钟
		while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != SET);
		
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
		RCC_RTCCLKCmd(ENABLE);
		
		RTC_WaitForSynchro();
		RTC_WaitForLastTask();
	}
}*/

/**
  * 函    数：RTC设置时间
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，全局数组里时间值将刷新到RTC硬件电路
  */
void MyRTC_SetTime(void)
{
	time_t time_cnt;		//定义秒计数器数据类型
	struct tm time_date = {0};	//定义日期时间数据类型:必须清零,mktime 会读 tm_isdst,栈上残留值会带来 ±1 小时偏移
	
	time_date.tm_year = MyRTC_Time[0] - 1900;		//将数组的时间赋值给日期时间结构体
	time_date.tm_mon = MyRTC_Time[1] - 1;
	time_date.tm_mday = MyRTC_Time[2];
	time_date.tm_hour = MyRTC_Time[3];
	time_date.tm_min = MyRTC_Time[4];
	time_date.tm_sec = MyRTC_Time[5];
	
	time_cnt = mktime(&time_date);	//调用mktime函数，将日期时间转换为秒计数器格式
																								
	
	RTC_SetCounter(time_cnt);						//将秒计数器写入到RTC的CNT中
	RTC_WaitForLastTask();							//等待上一次操作完成
}

/**
  * 函    数：RTC读取时间
  * 参    数：无
  * 返 回 值：无
  * 说    明：调用此函数后，RTC硬件电路里时间值将刷新到全局数组
  */
void MyRTC_ReadTime(void)
{
	time_t time_cnt;		//定义秒计数器数据类型
	struct tm time_date;	//定义日期时间数据类型
	
    
	time_cnt = RTC_GetCounter();		//读取RTC的CNT，获取当前的秒计数器
	
	time_date = *localtime(&time_cnt);				//使用localtime函数，将秒计数器转换为日期时间格式
	
	MyRTC_Time[0] = time_date.tm_year + 1900;		//将日期时间结构体赋值给数组的时间
	MyRTC_Time[1] = time_date.tm_mon + 1;
	MyRTC_Time[2] = time_date.tm_mday;
	MyRTC_Time[3] = time_date.tm_hour;
	MyRTC_Time[4] = time_date.tm_min;
	MyRTC_Time[5] = time_date.tm_sec;
    
    xQueueOverwrite(QueueGetHandler,MyRTC_Time);
    

    return;
}
