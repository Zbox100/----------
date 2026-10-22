#include "delay.h"

void Delay_f1us()
{
    /* 空循环计数:延时长度按该循环的指令周期校准。
       原写法 __NOP; 会被编译器当作无效果语句丢弃(伴随 #174 警告),
       实际时序就是"空循环";此处删除而不是改成 __NOP();,
       避免新增 50 条 NOP 把 1us 基准拉长约一倍,破坏 DHT11/I2C 的时序 */
    for(char i =10;i>0;i--)
    {
    }
}

void Delay_fnus(uint16_t i)
{
    while(i--)
    {Delay_f1us();}
}


void Delay_fnms(uint16_t i)
{
    while(i--)
    {Delay_fnus(1000);}
}


































