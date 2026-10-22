#ifndef __MYRTC_H
#define __MYRTC_H

/* 时间修改三态:正常显示 -> 接收 8 字节 -> 等待写入 RTC。
   事件组负责"页面/退出"的广播,这里只表达时间页私有的对话框状态 */
typedef enum
{
    TIME_EDIT_IDLE   = 0,   /* 正常显示:Time_Work_f 可以回读 RTC */
    TIME_EDIT_ACTIVE = 2,   /* 正在接收时间字节 */
    TIME_EDIT_COMMIT = 9    /* 字节收齐,等待写入 RTC */
} TimeEditState_t;

extern volatile TimeEditState_t g_time_edit;

void TimeEdit_Begin(void);      /* IDLE  -> ACTIVE */
void TimeEdit_Commit(void);     /* ACTIVE-> COMMIT */
void TimeEdit_End(void);        /* 任意  -> IDLE   */

/* [0]=年(4位) [1]=月 [2]=日 [3]=时 [4]=分 [5]=秒 */
extern uint16_t MyRTC_Time[];

void MyRTC_Init(void);
void MyRTC_SetTime(void);
void MyRTC_ReadTime(void);
void SetTime_w(void);

#endif
