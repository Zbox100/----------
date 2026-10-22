#include "blood.h"
#include "usart.h"
#include "head.h"
/**
  ******************************************************************************
  * @file    blood.c
  * @author  锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
  * @brief   血液锟斤拷锟斤拷锟斤拷转锟斤拷锟斤拷锟斤拷
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"                         //??vTaskDelay

extern QueueHandle_t Max30102Handler;
uint16_t g_fft_index = 0;         	 	//fft锟斤拷锟斤拷锟斤拷锟斤拷卤锟?

struct compx s1[FFT_N+16];           	//FFT锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷S[1]锟斤拷始锟斤拷牛锟斤拷锟斤拷荽锟叫★拷约锟斤拷锟斤拷锟?
struct compx s2[FFT_N+16];           	//FFT锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷S[1]锟斤拷始锟斤拷牛锟斤拷锟斤拷荽锟叫★拷约锟斤拷锟斤拷锟?

struct
{
	float 	Hp	;			//血锟届蛋锟斤拷	
	float 	HpO2;			//锟斤拷锟斤拷血锟届蛋锟斤拷
	
}g_BloodWave;//血液锟斤拷锟斤拷锟斤拷锟斤拷

BloodData g_blooddata = {0};					//血液锟斤拷锟捷存储

#define CORRECTED_VALUE			47   			//锟疥定血液锟斤拷锟斤拷锟斤拷锟斤拷

/*funcation start ------------------------------------------------------------*/
//血液锟斤拷锟斤拷锟较拷锟斤拷锟?
void blood_data_update(void)
{
	uint16_t wait_ms = 0;                 //连续等待新样本的时长(ms),用于超时退出
	//标志位，使用时 ，读取FIFO
	g_fft_index=0;
	while(g_fft_index < FFT_N)
	{
		if(MAX30102_INTPin_Read()!=0)      //INT为高:FIFO暂无新数据
		{
			if(wait_ms >= 2000)            //连续2s没有新样本:判定模块异常,放弃本次采集
			{
				break;
			}
			vTaskDelay(1);                 //让出CPU 1ms,避免无延时忙等把协作式调度器挂死
			wait_ms++;
			continue;
		}
		wait_ms = 0;                       //有新数据,清空等待计时
		//读取FIFO
		max30102_read_fifo();              //read from MAX30102 FIFO2
		//采样后写入fft输入并递增下标
		if(g_fft_index < FFT_N)
		{
			s1[g_fft_index].real = fifo_red;
			s1[g_fft_index].imag= 0;
			s2[g_fft_index].real = fifo_ir;
			s2[g_fft_index].imag= 0;
			g_fft_index++;
		}
	}
}

//血液锟斤拷息转锟斤拷
void blood_data_translate(void)
{	
	float n_denom;
	uint16_t i;
	//直锟斤拷锟剿诧拷
	float dc_red =0; 
	float dc_ir =0;
	float ac_red =0; 
	float ac_ir =0;
	
	for (i=0 ; i<FFT_N ; i++ ) 
	{
		dc_red += s1[i].real ;
		dc_ir +=  s2[i].real ;
	}
		dc_red =dc_red/FFT_N ;
		dc_ir =dc_ir/FFT_N ;
	for (i=0 ; i<FFT_N ; i++ )  
	{
		s1[i].real =  s1[i].real - dc_red ; 
		s2[i].real =  s2[i].real - dc_ir ; 
	}
	
	//锟狡讹拷平锟斤拷锟剿诧拷
//	UsartPrintf(USART_DEBUG,"***********8 pt Moving Average red******************************************************\r\n");
//	
	for(i = 1;i < FFT_N-1;i++) 
	{
			n_denom= ( s1[i-1].real + 2*s1[i].real + s1[i+1].real);
			s1[i].real=  n_denom/4.00; 
			
			n_denom= ( s2[i-1].real + 2*s2[i].real + s2[i+1].real);
			s2[i].real=  n_denom/4.00; 			
	}
	//锟剿碉拷平锟斤拷锟剿诧拷
	for(i = 0;i < FFT_N-8;i++) 
	{
			n_denom= ( s1[i].real+s1[i+1].real+ s1[i+2].real+ s1[i+3].real+ s1[i+4].real+ s1[i+5].real+ s1[i+6].real+ s1[i+7].real);
			s1[i].real=  n_denom/8.00; 
			
			n_denom= ( s2[i].real+s2[i+1].real+ s2[i+2].real+ s2[i+3].real+ s2[i+4].real+ s2[i+5].real+ s2[i+6].real+ s2[i+7].real);
			s2[i].real=  n_denom/8.00; 
		
//			UsartPrintf(USART_DEBUG,"%f\r\n",s1[i].real);		
	}
//	UsartPrintf(USART_DEBUG,"************8 pt Moving Average ir*************************************************************\r\n");
//	for(i = 0;i < FFT_N;i++) 
//	{
//		UsartPrintf(USART_DEBUG,"%f\r\n",s2[i].real);	
//	}
//	UsartPrintf(USART_DEBUG,"**************************************************************************************************\r\n");
	//锟斤拷始锟戒换锟斤拷示	
	g_fft_index = 0;	
	//锟斤拷锟劫革拷锟斤拷叶锟戒换
	FFT(s1);
	FFT(s2);
	//锟斤拷平锟斤拷
// UsartPrintf(USART_DEBUG,"FFT start ****************************************************************************************************\r\n");
	for(i = 0;i < FFT_N;i++) 
	{
		s1[i].real=sqrtf(s1[i].real*s1[i].real+s1[i].imag*s1[i].imag);
		s2[i].real=sqrtf(s2[i].real*s2[i].real+s2[i].imag*s2[i].imag);
	}
	//锟斤拷锟姐交锟斤拷锟斤拷锟斤拷
	for (i=1 ; i<FFT_N ; i++ ) 
	{
		ac_red += s1[i].real ;
		ac_ir +=  s2[i].real ;
	}
	
//	for(i = 0;i < FFT_N/2;i++) 
//	{
//		UsartPrintf(USART_DEBUG,"%f\r\n",s1[i].real);
//	}
//	UsartPrintf(USART_DEBUG,"****************************************************************************************\r\n");
//	for(i = 0;i < FFT_N/2;i++) 
//	{
//		UsartPrintf(USART_DEBUG,"%f\r\n",s2[i].real);
//	}
//	
// UsartPrintf(USART_DEBUG,"FFT end ******************************************************************************************************\r\n");
	for(i = 0;i < 50;i++) 
	{
		if(s1[i].real<=10)
			break;
	}
	
	//UsartPrintf(USART_DEBUG,"%d\r\n",(int)i);
	//锟斤拷取锟斤拷值锟斤拷暮锟斤拷锟斤拷锟? 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟轿?
	int s1_max_index = find_max_num_index(s1, 15);
	int s2_max_index = find_max_num_index(s2, 15);
//	UsartPrintf(USART_DEBUG,"%d\r\n",s1_max_index);
//	UsartPrintf(USART_DEBUG,"%d\r\n",s2_max_index);
	float Heart_Rate = 60.00 * ((100.0 * s1_max_index )/ 256.00)+20;
	
	g_blooddata.heart = Heart_Rate;
	
	float R = (ac_ir*dc_red)/(ac_red*dc_ir);
	float sp02_num =-45.060*R*R+ 30.354 *R + 94.845;
	g_blooddata.SpO2 = sp02_num;
			
}


void blood_Loop(void)
{
	
	//血液锟斤拷息锟斤拷取
	blood_data_update();
	//血液锟斤拷息转锟斤拷
	blood_data_translate();
	//锟斤拷示血液状态锟斤拷息
	g_blooddata.SpO2 = (g_blooddata.SpO2 > 99.99) ? 99.99:g_blooddata.SpO2;
    UsartPrintf(USART_DEBUG,"Heart rate: %3d",g_blooddata.heart);
    UsartPrintf(USART_DEBUG,"SpO2: %0.2f",g_blooddata.SpO2);
    
    xQueueOverwrite(Max30102Handler,&g_blooddata);
	
}

