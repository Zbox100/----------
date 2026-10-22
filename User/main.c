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
#include "i2c_bus.h"                  // 统一软件I2C协议层（7位地址/寄存器/错误码）
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
/* 退出请求已迁移到 PageEventGroup 的 EVT_EXIT 位(B2):
   位由 Usart_Rx_f 在任务上下文置位,4 个 Show 任务都能观察到,不再有"谁清谁得"的单消费问题 */
uint8_t IN = 0;
extern BloodData g_blooddata;
volatile uint32_t g_step_count = 0;   /* 串口综合显示用的步数快照(Adxl345_Work_f 更新) */
/////////////////////专用功能任务//////////////////////////////
TaskHandle_t Time_Work_fHandler;
TaskHandle_t Time_Show_fHandler;

TaskHandle_t Adxl345_Work_fHandler;
TaskHandle_t Adxl345_Show_fHandler;

TaskHandle_t DHT11_Work_fHandler;
TaskHandle_t DHT11_Show_fHandler;

TaskHandle_t Max30102_Work_fHandler;
TaskHandle_t Max30102_Show_fHandler;
/////////////////////通用功能任务//////////////////////////////
TaskHandle_t Menu_handling_fHandler;
TaskHandle_t Start_MyTaskListHandler;
TaskHandle_t Time_modification_fHandler;
TaskHandle_t Key_GetNum_fHandler;
TaskHandle_t beep_Work_fHandler;
TaskHandle_t Usart_Rx_fHandler;
////////////////////队列实现//////////////////////////////////////////////////////////
QueueHandle_t QueueGetHandler  =NULL;
QueueHandle_t KeyGetHandler    =NULL;
QueueHandle_t ADXL345Handler   =NULL;
QueueHandle_t DHT11Handler     =NULL;
QueueHandle_t Max30102Handler  =NULL;
QueueHandle_t UsartRxHandler   =NULL;   //USART1 ISR -> Usart_Rx_f
QueueHandle_t UsartMenuHandler =NULL;   //Usart_Rx_f  -> Menu_handling_f
QueueHandle_t UsartTimeHandler =NULL;   //Usart_Rx_f  -> SetTime_w
////////////////////事件组实现/////////////////////////////////
EventGroupHandle_t PageEventGroup = NULL;   //页面位 + 退出位广播(B2)

/* ---------- 页面/退出事件组公共操作 ---------- */

/* Show 任务统一等待入口:前台只等退出位,后台等"自己的页面位或退出位"。
   ⚠ 前台掩码里不能再带上自己的页面位 —— 它一直是置位状态,WaitBits 的条件立刻满足,
   会立即返回形成忙循环(协作式调度下等于把整机饿死)。
   xClearOnExit 必须为 pdFALSE —— 广播语义要求位保留到所有等待者都看到;
   若在这里清除,先醒的任务会把位清掉,其余 3 个就永远收不到(又退回"4 抢 1") */
static EventBits_t Page_Wait(EventBits_t page_bit, uint8_t active, TickType_t active_timeout)
{
    EventBits_t mask = active ? EVT_EXIT : (page_bit | EVT_EXIT);

    return xEventGroupWaitBits(PageEventGroup,
                               mask,
                               pdFALSE,
                               pdFALSE,
                               active ? active_timeout : portMAX_DELAY);
}

/* Menu 进入页面:先清掉上一轮遗留的退出位与页面位,再置本页位。
   之后不再 vTaskResume —— 页面任务常驻、阻塞在事件组上 */
static void Page_Enter(EventBits_t page_bit, TaskHandle_t page_task)
{
    if(page_task == NULL)                 //任务创建失败:留在菜单,避免把菜单挂死后无人接手
    {
        return;
    }
    xEventGroupClearBits(PageEventGroup, EVT_PAGE_ALL | EVT_EXIT);
    xEventGroupSetBits(PageEventGroup, page_bit);
    if(Key_GetNum_fHandler != NULL)          //判空:否则 vTaskSuspend(NULL) 会挂起 Menu 自己
    {
        vTaskSuspend(Key_GetNum_fHandler);
    }
    vTaskSuspend(NULL);
}

/* 页面退出时统一收尾:清掉"页面期间"积压在菜单队列里的 1~4,再恢复菜单与按键。
   否则回到菜单的第一轮会把残留指令当成新输入,出现"按 0 退出却自动跳回某页" */
static void Page_LeaveCommon(void)
{
    uint8_t dummy;

    while(xQueueReceive(UsartMenuHandler, &dummy, 0) == pdTRUE)
    {
        /* 丢弃页面期间残留的菜单指令 */
    }
    vTaskResume(Menu_handling_fHandler);
    vTaskResume(Key_GetNum_fHandler);
}

void Adxl345_Work_f(void*arg)
{	
        uint32_t Normal_num=0;                          //正常次数
        // 在任务局部声明（替换原来的 unsigned char Flag） 
        uint8_t impact_x = 0 ; // X 轴冲击标志 
        uint8_t impact_y = 0 ; // Y 轴冲击标志
        float mag = 0;                                 //三轴合加速度幅值,静止时约为1000mg
        float base = 1000.0f;                          //慢速基线(重力+姿态缓变),τ≈5s
        float dev = 0;                                 //mag 相对基线的偏差:一次落脚只形成一个正半波
        uint8_t armed = 1;                             //施密特状态:1=等待抬升,0=已计步待回落
        const float STEP_TH = 100.0f;                  //抬升阈值(mg):静止约1000,走路摆动约±100~200,故取100
        const float ARM_TH  = 50.0f;                   //回落阈值(mg):负向,取50使每步都能重新武装
        const uint16_t STEP_REFRACTORY_MS = 300;       //两次计步最小间隔(ms)
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
        /* 量程校验:ADXL345 配 ±16g、3.9mg/LSB,单轴物理极值约 ±15974mg。
           超出即视为 I2C 位错误造成的脏值,丢弃本轮样本(不计步、不发队列、
           不判冲击,也不更新基线 base),避免脏值污染步态判定 */
        if(temp_X < -17000 || temp_X > 17000 ||
           temp_Y < -17000 || temp_Y > 17000 ||
           temp_Z < -17000 || temp_Z > 17000)
        {
            continue;               //回到循环顶,vTaskDelayUntil 保证 50ms 周期不变
        }
        mag = sqrtf(temp_X*temp_X + temp_Y*temp_Y + temp_Z*temp_Z);  //三轴合加速度,对握持方向不敏感

        /* 慢速基线:一阶低通,τ≈5s,只跟随重力/姿态的缓慢变化,不被步态振荡带走 */
        base += (mag - base) * 0.01f;
        dev = mag - base;                                      //相对基线的偏差(带符号)

        /* 计步:施密特状态机,判据用"相对基线的偏差 dev",而不是 |Δmag|。
           原因:|Δmag| 在一次落脚内有多个峰(落地/回弹/摆臂),它们的间距与相邻
           两步的间距是同一量级(都 400~800ms),所以无论怎么调阈值和解锁条件都
           只能二选一:解锁早了每步多计 2~3 次,解锁晚了走路时迟迟不解锁、计数
           被拖到停下之后(1~2s 延迟)。根因是判据信号选错了。
           带符号的 dev 一次落脚只形成一个正半波:
             抬升越过 +STEP_TH 计一步,随即解除武装;
             直到 dev 回落到 -ARM_TH 以下(步态摆动带来的负向半波)才重新武装。
           同一步内的谐波压不到负阈值,故一次落脚只计一步;触发在抬升前沿,
           显示延迟不因此增加。 */
        if(!armed)
        {
            if(dev < -ARM_TH) armed = 1;                       //回落到负阈值以下:重新武装
        }
        else if(dev > STEP_TH)
        {
            if((xTaskGetTickCount() - lastStepTick) >= pdMS_TO_TICKS(STEP_REFRACTORY_MS))
            {
                Normal_num++;                                  //计一步
                g_step_count = Normal_num;                      //同步给串口综合显示
                lastStepTick = xTaskGetTickCount();            //记录本次计步时刻
                armed = 0;                                     //解除武装,等待负向回落
            }
        }
        xQueueOverwrite(ADXL345Handler,&Normal_num);   //深度1"仅保留最新值"队列用覆盖写,避免满队列丢值导致显示落后一拍
      
        /* ① 逐轴检测：各自独立标志 */ 
        impact_x = (temp_X > 700 ) ? 1 : 0 ;
        impact_y = (temp_Y > 700 ) ? 1 : 0 ;

        /* ② 报警触发：任一轴冲击且使能。
           beep 任务自身带 2s tick 硬上限,这里只负责唤醒,不再维护轮数计数 */
        if ((impact_x || impact_y) && IN) {
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
    uint32_t last_shown = 0xFFFFFFFF;   //上次已显示值(取不可能的值,保证首次必刷)
    uint8_t  frame_drawn = 0;   //静态帧(边框/标签)是否已画:进页面清一次屏后置1,退出时置0
    uint8_t  active = 0;        //本页是否在前台(由页面位决定)
    while(1)
    {
        EventBits_t bits = Page_Wait(EVT_PAGE_STEP, active, pdMS_TO_TICKS(50));

        if(bits & EVT_EXIT)
        {
            if(bits & EVT_PAGE_STEP)            //后台页面只确认收到;前台页面才真正退页
            {
                xEventGroupClearBits(PageEventGroup, EVT_PAGE_STEP | EVT_EXIT);  //先让出前台身份
                global_var = 0;
                GPIO_ResetBits(GPIOB, GPIO_Pin_12);
                GPIO_SetBits(GPIOB, GPIO_Pin_1);
                OLED_Clear();
                frame_drawn = 0;        //离屏清场:静态帧随之失效,下次进页面重画
                Page_LeaveCommon();
                vTaskSuspend(beep_Work_fHandler);
                active = 0;
                continue;
            }
            vTaskDelay(1);              //后台页:让出1个tick,避免退出位未清时忙循环
            continue;
        }

        if(!(bits & EVT_PAGE_STEP))     //既没有退出事件,也没轮到本页
        {
            active = 0;
            continue;
        }
        active = 1;
        /* 局部刷新:整屏清除 + 静态边框/标签只在进页面时做一次,
           循环内只重写变化的数字(OLED_ShowNum 定宽整格覆盖,天然盖掉旧值),
           不再每轮 OLED_Clear —— 那是本页 100ms 整屏闪烁的根源 */
        if(!frame_drawn)
        {
            OLED_Clear();                       //抹掉上一页菜单的残留,整屏只清这一次
            OLED_ShowString(1, 1, "***************");
            OLED_ShowString(2, 1, "Bushu:");
            OLED_ShowString(4, 1, "***************");
            frame_drawn = 1;
            last_shown = 0xFFFFFFFF;    //重画静态帧后强制刷新一次数字
        }
        /* 只保留最新值:等待节拍由 Page_Wait 的 50ms 承担,这里非阻塞取一份快照,
           且仅在数值变化时重画,消掉固定延时叠加的显示滞后 */
        if(xQueueReceive(ADXL345Handler,&dis_buf_R,0) == pdTRUE)
        {
            if(dis_buf_R != last_shown)
            {
                OLED_ShowNum(2,7,dis_buf_R,4);
                last_shown = dis_buf_R;
            }
        }
    }
}
     
void Time_Work_f( void * arg ) //时间读取任务
{
    while(1)
    {
    /* 时间修改期间禁止刷新全局数组:MyRTC_ReadTime 会把 MyRTC_Time[]
       整组写回当前 RTC 值,而 SetTime_w 正是逐字节往这个数组里填新时间。
       原实现靠 vTaskSuspendAll 屏蔽覆盖,改用 vTaskDelay 让出 CPU 后必须显式跳过,
       否则每 1s 都会把刚输入的字段冲掉,修改结果必然是错的。
       注意判据必须是 ==0 而不是 !=2:收完最后一个字节后 global_var 已置 9,
       而 SetTime_w 之后还有两段 500ms 的 vTaskDelay,合计正好 1s,
       足以让本任务插进来把整组字段刷回旧值,再交给 MyRTC_SetTime 写回 RTC */
    if(global_var == 0)
    {
        MyRTC_ReadTime();

        /* 综合遥测:每秒一行,把时间/步数/心率/血氧/温湿度汇总输出,
           取代原先各模块分散打印(DHT11 读成功、blood_Loop 每轮心率/血氧) */
        {
            uint8_t hi = 0, hd = 0, ti = 0, td = 0;

            if(g_dht11_valid)
            {
                hi = g_dht11_latest.humi_int;
                hd = g_dht11_latest.humi_deci;
                ti = g_dht11_latest.temp_int;
                td = g_dht11_latest.temp_deci;
            }

            UsartPrintf(USART1,
                "[%04u-%02u-%02u %02u:%02u:%02u] Step=%lu HR=%d SpO2=%.1f T=%u.%uC H=%u.%u%%\r\n",
                MyRTC_Time[0], MyRTC_Time[1], MyRTC_Time[2],
                MyRTC_Time[3], MyRTC_Time[4], MyRTC_Time[5],
                (unsigned long)g_step_count,
                g_blooddata.heart, g_blooddata.SpO2,
                ti, td, hi, hd);
        }
    }
    vTaskDelay(1000);  
    }
          
}
void Time_Show_f( void*arg ) //时间显示任务任务
{ 
    
    uint16_t MyRTC_R[6];
    uint16_t last_time[6] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    uint8_t  frame_drawn = 0;   //静态帧是否已画:退出或 SetTime_w 改写屏幕后置0重画
    uint8_t  active = 0;        //本页是否在前台
    uint8_t  i;

    while(1)
    {
        EventBits_t bits = Page_Wait(EVT_PAGE_TIME, active, pdMS_TO_TICKS(100));

        if(bits & EVT_EXIT)     //退出位对 4 个页面任务广播;只有前台页面执行退页
        {
            if(bits & EVT_PAGE_TIME)
            {
                xEventGroupClearBits(PageEventGroup, EVT_PAGE_TIME | EVT_EXIT);
                global_var = 0;
                OLED_Clear();
                frame_drawn = 0;
                Page_LeaveCommon();
                active = 0;
                continue;
            }
            vTaskDelay(1);
            continue;
        }

        if(!(bits & EVT_PAGE_TIME))
        {
            active = 0;
            continue;
        }
        active = 1;

        if(global_var==2)
        {
        SetTime_w();
        frame_drawn = 0;    //SetTime_w 会往第4行写 "Exiting..." 再涂成空格,静态帧已脏,下一轮重画
        for(i = 0; i < 6; i++) last_time[i] = 0xFFFF;   //强制下一拍把新时间写上屏
        if((xEventGroupGetBits(PageEventGroup) & EVT_EXIT) != 0)
        {
            continue;       //退出位已置:不再重画,直接回顶部走统一退页流程
        }
        }

        /* 局部刷新:整屏清除 + 静态标签只在进页面时做一次,循环内只重写 6 个时间数字 */
        if(!frame_drawn)
        {
            OLED_Clear();                       //抹掉上一页菜单的残留
            OLED_ShowString(1, 1, "Date:    -  -  ");
            OLED_ShowString(2, 1, "Time:  :  :  ");
            OLED_ShowString(3, 1, "Key 5 to amend  ");
            OLED_ShowString(4, 1, "***************");
            frame_drawn = 1;
            for(i = 0; i < 6; i++) last_time[i] = 0xFFFF;
        }
        /* 等待节拍已由 Page_Wait 的 100ms 承担:退出请求最长 100ms 内被看到。
           这里非阻塞取一份快照,只有数值变化才重画(时间任务每秒才更新一次) */
        if(xQueueReceive(QueueGetHandler,MyRTC_R,0) == pdTRUE)
        {
            if((MyRTC_R[0] != last_time[0]) || (MyRTC_R[1] != last_time[1]) ||
               (MyRTC_R[2] != last_time[2]) || (MyRTC_R[3] != last_time[3]) ||
               (MyRTC_R[4] != last_time[4]) || (MyRTC_R[5] != last_time[5]))
            {
                for(i = 0; i < 6; i++) last_time[i] = MyRTC_R[i];
                OLED_ShowNum(1, 6, MyRTC_R[0], 4);     // 年
                OLED_ShowNum(1, 11, MyRTC_R[1], 2);    // 月
                OLED_ShowNum(1, 14, MyRTC_R[2], 2);    // 日
                OLED_ShowNum(2, 6, MyRTC_R[3], 2);     // 时
                OLED_ShowNum(2, 9, MyRTC_R[4], 2);     // 分
                OLED_ShowNum(2, 12, MyRTC_R[5], 2);    // 秒
            }
        }
    }
               
}

void DHT11_Work_f(void*arg)
{  
    TickType_t xLastWakeTime = xTaskGetTickCount(); //记录首次唤醒时刻,建立绝对时间基准网格
    const TickType_t xPeriod = pdMS_TO_TICKS(2000); //2s 采样周期:DHT11 手册要求两次读取间隔≥1s,过频会读到未刷新的旧值

    /* 主循环 */
    while(1)
    {
        DHT11_SCAN();     
        vTaskDelayUntil(&xLastWakeTime, xPeriod);    //按绝对时间网格唤醒,补偿执行耗时,保证 2s 等间隔
    }
}

void DHT11_Show_f(void*arg)
{
    DHT11_Data_TypeDef DHT11_Data_R;
    uint8_t frame_drawn = 0;        //静态帧是否已画:退出清屏后置0,下次进页面重画
    uint8_t active = 0;             //本页是否在前台

    while(1)
    {   
        EventBits_t bits = Page_Wait(EVT_PAGE_DHT, active, pdMS_TO_TICKS(200));

        if(bits & EVT_EXIT)
        {
            if(bits & EVT_PAGE_DHT)
            {
                xEventGroupClearBits(PageEventGroup, EVT_PAGE_DHT | EVT_EXIT);
                global_var = 0;
                OLED_Clear();
                frame_drawn = 0;        //离屏清场:静态帧随之失效,下次进页面重画
                Page_LeaveCommon();
                active = 0;
                continue;
            }
            vTaskDelay(1);
            continue;
        }

        if(!(bits & EVT_PAGE_DHT))
        {
            active = 0;
            continue;
        }
        active = 1;
        /* 同 Max30102_Show_f:DHT11_SCAN() 只在读成功时才往队列里送数据,模块未连接时
           队列永远为空,原 portMAX_DELAY 死等会让本任务无法响应退出请求,页面卡在"Exiting..."
           改为非阻塞取数,等待节拍由 Page_Wait 的 200ms 承担 */
        /* 局部刷新:静态标签只在进页面时画一次,数据到达时只重写 4 个数字 */
        if(!frame_drawn)
        {
            OLED_Clear();                       //抹掉上一页菜单的残留
            OLED_ShowString(1, 1, "***************");
            OLED_ShowString(2,3,"Humi:");
            OLED_ShowString(3,3,"Temp:");
            OLED_ShowString(4, 1, "***************");
            frame_drawn = 1;
        }
        if(xQueueReceive(DHT11Handler,&DHT11_Data_R,0) == pdTRUE)
        {
            OLED_ShowNum(2,9,DHT11_Data_R.humi_int,2);
            OLED_ShowNum(2,12,DHT11_Data_R.humi_deci,2);
            OLED_ShowNum(3,9,DHT11_Data_R.temp_int,2);
            OLED_ShowNum(3,12,DHT11_Data_R.temp_deci,2);
        }
    }

        
}            

void Max30102_Work_f(void*arg)
{
    uint8_t online = 0;                              //模块在位标志:PART_ID校验通过才置1

    while(1)
    {   
        /* 在位检测:PART_ID(0xFF)正常应读回0x15。软件I2C读不到器件时会应答失败返回0,
           模块未连接时直接跳过本轮采样,避免进入等待INT引脚/FIFO的循环把调度器挂死 */
        online = (Max30102_ReadReg(REG_PART_ID) == 0x15) ? 1 : 0;
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
   uint8_t frame_drawn = 0;     //静态帧是否已画:退出清屏后置0,下次进页面重画
   uint8_t active = 0;          //本页是否在前台
   while(1)
   {
        EventBits_t bits = Page_Wait(EVT_PAGE_MAX, active, pdMS_TO_TICKS(200));

        if(bits & EVT_EXIT)
        {
            if(bits & EVT_PAGE_MAX)
            {
                xEventGroupClearBits(PageEventGroup, EVT_PAGE_MAX | EVT_EXIT);
                global_var = 0;
                OLED_Clear();
                frame_drawn = 0;    //离屏清场:静态帧随之失效,下次进页面重画
                Page_LeaveCommon();
                active = 0;
                continue;
            }
            vTaskDelay(1);
            continue;
        }

        if(!(bits & EVT_PAGE_MAX))
        {
            active = 0;
            continue;
        }
        active = 1;
    /* 原为 xQueueReceive(...,portMAX_DELAY) 死等:模块未连接时采集任务不会往队列里送数据
       (Max30102_Work_f 在位检测不通过就直接跳过采样),本任务会永远阻塞在这里,
       既刷不出界面,也回不到循环顶部判断退出条件;
       改为非阻塞取数,等待节拍由 Page_Wait 的 200ms 承担 */
    /* 局部刷新:静态标签只在进页面时画一次,数据到达时只重写 SpO2 与心率两个数字 */
    if(!frame_drawn)
    {
        OLED_Clear();                           //抹掉上一页菜单的残留
        OLED_ShowString(1, 1, "***************");
        OLED_ShowString(2,4,"SpO2: ");
        OLED_ShowString(3,4,"Heart:");
        OLED_ShowString(4, 1, "***************");
        frame_drawn = 1;
    }
    if(xQueueReceive(Max30102Handler,&g_blooddata,0) == pdTRUE)
    {
        OLED_ShowNum(2,9,g_blooddata.SpO2,3);
        OLED_ShowNum(3,10,g_blooddata.heart,3);
    }
    
   }
}

/* 串口接收分发任务:ISR 只把字节投进 UsartRxHandler,由本任务决定去向。
   必须常驻运行 —— Show 页面下 Menu_handling_f 已挂起,退出指令只能靠本任务置事件位 */
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
            /* 退出广播:置位而不是清标志,4 个 Show 任务都能观察到;
               菜单态(没有页面位)忽略本次退出,避免退出位残留导致下次进页面立刻被弹回 */
            if((xEventGroupGetBits(PageEventGroup) & EVT_PAGE_ALL) != 0)
            {
                xEventGroupSetBits(PageEventGroup, EVT_EXIT);
            }
        }
        else if(global_var == 2)                //时间设置进行中:字节原样转给 SetTime_w,不做菜单解码
        {
            xQueueSend(UsartTimeHandler,&ch,0);
        }
        else if(ch == 5)                        //进入时间修改
        {
            /* 只在时间页前台时才接受:否则在菜单按 5 会给人"已进入修改"的错觉,
               而且这条指令会一直留在队列里,等下次进时间页时突然进入修改态 */
            if((xEventGroupGetBits(PageEventGroup) & EVT_PAGE_TIME) != 0)
            {
                /* 这条指令本身也要排进时间队列:SetTime_w 的 case 0 会消费它并立刻打印输入提示 */
                xQueueSend(UsartTimeHandler,&ch,0);
                global_var = 2;
            }
        }
        else if((ch >= 1) && (ch <= 4))          //菜单功能选择
        {
            /* 只在菜单态接收:页面态 Menu 已挂起,此时入队只会变成"回菜单后自动跳页"的残留 */
            if((xEventGroupGetBits(PageEventGroup) & EVT_PAGE_ALL) == 0)
            {
                xQueueSend(UsartMenuHandler,&ch,0);
            }
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
            Page_Enter(EVT_PAGE_TIME, Time_Show_fHandler);
            break;
        case 2 : 
            IN = 1;                             //跌倒检测使能:只在进计步页时打开
            Page_Enter(EVT_PAGE_STEP, Adxl345_Show_fHandler);
            break;
        case 3 : 
            Page_Enter(EVT_PAGE_DHT, DHT11_Show_fHandler);
            break;
        case 4 : 
            Page_Enter(EVT_PAGE_MAX, Max30102_Show_fHandler);
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
                {   Page_Enter(EVT_PAGE_TIME, Time_Show_fHandler);
                }
                    
                else if(menu_index == 1)
                {   IN = 1;
                    Page_Enter(EVT_PAGE_STEP, Adxl345_Show_fHandler);
                }
                    
                else if(menu_index == 2)
                {   Page_Enter(EVT_PAGE_DHT, DHT11_Show_fHandler);
                }
                else if(menu_index == 3)
                {   Page_Enter(EVT_PAGE_MAX, Max30102_Show_fHandler);
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
    TickType_t t_start;

    while(1)
    {	
        /* 自限:每次被唤醒最多响 2s,到点必停并自挂起。
           用 tick 差做判据而不是"翻转轮数":原来的轮数上限依赖全局 beep_Cnt,
           而它会被触发方清零,理论上可被重复触发无限续期;tick 差值在本次唤醒内
           只由本任务自己维护,外部 vTaskResume 无法改变,上限是硬性的 */
        t_start = xTaskGetTickCount();
        while((xTaskGetTickCount() - t_start) < pdMS_TO_TICKS(2000))
        {
            UsartPrintf(USART1, "  FALLING DOWN! \r\n");    
            OLED_ShowString(3, 2, "FALLING DOWN!");
            GPIO_ResetBits(GPIOB, GPIO_Pin_12);						
            GPIO_ResetBits(GPIOB, GPIO_Pin_1);						
            vTaskDelay(pdMS_TO_TICKS(100));
            OLED_ShowString(3, 2, "FALLING DOWN!");        
            GPIO_SetBits(GPIOB, GPIO_Pin_12);							
            GPIO_SetBits(GPIOB, GPIO_Pin_1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        /* 收尾固定在静音电平,避免恰好停在"响"的半周期上被挂起 */
        GPIO_SetBits(GPIOB, GPIO_Pin_12);
        GPIO_SetBits(GPIOB, GPIO_Pin_1);
        vTaskSuspend(NULL);     //自限结束:挂起,等待下一次触发唤醒
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
    /* 任务优先级分层:
       5 = 重 Work(长非阻塞段/时序敏感采样):DHT11 位操作忙等、MAX30102 FIFO+FFT
       4 = 其余 Work(时间读取、ADXL 50ms 采样) 与 交互任务(Menu/Key/beep/Usart_Rx)
       3 = Show 显示任务(只画屏,最低) */
    Create_Task(Time_Work_f,     "Time_Work_f",     192, 4, &Time_Work_fHandler);  //192字:原峰值38w,新增每秒综合遥测后含 vsnprintf(%f) 的栈开销,留足余量
    Create_Task(Time_Show_f,     "Time_Show_f",    128, 3, &Time_Show_fHandler);  //128字:内含 SetTime_w()(多路打印),实测峰值52w,按"+48余量"取128
    Create_Task(Adxl345_Work_f,  "Adxl345_Work_f",   128, 4, &Adxl345_Work_fHandler);  //96字:实测峰值38w
    Create_Task(Adxl345_Show_f,  "Adxl345_Show_f",   96, 3, &Adxl345_Show_fHandler);  //96字:实测峰值36w
    Create_Task(DHT11_Work_f,    "DHT11_Work_f",    128, 5, &DHT11_Work_fHandler);  //128字:此前含临时栈水位打印时峰值66~70w,观测装置已拆,峰值更低
    Create_Task(DHT11_Show_f,    "DHT11_Show_f",     96, 3, &DHT11_Show_fHandler);  //96字:实测峰值38w
    Create_Task(Max30102_Work_f, "Max30102_Work_f", 176, 5, &Max30102_Work_fHandler);  //176字:实测峰值123w
    Create_Task(Max30102_Show_f, "Max30102_Show_f",  96, 3, &Max30102_Show_fHandler);  //96字:实测峰值34w

    Create_Task(Menu_handling_f, "Menu_handling_f", 128, 4, &Menu_handling_fHandler);  //128字:实测峰值60w
    Create_Task(Key_GetNum_f,    "Key_GetNum_f",     96, 4, &Key_GetNum_fHandler);  //96字:实测峰值34w
    Create_Task(beep_Work_f,     "beep_Work_f",      96, 4, &beep_Work_fHandler);  //96字:实测峰值42w(已触发报警跑过打印分支);UsartPrintf 的 256B 缓冲是 static,不在任务栈上
    Create_Task(Usart_Rx_f,      "Usart_Rx_f",       96, 4, &Usart_Rx_fHandler);  //96字:实测峰值32w

    if(Task_Fail_Cnt > 0)
    {
        /* 堆不足:报出失败个数即可,原因必然是同一个。
           本任务只有 128 字栈,这里禁止调用 UsartPrintf(它有 256B 栈上缓冲) */
        OLED_Clear();
        OLED_ShowString(1,1,"TASK CREATE");
        OLED_ShowNum(2,1,Task_Fail_Cnt,1);
        OLED_ShowString(2,3,"FAILED");
    }
    
    
    /* 4 个 Show 任务不再在启动时挂起(B2):它们常驻运行,各自阻塞在 PageEventGroup 上,
       由页面位决定谁在前台、由退出位广播给全部 4 个。
       仍需判空后挂起的是 beep —— vTaskSuspend(NULL) 语义是挂起调用者自身,
       句柄创建失败时必须跳过,否则会把本任务挂起、后续步骤全部落空 */
    if(beep_Work_fHandler != NULL) vTaskSuspend(beep_Work_fHandler);

    taskEXIT_CRITICAL();

    vTaskDelete(NULL);

}

int main(void)
{
    BaseType_t xReceive;

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
    UsartRxHandler   = xQueueCreate(32, sizeof(uint8_t));   //32 深度:防止长 I2C 阻塞期间连续按键/蓝牙字节溢出
    UsartMenuHandler = xQueueCreate(8,  sizeof(uint8_t));
    UsartTimeHandler = xQueueCreate(32, sizeof(uint8_t));   //时间修改一次要排 8 字节,留足余量
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
    SoftI2C_Init(&I2C_Bus_MAX);                     // MAX30102 的软件 I2C 总线(PA11/PA12)初始化
    MAX30102_GPIO();                                // MAX30102 GPIO初始化
    Max30102_reset();                               // MAX30102复位
    MAX30102_Config();                              // MAX30102配置

    /*任务运行部分，使用动态创建任务*/
        
    
    QueueGetHandler = xQueueCreate(1, sizeof(uint16_t) * 6);
    KeyGetHandler   = xQueueCreate(1, sizeof(uint8_t)  * 1);
    ADXL345Handler  = xQueueCreate(1, sizeof(uint32_t)  * 1); 
    DHT11Handler    = xQueueCreate(1, sizeof(DHT11_Data_TypeDef)  * 1); 
    Max30102Handler = xQueueCreate(1, sizeof(BloodData)  * 1);
    if((QueueGetHandler == NULL) || (KeyGetHandler == NULL) ||
       (ADXL345Handler == NULL) || (DHT11Handler == NULL) || (Max30102Handler == NULL))
    {
        /* 与上面三条串口队列同样的处理:OLED 已可用,先显示再停机,
           否则这些队列为 NULL 时各任务会在 xQueueReceive/xQueueSend 上踩空指针 */
        OLED_Clear();
        OLED_ShowString(1,1,"QUEUE CREATE");
        OLED_ShowString(2,1,"FAIL");
        while(1);
    }

    /* 页面/退出事件组必须在任务创建前存在:4 个 Show 任务启动后立即会用它做等待 */
    PageEventGroup = xEventGroupCreate();
    if(PageEventGroup == NULL)
    {
        OLED_Clear();
        OLED_ShowString(1,1,"EVENT GROUP");
        OLED_ShowString(2,1,"CREATE FAIL");
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

