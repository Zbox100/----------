#ifndef __MYRTC_H
#define __MYRTC_H

/* [0]=年(4位) [1]=月 [2]=日 [3]=时 [4]=分 [5]=秒 */
extern uint16_t MyRTC_Time[];

void MyRTC_Init(void);
void MyRTC_SetTime(void);
void MyRTC_ReadTime(void);
void SetTime_w(void);

#endif
