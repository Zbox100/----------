#include "stm32f10x.h"                // STM32F103系列芯片的标准外设库核心头文件
#include "stm32f10x_it.h"             // STM32F103系列芯片的中断服务函数定义头文件
#include "string.h"                   // C语言标准库字符串操作头文件
#include "Delay.h"                    // 自定义延时函数模块头文件
#include "Oled.h"                     // OLED显示屏驱动模块头文件
#include "usart.h"                    // 自定义串口通信（USART）模块头文件
#include "MyRTC.h"                    // 自定义实时时钟（RTC）驱动封装模块头文件
#include "Key.h"                      // 自定义按键输入驱动模块头文件
#include "head.h"                     // 自定义公共全局头文件（封装通用定义与宏）
#include "adxl345.h"                  // ADXL345三轴加速度传感器驱动模块头文件
#include "DHT11.h"                    // DHT11温湿度传感器驱动模块头文件
#include "max30102.h"                 // MAX30102心率血氧光电传感器驱动模块头文件
#include "algorithm.h"                // 传感器数据处理算法（如心率、血氧计算）模块头文件
#include "xiic.h"                     // 自定义软件I2C通信模块头文件
#include "blood.h"                    // 血氧心率（生理参数）数据结构与处理模块头文件

//全局变量
int rxBuffer;
int global_var = 0;
uint8_t Flag_U;
uint16_t data;

// FreeRTOS核心头文件：提供FreeRTOS基础类型、宏定义与调度器核心支撑接口
#include "FreeRTOS.h"
// FreeRTOS任务管理头文件：包含任务创建、挂起、恢复、删除等任务控制相关接口
#include "task.h"
// FreeRTOS队列管理头文件：包含队列创建、数据发送/接收等任务间通信相关接口
#include "queue.h"
/////////////////////全局变量//////////////////////////////////
uint16_t KeyExit_R = 0; 
uint8_t IN = 0;
extern BloodData g_blooddata;
/////////////////////专用功能任务//////////////////////////////
TaskHandle_t Time_Work_fHandler;
TaskHandle_t Time_Show_fHandler;

TaskHandle_t Adxl345_Work_fHandler;
TaskHandle_t Adxl345_Show_fHandler;

TaskHandle_t DTH11_Work_fHandler;
TaskHandle_t DTH11_Show_fHandler;

TaskHandle_t Max30102_Work_fHandler;
TaskHandle_t Max30102_Show_fHandler;
/////////////////////通用功能任务//////////////////////////////
TaskHandle_t Menu_handling_fHandler;
TaskHandle_t Start_MyTaskListHandler;
TaskHandle_t Time_modification_fHandler;
TaskHandle_t Key_GetNum_fHandler;
TaskHandle_t beep_Work_fHandler;
////////////////////队列实现///////////////////////////////////
QueueHandle_t QueueGetHandler  =NULL;
QueueHandle_t KeyGetHandler    =NULL;
QueueHandle_t KeyExitHandler   =NULL;
QueueHandle_t ADX1345Handler   =NULL;
QueueHandle_t DTH11Handler     =NULL;
QueueHandle_t Max30102Handler  =NULL;

void Adxl345_Work_f(void*arg)
{	

        volatile uint32_t time = 0;                     // ms计时变量 
        uint32_t Normal_num=0;                          //正常次数
        unsigned char ReadAdxl345 = 0;	                    //读取adxl345数据
        unsigned int Number_of_detections = 0;			//检测次数
        unsigned char Flag = 0;                         //碰撞检测标志
        float buf = 0;                                 //缓冲区
   	
	while(1)
	{
        if(time==720000)
		{
			time=0;
			ReadAdxl345=1;
		}
		else
		{
				time++;
		}
        
		if(ReadAdxl345== 1)                                                     
		{
				ReadAdxl345= 0; 
                ReadData_x();  						                            //三轴检测函数
				buf = temp_Z - buf;
				if(Number_of_detections&&(buf >25)){Normal_num++;}              //正常次数++
				else Number_of_detections++;
				buf = temp_Z ;
				xQueueSend(ADX1345Handler,&Normal_num,0);
              
				if(temp_X < 1 || temp_X > 200000) temp_X = 0;
				if(temp_Y < 1 || temp_Y > 200000) temp_Y = 0;
 
				if(temp_X>700||temp_Y>700){Flag++;}
				if((Flag>0)&&IN){vTaskResume(beep_Work_fHandler);Flag=0;IN = 0;} 
                            
         vTaskDelay(100);
    }   
}
}

void Adxl345_Show_f( void * arg )
{
    uint8_t dis_buf_R;
    while(1)
    {
        xQueueReceiveFromISR(KeyExitHandler,&KeyExit_R,NULL);
        if((KeyExit_R == 1)||(global_var==1))
        {
            taskENTER_CRITICAL();
            KeyExit_R = 0;global_var = 0;
            GPIO_ResetBits(GPIOB, GPIO_Pin_12);
			GPIO_SetBits(GPIOB, GPIO_Pin_1);
            OLED_Clear();
            vTaskResume(Menu_handling_fHandler);vTaskResume(Key_GetNum_fHandler);vTaskSuspend(beep_Work_fHandler);vTaskSuspend(NULL);
            taskEXIT_CRITICAL();
        }
        OLED_Clear();
        xQueueReceive(ADX1345Handler,&dis_buf_R,portMAX_DELAY);
        OLED_ShowString(1, 1, "***************");
        OLED_ShowString(2, 1, "Bushu:");
        OLED_ShowNum(2,7,dis_buf_R,4);
        OLED_ShowString(4, 1, "***************");
        vTaskDelay(100);
    }
}
     
void Time_Work_f( void * arg ) //时间读取任务
{

    while(1)
    {
    MyRTC_ReadTime();                                
    vTaskDelay(1000);  
    }
          
}
void Time_Show_f( void*arg ) //时间显示任务任务
{ 
    
    uint16_t MyRTC_R[6];
       
    while(1)
    {
        if(global_var==2)
        {
        vTaskSuspendAll();
        taskENTER_CRITICAL();
        SetTime_w();
        taskEXIT_CRITICAL();
        xTaskResumeAll();
        }
        
        xQueueReceiveFromISR(KeyExitHandler,&KeyExit_R,NULL);
        if((KeyExit_R == 1)||(global_var==1))
        {taskENTER_CRITICAL();
        KeyExit_R = 0;global_var = 0;OLED_Clear();vTaskResume(Menu_handling_fHandler);vTaskResume(Key_GetNum_fHandler);vTaskSuspend(NULL);
        taskEXIT_CRITICAL();}
        OLED_Clear();
        OLED_ShowString(1, 1, "Date:    -  -  ");
        OLED_ShowString(2, 1, "Time:  :  :  ");
        OLED_ShowString(3, 1, "Key 5 to amend  ");
        OLED_ShowString(4, 1, "***************");
        xQueueReceive(QueueGetHandler,MyRTC_R,portMAX_DELAY);
        OLED_ShowNum(1, 6, MyRTC_R[0], 4);     // 年
        OLED_ShowNum(1, 11, MyRTC_R[1], 2);    // 月
        OLED_ShowNum(1, 14, MyRTC_R[2], 2);    // 日
        OLED_ShowNum(2, 6, MyRTC_R[3], 2);     // 时
        OLED_ShowNum(2, 9, MyRTC_R[4], 2);     // 分
        OLED_ShowNum(2, 12, MyRTC_R[5], 2);    // 秒                  
        vTaskDelay(1000);  
    }
               
}

void DTH11_Work_f(void*arg)
{  
    
    /* 主循环 */
    while(1)
    {
		DHT11_SCAN();     
        vTaskDelay(100);		
    }
}

void DTH11_Show_f(void*arg)
{
    DHT11_Data_TypeDef DHT11_Data_R;

    while(1)
    {   
        xQueueReceiveFromISR(KeyExitHandler,&KeyExit_R,NULL);
        if((KeyExit_R == 1)||(global_var==1))
        {
            taskENTER_CRITICAL();
            KeyExit_R = 0;global_var = 0;          
            OLED_Clear();
            vTaskResume(Menu_handling_fHandler);
            vTaskResume(Key_GetNum_fHandler);
            vTaskSuspend(NULL);
            taskEXIT_CRITICAL();
        }
        OLED_Clear();
        xQueueReceive(DTH11Handler,&DHT11_Data_R,portMAX_DELAY);
        OLED_ShowString(1, 1, "***************");
        OLED_ShowString(2,3,"Humi:");
        OLED_ShowString(3,3,"Temp:");
        OLED_ShowNum(2,9,DHT11_Data_R.humi_int,2);
        OLED_ShowNum(2,12,DHT11_Data_R.humi_deci,2);
        OLED_ShowNum(3,9,DHT11_Data_R.temp_int,2);
        OLED_ShowNum(3,12,DHT11_Data_R.temp_deci,2);
        OLED_ShowString(4, 1, "***************");
        vTaskDelay(100);
    }

        
}            

void Max30102_Work_f(void*arg)
{

    while(1)
    {   
        for(int i = 0;i < 128;i++) 
        {   
            while(MAX30102_INTPin_Read()==0)
            {          
                // 读取FIFO               
                max30102_read_fifo();
            }
        }
        blood_Loop();        // 获取血氧和心率
        vTaskDelay(100);
        
    }
    
}	

void Max30102_Show_f(void*arg)
{
   while(1)
   {
        xQueueReceiveFromISR(KeyExitHandler,&KeyExit_R,NULL);
        if((KeyExit_R == 1)||(global_var==1))
        {
            taskENTER_CRITICAL();
            KeyExit_R = 0;global_var = 0;            
            OLED_Clear();
            vTaskResume(Menu_handling_fHandler);
            vTaskResume(Key_GetNum_fHandler);
            vTaskSuspend(NULL);
            taskEXIT_CRITICAL();
        }  
    xQueueReceive(Max30102Handler,&g_blooddata,portMAX_DELAY);
    OLED_Clear();
    OLED_ShowString(1, 1, "***************");
    OLED_ShowString(2,4,"SpO2: ");
	OLED_ShowNum(2,9,g_blooddata.SpO2,3);
    OLED_ShowString(3,4,"Heart:");
	OLED_ShowNum(3,10,g_blooddata.heart,3);
    OLED_ShowString(4, 1, "***************");
    
    vTaskDelay(100);
   }
}

void Menu_handling_f( void * arg )   //菜单处理任务
{
    UsartPrintf(USART1, "  Please Input num of Func \r\n");
    UsartPrintf(USART1, "  1-Time 2-Bushu\r\n");
    UsartPrintf(USART1, "  3-Wenshi 4-Xueyang\r\n");
    UsartPrintf(USART1, "  0-Exit");
    char Menu[4][15] = {"Time    ","Bushu   ","Wenshi  ","Xueyang "}; // 菜单选项
    uint8_t operate = 0;
    uint8_t menu_index = 0;
    uint8_t i = 1;
    while(1)
    {    
    OLED_ShowString(1, 1, "Enter menu num:");
    OLED_ShowString(2,1,"Choose:"); 
        OLED_ShowString(3, 1, "1:Exit  2:Enter ");
        OLED_ShowString(4, 1, "3:Up    4:Next  ");
    
    xQueueReceive(KeyGetHandler,&operate,portMAX_DELAY); 
      
    /*蓝牙功能选择*/	
        switch(rxBuffer)
    { 
        /* 执行当前功能，再次按下退出功能 */
        case 1 : 
            {   
                taskENTER_CRITICAL();
                vTaskResume(Time_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                taskEXIT_CRITICAL();}
            break;
        case 2 : 
            {   
                taskENTER_CRITICAL();
                IN = 1;
                vTaskResume(Adxl345_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                taskEXIT_CRITICAL();
                }                 
            break;
        case 3 : 
           {   
                taskENTER_CRITICAL();
                vTaskResume(DTH11_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                taskEXIT_CRITICAL();
                }   
            break;
        case 4 : 
            {   
                taskENTER_CRITICAL();
                vTaskResume(Max30102_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                taskEXIT_CRITICAL();
                }      
            break;
        default :
            break;
    }	
       /*按键功能选择*/
      switch(operate)
        { 
            
            /* 执行当前功能，再次按下退出功能 */
            case 1 : 
                if(menu_index == 0)
                {   taskENTER_CRITICAL();
                    vTaskResume(Time_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                    taskEXIT_CRITICAL();}
                    
                else if(menu_index == 1)
                {   taskENTER_CRITICAL();
                    IN = 1;
                    vTaskResume(Adxl345_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                    taskEXIT_CRITICAL();
                }
                    
                else if(menu_index == 2)
                {   taskENTER_CRITICAL();
                    vTaskResume(DTH11_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                    taskEXIT_CRITICAL();
                }
                else if(menu_index == 3)
                {   taskENTER_CRITICAL();
                    vTaskResume(Max30102_Show_fHandler);vTaskSuspend(Key_GetNum_fHandler);vTaskSuspend(NULL);
                    taskEXIT_CRITICAL();
                }
                break;
            case 2 : 
                if (menu_index == 0) {menu_index = 0; }
                else menu_index--;         // 选择上一个功能
                break;
            case 3 : 
                menu_index++;         // 选择下一个功能
                if (menu_index > 3) {menu_index = 3; }
                break;
            default :
                break;
        }        
        OLED_ShowString(2, 8, Menu[menu_index]); // 显示当前菜单选项
        if(i == 1)
        {
            OLED_Clear();
            OLED_ShowString(2, 4, "Loading...");  
            i--;
        }
        
        vTaskDelay(100);
    }
    

}

void Key_GetNum_f(void *arg)  //时间修改任务
{

    uint8_t operate;
    while(1)
    {   
        operate = Key_GetNum();
        xQueueSend(KeyGetHandler,&operate,portMAX_DELAY);    
        vTaskDelay(100);
    }
    
    

}
void beep_Work_f(void* arg)
{	
    while(1)
    {	
    UsartPrintf(USART1, "  FALLING DOWN! \r\n");    
    OLED_ShowString(3, 2, "FALLING DOWN!");
	GPIO_ResetBits(GPIOB, GPIO_Pin_12);						
	GPIO_ResetBits(GPIOB, GPIO_Pin_1);						
	vTaskDelay(100);
    OLED_ShowString(3, 2, "FALLING DOWN!");        
	GPIO_SetBits(GPIOB, GPIO_Pin_12);							
	GPIO_SetBits(GPIOB, GPIO_Pin_1);
	vTaskDelay(100);																
		
    }    															
}

void Start_MyTaskList(void *arg)  //任务启动任务
{
    taskENTER_CRITICAL();
    xTaskCreate(Time_Work_f,"Time_Work_f",128,NULL ,4,&Time_Work_fHandler);
    xTaskCreate(Time_Show_f,"Time_Show_f",128,NULL ,4,&Time_Show_fHandler);
    xTaskCreate(Adxl345_Work_f,"Adxl345_Work_f",128,NULL ,4,&Adxl345_Work_fHandler);
    xTaskCreate(Adxl345_Show_f,"Adxl345_Show_f",128,NULL ,4,&Adxl345_Show_fHandler);
    xTaskCreate(DTH11_Work_f,"DTH11_Work_f",224,NULL ,4,&DTH11_Work_fHandler);
    xTaskCreate(DTH11_Show_f,"DTH11_Show_f",224,NULL ,4,&DTH11_Show_fHandler);
    xTaskCreate(Max30102_Work_f,"Max30102_Work_f",224,NULL ,4,&Max30102_Work_fHandler);
    xTaskCreate(Max30102_Show_f,"Max30102_Show_f",224,NULL ,4,&Max30102_Show_fHandler);
    
    xTaskCreate(Menu_handling_f,"Menu_handling_f",128,NULL ,4,&Menu_handling_fHandler);
    xTaskCreate(Key_GetNum_f,"Key_GetNum_f",128,NULL ,4,&Key_GetNum_fHandler);
    xTaskCreate(beep_Work_f,"beep_Work_f",128,NULL ,4,&beep_Work_fHandler);
    
    
    vTaskSuspend(Time_Show_fHandler);
    vTaskSuspend(Adxl345_Show_fHandler);
    vTaskSuspend(DTH11_Show_fHandler);
    vTaskSuspend(Max30102_Show_fHandler);
    vTaskSuspend(beep_Work_fHandler);

    taskEXIT_CRITICAL();
    vTaskDelete(NULL);

}
int main(void)
{
    BaseType_t xReceive = pdPASS;
    DHT11_Data_TypeDef DHT11_Data_R2;
    BloodData Trans;
    /* 初始化部分 */
    OLED_Init();                                    // 初始化OLED显示器
    MyRTC_Init();                                   // 初始化实时时钟
    Key_Init();                                     // 初始化按键
    Usart1_Init(115200);                            // 初始化USART1串口，自定义的输入输出函数
    EXTI8_Config();                                 // 配置外部中断   
    Init_ADXL345();
    DHT11_GPIO_Config();
    IIC_GPIO_INIT();                                // I2C GPIO初始化
    MAX30102_GPIO();                                // MAX30102 GPIO初始化
    Max30102_reset();                               // MAX30102复位
    MAX30102_Config();                              // MAX30102配置

    /*任务运行部分，使用动态创建任务*/
        
    
    QueueGetHandler = xQueueCreate(6, sizeof(uint16_t) * 6);
    KeyGetHandler   = xQueueCreate(1, sizeof(uint8_t)  * 1);
    KeyExitHandler  = xQueueCreate(1, sizeof(uint8_t)  * 1); 
    ADX1345Handler  = xQueueCreate(1, sizeof(uint32_t)  * 1); 
    DTH11Handler    = xQueueCreate(1, sizeof(&DHT11_Data_R2)  * 1); 
    Max30102Handler = xQueueCreate(1, sizeof(&Trans)  * 1); 
    
    xReceive = xTaskCreate(Start_MyTaskList,"Start_MyTaskList",128,NULL ,2,&Start_MyTaskListHandler);
    /* 启动任务调度 */           
   if(pdPASS == xReceive)
    vTaskStartScheduler();   /* 启动任务，开启调度 */
   else
    return -1;   
    while(1);
    
}

