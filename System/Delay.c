#include "delay.h"

void Delay_f1us()
{
    for(char i =10;i>0;i--)
    {
        __NOP;
        __NOP;
        __NOP;
        __NOP;
        __NOP;        
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


































