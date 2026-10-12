#include "stm32f10x.h"                // STM32F103系列芯片的标准外设库核心头文件
#include "stm32f10x_it.h"             // STM32F103系列芯片的中断服务函数定义头文件
#include "string.h"                   // C语言标准库字符串操作头文件
#include "math.h"                     // 数学库(sqrtf/fabsf,用于合加速度与差分计算)
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
int global_var = 0;

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
static uint8_t beep_Cnt = 0;    //beep 报警已翻转轮数:达到上限后自挂起,防止误触发后无限报警
TaskHandle_t Usart_Rx_fHandler;
////////////////////队列实现///////////////////////////////////
QueueHandle_t QueueGetHandler  =NULL;
QueueHandle_t KeyGetHandler    =NULL;
QueueHandle_t KeyExitHandler   =NULL;
QueueHandle_t ADX1345Handler   =NULL;
QueueHandle_t DTH11Handler     =NULL;
QueueHandle_t Max30102Handler  =NULL;
QueueHandle_t UsartRxHandler   =NULL;   //USART1 ISR -> Usart_Rx_f
QueueHandle_t UsartMenuHandler =NULL;   //Usart_Rx_f  -> Menu_handling_f
QueueHandle_t UsartTimeHandler =NULL;   //Usart_Rx_f  -> SetTime_w

void Adxl345_Work_f(void*arg)
{	
        uint32_t Normal_num=0;                          //正常次数
        unsigned int Number_of_detections = 0;			//检测次数
        // 在任务局部声明（替换原来的 unsigned char Flag） 
        uint8_t impact_x = 0 ; // X 轴冲击标志 
        uint8_t impact_y = 0 ; // Y 轴冲击标志
        float buf = 0;                                 //缓冲区(上一帧合加速度)
        float mag = 0;                                 //三轴合加速度幅值,静止时约为1000mg
        float diff = 0;                                //合加速度差分绝对值
        uint8_t step_lock = 0;                         //计步锁定标志:一次波动只计1步,信号回落才解锁
        TickType_t lastStepTick = 0;                   //上一次计步的时刻(tick)
        TickType_t xLastWakeTime = xTaskGetTickCount(); //记录首次唤醒时刻,建立绝对时间基准网格
        const TickType_t xPeriod = pdMS_TO_TICKS(50);   //50ms 精确采样周期
        // TickType_t t_last = 0;                          //上一次循环起始的tick
        // uint32_t tick_sum = 0;                          //累计周期(tick)
        // uint16_t cnt = 0;                               //累计循环次数

	while(1)
	{
        // TickType_t t_now;
        vTaskDelayUntil(&xLastWakeTime, xPeriod);       //按绝对时间网格唤醒,补偿执行耗时,保证 50ms 等间隔

        /*周期实测(已验证,故停用):记录相邻两次唤醒的tick差,累加100次再打印一次。
          每轮都打印会显著拉长被测周期,所以必须攒批输出。
          实测结果:5次输出4次50ms+1次49ms(±1 tick量化),无丢拍,周期正常 */
        // t_now = xTaskGetTickCount();
        // if(t_last != 0)                                 //首轮没有可比对象,跳过
        // {
        //     tick_sum += (t_now - t_last);               //tick是32位无符号,相减自然处理回绕
        //     if(++cnt >= 100)
        //     {
        //         UsartPrintf(USART1, "period avg=%u ms\r\n", tick_sum / 100);
        //         tick_sum = 0;
        //         cnt = 0;
        //     }
        // }
        // t_last = t_now;

        ReadData_x();  						            //三轴检测函数
        mag = sqrtf(temp_X*temp_X + temp_Y*temp_Y + temp_Z*temp_Z);  //三轴合加速度,对握持方向不敏感
        diff = fabsf(mag - buf);                               //合加速度差分绝对值
        //      UsartPrintf(USART1, "mag=%.0f diff=%.0f\r\n", mag, diff); //调试打印:128字栈会被其256B缓冲+vprintf击穿导致死机,已停用
        if(Number_of_detections&&(diff > 150)&&(step_lock == 0)){     //差分超过触发阈值且本次波动尚未计步
            if((xTaskGetTickCount() - lastStepTick) >= pdMS_TO_TICKS(400)){  //距上次计步至少400ms
                Normal_num++;                                      //正常次数++
                lastStepTick = xTaskGetTickCount();                //记录本次计步时刻
                step_lock = 1;                                     //锁定,信号回落前不再计步
            }
        }
        else Number_of_detections++;
        if(diff < 30){step_lock = 0;}                              //迟滞解锁:差分回落到30以下才解除锁定
        buf = mag;
        xQueueSend(ADX1345Handler,&Normal_num,0);
      
        if(temp_X < 1 || temp_X > 200000) temp_X = 0;
        if(temp_Y < 1 || temp_Y > 200000) temp_Y = 0;

        /* ① 逐轴检测：各自独立标志 */ 
        impact_x = (temp_X > 700 ) ? 1 : 0 ;
        impact_y = (temp_Y > 700 ) ? 1 : 0 ;

        /* ② 报警触发：任一轴冲击且使能 */
        if ((impact_x || impact_y) && IN) {
            beep_Cnt = 0;                       //新一轮报警从0开始计数,保证每次触发都是完整时长
            vTaskResume(beep_Work_fHandler);
            impact_x = 0 ;
            impact_y = 0 ;
            IN = 0 ; // 触发一次后清除使能
        }
    }
}

void Adxl345_Show_f( void * arg )
{
    uint32_t dis_buf_R;
    while(1)
    {
        xQueueReceive(KeyExitHandler,&KeyExit_R,NULL);
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
        SetTime_w();
        }
        
        KeyExit_R = 0;      //先清零:非阻塞接收失败时会保留旧值,清掉避免上一轮的 1 误触发退出
        xQueueReceive(KeyExitHandler,&KeyExit_R,pdMS_TO_TICKS(50));
        if((KeyExit_R == 1)||(global_var==1))   //global_var==1 是蓝牙发 0 的退出路径,SetTime_w 提前返回时靠它退出
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
    TickType_t xLastWakeTime = xTaskGetTickCount(); //记录首次唤醒时刻,建立绝对时间基准网格
    const TickType_t xPeriod = pdMS_TO_TICKS(2000); //2s 采样周期:DHT11 手册要求两次读取间隔≥1s,过频会读到未刷新的旧值

    /* 主循环 */
    while(1)
    {
        DHT11_SCAN();     
        /* ---- 临时验收观测:idle 真实占比 ----
           空闲占比 = 本窗口内空闲任务的累计运行时间 / 本窗口总时间。
           两项都取本窗口的增量再相除,不受累计值回绕影响。
           窗口即本任务的 2s 周期;验收完成后随 configGENERATE_RUN_TIME_STATS 一起移除 */
        {
            static uint32_t last_idle  = 0;
            static uint32_t last_total = 0;
            static uint8_t  first_win  = 1;
            /* 运行时间统计的时基就是节拍计数(见 FreeRTOSConfig.h),
               ulTaskGetIdleRunTimeCounter() 即空闲任务累计占用的节拍数。
               该函数需 configGENERATE_RUN_TIME_STATS=1 且 INCLUDE_xTaskGetIdleTaskHandle=1 */
            uint32_t now_idle  = (uint32_t)ulTaskGetIdleRunTimeCounter();
            uint32_t now_total = (uint32_t)xTaskGetTickCount();
            if(!first_win)   //首窗没有可相减的基准,跳过
            {
                uint32_t idle_delta  = now_idle  - last_idle;
                uint32_t total_delta = now_total - last_total;
                uint32_t pct = (total_delta != 0U)
                             ? (uint32_t)(((uint64_t)idle_delta * 100U) / total_delta) : 0U;
                UsartPrintf(USART1, "idle=%u%%\r\n", (unsigned int)pct);
            }
            first_win  = 0;
            last_idle  = now_idle;
            last_total = now_total;
        }
        vTaskDelayUntil(&xLastWakeTime, xPeriod);    //按绝对时间网格唤醒,补偿执行耗时,保证 2s 等间隔
    }
}

void DTH11_Show_f(void*arg)
{
    DHT11_Data_TypeDef DHT11_Data_R;

    while(1)
    {   
        xQueueReceive(KeyExitHandler,&KeyExit_R,NULL);
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
        /* 同 Max30102_Show_f:DHT11_SCAN() 只在读成功时才往队列里送数据,模块未连接时
           队列永远为空,原 portMAX_DELAY 死等会让本任务无法响应退出请求,页面卡在"Exiting..."
           改为带超时接收,超时后回到循环顶部重新判断退出条件 */
        if(xQueueReceive(DTH11Handler,&DHT11_Data_R,pdMS_TO_TICKS(200)) == pdTRUE)
        {
            OLED_Clear();
            OLED_ShowString(1, 1, "***************");
            OLED_ShowString(2,3,"Humi:");
            OLED_ShowString(3,3,"Temp:");
            OLED_ShowNum(2,9,DHT11_Data_R.humi_int,2);
            OLED_ShowNum(2,12,DHT11_Data_R.humi_deci,2);
            OLED_ShowNum(3,9,DHT11_Data_R.temp_int,2);
            OLED_ShowNum(3,12,DHT11_Data_R.temp_deci,2);
            OLED_ShowString(4, 1, "***************");
        }
        vTaskDelay(100);
    }

        
}            

void Max30102_Work_f(void*arg)
{
    uint8_t online = 0;                              //模块在位标志:PART_ID校验通过才置1

    while(1)
    {   
        /* 在位检测:PART_ID(0xFF)正常应读回0x15。软件I2C读不到器件时会应答失败返回0,
           模块未连接时直接跳过本轮采样,避免进入等待INT引脚/FIFO的循环把调度器挂死 */
        online = (IIC_Read_Byte(MAX30102_Device_address, REG_PART_ID) == 0x15) ? 1 : 0;
        if(online == 0)
        {
            vTaskDelay(500);                         //模块不在位:每500ms复查一次,期间让出CPU
            continue;
        }

        /* 预热:连续读取128次FIFO。原实现 while(INT==0) 为无超时死等,
           模块异常或INT引脚浮空时会永久占用CPU(协作式调度下整机停摆);
           现给每轮加等待上限并每轮让出1ms */
        for(int i = 0;i < 128;i++) 
        {   
            uint16_t wait = 50;                      //单个样本最多连续读50次(MAX30102配置为50Hz,20ms出一个样本)
            while((MAX30102_INTPin_Read()==0)&&(wait > 0))
            {          
                // 读取FIFO               
                max30102_read_fifo();
                wait--;
            }
            vTaskDelay(1);                           //让出CPU 1ms,保证其它任务(含OLED绘制)能得到运行
        }
        blood_Loop();        // 获取血氧和心率
        vTaskDelay(100);
        
    }
    
}	

void Max30102_Show_f(void*arg)
{
   while(1)
   {
        xQueueReceive(KeyExitHandler,&KeyExit_R,NULL);
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
    /* 原为 xQueueReceive(...,portMAX_DELAY) 死等:模块未连接时采集任务不会往队列里送数据
       (Max30102_Work_f 在位检测不通过就直接跳过采样),本任务会永远阻塞在这里,
       既刷不出界面,也回不到循环顶部判断退出条件(KeyExit_R/global_var==1);
       而退出键中断已经先把屏幕清掉并写上"Exiting...",于是页面卡死在这行字上。
       改为带超时接收:超时也会回到循环顶部重新判断退出条件 */
    if(xQueueReceive(Max30102Handler,&g_blooddata,pdMS_TO_TICKS(200)) == pdTRUE)
    {
        OLED_Clear();
        OLED_ShowString(1, 1, "***************");
        OLED_ShowString(2,4,"SpO2: ");
        OLED_ShowNum(2,9,g_blooddata.SpO2,3);
        OLED_ShowString(3,4,"Heart:");
        OLED_ShowNum(3,10,g_blooddata.heart,3);
        OLED_ShowString(4, 1, "***************");
    }
    
    vTaskDelay(100);
   }
}

/* 串口接收分发任务:ISR 只把字节投进 UsartRxHandler,由本任务决定去向。
   必须常驻运行 —— Show 页面下 Menu_handling_f 已挂起,退出指令只能靠本任务置 global_var */
void Usart_Rx_f(void *arg)
{
    uint8_t ch;
    while(1)
    {
        /* portMAX_DELAY 下本函数只会"取到数据"这一种返回:任务在此阻塞到队列非空为止,
           不存在超时返回 pdFALSE 的分支,原先的 != pdTRUE / continue 恒不可达,故直接调用 */
        xQueueReceive(UsartRxHandler,&ch,portMAX_DELAY);
        if(ch == 0)                             //退出:任何语境下都优先,必须放在时间设置判断之前
        {
            OLED_Clear();
            OLED_ShowString(2,5,"Exiting...");
            global_var = 1;
        }
        else if(global_var == 2)                //时间设置进行中:字节原样转给 SetTime_w,不做菜单解码
        {
            xQueueSend(UsartTimeHandler,&ch,0);
        }
        else if(ch == 5)                        //进入时间修改
        {
            /* 这条指令本身也要排进时间队列:SetTime_w 的 case 0 会消费它并立刻打印输入提示。
               与改造前一致(原 ISR 的 Flag_U/data 会被 SetTime_w 的首次等待直接取走),
               否则提示要等到用户再发一个字节才出现,且那个字节会被 case 0 白白吃掉 */
            xQueueSend(UsartTimeHandler,&ch,0);
            global_var = 2;
        }
        else if((ch >= 1) && (ch <= 4))          //菜单功能选择
        {
            xQueueSend(UsartMenuHandler,&ch,0);
        }
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
    uint8_t bt_cmd = 0xFF;              //本轮的蓝牙指令,0xFF 表示无
    while(1)
    {    
    OLED_ShowString(1, 1, "Enter menu num:");
    OLED_ShowString(2,1,"Choose:"); 
        OLED_ShowString(3, 1, "1:Exit  2:Enter ");
        OLED_ShowString(4, 1, "3:Up    4:Next  ");
    
    xQueueReceive(KeyGetHandler,&operate,portMAX_DELAY); 
      
    /*蓝牙功能选择*/	
        bt_cmd = 0xFF;                      //非阻塞取一条蓝牙指令,取不到则保持0xFF走default
        xQueueReceive(UsartMenuHandler,&bt_cmd,pdMS_TO_TICKS(50));
        switch(bt_cmd)
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
    if(beep_Cnt >= 10) vTaskSuspend(NULL);  //自限:翻转10轮(每轮约200ms,合计约2s)后自挂起,不再无限报警
    beep_Cnt++;
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

/* ---------- 任务创建的统一入口 ---------- */
static uint8_t Task_Fail_Cnt = 0;   //堆不足导致创建失败的任务数

/* 全部任务都经这里创建,失败时累加计数,避免十几份重复的 if。
   失败原因必然是同一个(堆耗尽),所以通报只报个数,不报名字 */
static void Create_Task(TaskFunction_t fn, char *name, uint16_t stack, UBaseType_t prio, TaskHandle_t *handle)
{
    if(xTaskCreate(fn, name, stack, NULL, prio, handle) != pdPASS)
    {
        Task_Fail_Cnt++;
    }
}

void Start_MyTaskList(void *arg)  //任务启动任务
{
    taskENTER_CRITICAL();
    Create_Task(Time_Work_f,     "Time_Work_f",     128, 4, &Time_Work_fHandler);
    Create_Task(Time_Show_f,     "Time_Show_f",     128, 4, &Time_Show_fHandler);
    Create_Task(Adxl345_Work_f,  "Adxl345_Work_f",  192, 4, &Adxl345_Work_fHandler);  //192字:周期性打印已移除,恢复原值;若以后本任务内要调UsartPrintf,必须加到256
    Create_Task(Adxl345_Show_f,  "Adxl345_Show_f",  128, 4, &Adxl345_Show_fHandler);
    Create_Task(DTH11_Work_f,    "DTH11_Work_f",    224, 4, &DTH11_Work_fHandler);
    Create_Task(DTH11_Show_f,    "DTH11_Show_f",    224, 4, &DTH11_Show_fHandler);
    Create_Task(Max30102_Work_f, "Max30102_Work_f", 224, 4, &Max30102_Work_fHandler);
    Create_Task(Max30102_Show_f, "Max30102_Show_f", 224, 4, &Max30102_Show_fHandler);

    Create_Task(Menu_handling_f, "Menu_handling_f", 256, 4, &Menu_handling_fHandler);
    Create_Task(Key_GetNum_f,    "Key_GetNum_f",    128, 4, &Key_GetNum_fHandler);
    Create_Task(beep_Work_f,     "beep_Work_f",     128, 4, &beep_Work_fHandler);
    Create_Task(Usart_Rx_f,      "Usart_Rx_f",      128, 3, &Usart_Rx_fHandler);

    if(Task_Fail_Cnt > 0)
    {
        /* 堆不足:报出失败个数即可,原因必然是同一个。
           本任务只有 128 字栈,这里禁止调用 UsartPrintf(它有 256B 栈上缓冲) */
        OLED_Clear();
        OLED_ShowString(1,1,"TASK CREATE");
        OLED_ShowNum(2,1,Task_Fail_Cnt,1);
        OLED_ShowString(2,3,"FAILED");
    }
    
    
    /* 逐个判空再挂起:创建失败的句柄会被 FreeRTOS 置为 NULL,
       而 vTaskSuspend(NULL) 的语义是"挂起调用者自身"——这里就是本任务,
       一旦踩中会把自己挂起,导致后面的挂起全部落空、栈与TCB永久泄漏。
       传 NULL 给 vTaskDelete 才是删除自身,两者语义不同,必须区分 */
    if(Time_Show_fHandler     != NULL) vTaskSuspend(Time_Show_fHandler);
    if(Adxl345_Show_fHandler  != NULL) vTaskSuspend(Adxl345_Show_fHandler);
    if(DTH11_Show_fHandler    != NULL) vTaskSuspend(DTH11_Show_fHandler);
    if(Max30102_Show_fHandler != NULL) vTaskSuspend(Max30102_Show_fHandler);
    if(beep_Work_fHandler     != NULL) vTaskSuspend(beep_Work_fHandler);

    taskEXIT_CRITICAL();
    vTaskDelete(NULL);

}

int main(void)
{
    BaseType_t xReceive = pdPASS;
    DHT11_Data_TypeDef DHT11_Data_R2;
    BloodData Trans;

    /* NVIC优先级分组:必须在任何NVIC_Init之前设置。misc.c的NVIC_Init会读AIRCR反推分组,
       若PRIGROUP保持复位值0会算出负移位数导致IP一律写成0x00。
       这里设为4位全抢占,与FreeRTOS约定的configMAX_SYSCALL_INTERRUPT_PRIORITY(5<<4=0x50)一致 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* 初始化部分 */
    OLED_Init();                                    // 初始化OLED显示器
    MyRTC_Init();                                   // 初始化实时时钟
    Key_Init();                                     // 初始化按键
    /* 串口接收队列必须在 Usart1_Init(使能RXNE中断)之前创建:
       否则中断先到、队列还是NULL,ISR里的 xQueueSendFromISR 会踩空指针 */
    UsartRxHandler   = xQueueCreate(8, sizeof(uint8_t));
    UsartMenuHandler = xQueueCreate(4, sizeof(uint8_t));
    UsartTimeHandler = xQueueCreate(8, sizeof(uint8_t));
    if((UsartRxHandler == NULL) || (UsartMenuHandler == NULL) || (UsartTimeHandler == NULL))
    {
        /* 此处 OLED_Init 已执行,必须先把失败显示出来再停机:
           直接 return 会让 main 静默退出,现场只剩一块没有任何提示的屏,无从判断是堆不够。
           字符串拆两行是因为 OLED 一行只有 16 列,"QUEUE CREATE FAIL" 17 字符会被截断 */
        OLED_Clear();
        OLED_ShowString(1,1,"QUEUE CREATE");
        OLED_ShowString(2,1,"FAIL");
        while(1);                                   // 堆不足:停机保留现场,不启动调度器
    }

    Usart1_Init(115200);                            // 初始化USART1串口，自定义的输入输出函数   
    EXTI8_Config();                                 // 配置外部中断
    Init_ADXL345();
    DHT11_GPIO_Config();
    IIC_GPIO_INIT();                                // I2C GPIO初始化
    MAX30102_GPIO();                                // MAX30102 GPIO初始化
    Max30102_reset();                               // MAX30102复位
    MAX30102_Config();                              // MAX30102配置

    /*任务运行部分，使用动态创建任务*/
        
    
    QueueGetHandler = xQueueCreate(1, sizeof(uint16_t) * 6);
    KeyGetHandler   = xQueueCreate(1, sizeof(uint8_t)  * 1);
    KeyExitHandler  = xQueueCreate(1, sizeof(uint8_t)  * 1); 
    ADX1345Handler  = xQueueCreate(1, sizeof(uint32_t)  * 1); 
    DTH11Handler    = xQueueCreate(1, sizeof(DHT11_Data_TypeDef)  * 1); 
    Max30102Handler = xQueueCreate(1, sizeof(BloodData)  * 1);
    if((QueueGetHandler == NULL) || (KeyGetHandler == NULL) || (KeyExitHandler == NULL) ||
       (ADX1345Handler == NULL) || (DTH11Handler == NULL) || (Max30102Handler == NULL))
    {
        /* 与上面三条串口队列同样的处理:OLED 已可用,先显示再停机,
           否则这些队列为 NULL 时各任务会在 xQueueReceive/xQueueSend 上踩空指针 */
        OLED_Clear();
        OLED_ShowString(1,1,"QUEUE CREATE");
        OLED_ShowString(2,1,"FAIL");
        while(1);
    }
    
    xReceive = xTaskCreate(Start_MyTaskList,"Start_MyTaskList",128,NULL ,2,&Start_MyTaskListHandler);
    if(pdPASS != xReceive)
    {
        /* 启动任务创建失败同样要看得见:原来这里 return 会静默退出,现场看不出原因 */
        OLED_Clear();
        OLED_ShowString(1,1,"TASK CREATE");
        OLED_ShowString(2,1,"FAIL");
        while(1);
    }
    vTaskStartScheduler();   /* 启动任务，开启调度 */
    while(1);
    
}

/* 栈溢出钩子:configCHECK_FOR_STACK_OVERFLOW=2 时必须实现。
 * 触发说明某任务栈击穿,在此冻结系统并停中断,便于定位是哪个任务溢出 */
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
    ( void ) xTask;
    ( void ) pcTaskName;
    taskDISABLE_INTERRUPTS();
    for( ;; );
}

