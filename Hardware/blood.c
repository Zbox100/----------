#include "blood.h"
#include "usart.h"
#include "head.h"
/**
  ******************************************************************************
  * @file    blood.c
  * @author  ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
  * @brief   ÑªÒºï¿½ï¿½ï¿½ï¿½ï¿½ï¿½×ªï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"                         //??vTaskDelay

extern QueueHandle_t Max30102Handler;
uint16_t g_fft_index = 0;         	 	//fftï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Â±ï¿½

struct compx s1[FFT_N+16];           	//FFTï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½S[1]ï¿½ï¿½Ê¼ï¿½ï¿½Å£ï¿½ï¿½ï¿½ï¿½Ý´ï¿½Ð¡ï¿½Ô¼ï¿½ï¿½ï¿½ï¿½ï¿½
struct compx s2[FFT_N+16];           	//FFTï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½S[1]ï¿½ï¿½Ê¼ï¿½ï¿½Å£ï¿½ï¿½ï¿½ï¿½Ý´ï¿½Ð¡ï¿½Ô¼ï¿½ï¿½ï¿½ï¿½ï¿½

struct
{
	float 	Hp	;			//Ñªï¿½ìµ°ï¿½ï¿½	
	float 	HpO2;			//ï¿½ï¿½ï¿½ï¿½Ñªï¿½ìµ°ï¿½ï¿½
	
}g_BloodWave;//ÑªÒºï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½

BloodData g_blooddata = {0};					//ÑªÒºï¿½ï¿½ï¿½Ý´æ´¢

#define CORRECTED_VALUE			47   			//ï¿½ê¶¨ÑªÒºï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½

/*funcation start ------------------------------------------------------------*/
//ÑªÒºï¿½ï¿½ï¿½ï¿½ï¿½Ï¢ï¿½ï¿½ï¿½ï¿½
void blood_data_update(void)
{
	uint16_t wait_ms = 0;                 //Á¬ÐøµÈ´ýÐÂÑù±¾µÄÊ±³¤(ms),ÓÃÓÚ³¬Ê±ÍË³ö
	//±êÖ¾Î»£¬Ê¹ÓÃÊ± £¬¶ÁÈ¡FIFO
	g_fft_index=0;
	while(g_fft_index < FFT_N)
	{
		if(MAX30102_INTPin_Read()!=0)      //INTÎª¸ß:FIFOÔÝÎÞÐÂÊý¾Ý
		{
			if(wait_ms >= 2000)            //Á¬Ðø2sÃ»ÓÐÐÂÑù±¾:ÅÐ¶¨Ä£¿éÒì³£,·ÅÆú±¾´Î²É¼¯
			{
				break;
			}
			vTaskDelay(1);                 //ÈÃ³öCPU 1ms,±ÜÃâÎÞÑÓÊ±Ã¦µÈ°ÑÐ­×÷Ê½µ÷¶ÈÆ÷¹ÒËÀ
			wait_ms++;
			continue;
		}
		wait_ms = 0;                       //ÓÐÐÂÊý¾Ý,Çå¿ÕµÈ´ý¼ÆÊ±
		//¶ÁÈ¡FIFO
		max30102_read_fifo();              //read from MAX30102 FIFO2
		//²ÉÑùºóÐ´ÈëfftÊäÈë²¢µÝÔöÏÂ±ê
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

//ÑªÒºï¿½ï¿½Ï¢×ªï¿½ï¿½
void blood_data_translate(void)
{	
	float n_denom;
	uint16_t i;
	//Ö±ï¿½ï¿½ï¿½Ë²ï¿½
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
	
	//ï¿½Æ¶ï¿½Æ½ï¿½ï¿½ï¿½Ë²ï¿½
//	UsartPrintf(USART_DEBUG,"***********8 pt Moving Average red******************************************************\r\n");
//	
	for(i = 1;i < FFT_N-1;i++) 
	{
			n_denom= ( s1[i-1].real + 2*s1[i].real + s1[i+1].real);
			s1[i].real=  n_denom/4.00; 
			
			n_denom= ( s2[i-1].real + 2*s2[i].real + s2[i+1].real);
			s2[i].real=  n_denom/4.00; 			
	}
	//ï¿½Ëµï¿½Æ½ï¿½ï¿½ï¿½Ë²ï¿½
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
	//ï¿½ï¿½Ê¼ï¿½ä»»ï¿½ï¿½Ê¾	
	g_fft_index = 0;	
	//ï¿½ï¿½ï¿½Ù¸ï¿½ï¿½ï¿½Ò¶ï¿½ä»»
	FFT(s1);
	FFT(s2);
	//ï¿½ï¿½Æ½ï¿½ï¿½
//	UsartPrintf(USART_DEBUG,"ï¿½ï¿½Ê¼FFTï¿½ã·¨****************************************************************************************************\r\n");
	for(i = 0;i < FFT_N;i++) 
	{
		s1[i].real=sqrtf(s1[i].real*s1[i].real+s1[i].imag*s1[i].imag);
		s2[i].real=sqrtf(s2[i].real*s2[i].real+s2[i].imag*s2[i].imag);
	}
	//ï¿½ï¿½ï¿½ã½»ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½
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
//	UsartPrintf(USART_DEBUG,"ï¿½ï¿½ï¿½ï¿½FFTï¿½ã·¨***************************************************************************************************************\r\n");
	for(i = 0;i < 50;i++) 
	{
		if(s1[i].real<=10)
			break;
	}
	
	//UsartPrintf(USART_DEBUG,"%d\r\n",(int)i);
	//ï¿½ï¿½È¡ï¿½ï¿½Öµï¿½ï¿½Äºï¿½ï¿½ï¿½ï¿½ï¿½  ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½Îª 
	int s1_max_index = find_max_num_index(s1, 30);
	int s2_max_index = find_max_num_index(s2, 30);
//	UsartPrintf(USART_DEBUG,"%d\r\n",s1_max_index);
//	UsartPrintf(USART_DEBUG,"%d\r\n",s2_max_index);
	float Heart_Rate = 60.00 * ((100.0 * s1_max_index )/ 512.00)+20;
	
	g_blooddata.heart = Heart_Rate;
	
	float R = (ac_ir*dc_red)/(ac_red*dc_ir);
	float sp02_num =-45.060*R*R+ 30.354 *R + 94.845;
	g_blooddata.SpO2 = sp02_num;
			
}


void blood_Loop(void)
{
	
	//ÑªÒºï¿½ï¿½Ï¢ï¿½ï¿½È¡
	blood_data_update();
	//ÑªÒºï¿½ï¿½Ï¢×ªï¿½ï¿½
	blood_data_translate();
	//ï¿½ï¿½Ê¾ÑªÒº×´Ì¬ï¿½ï¿½Ï¢
	g_blooddata.SpO2 = (g_blooddata.SpO2 > 99.99) ? 99.99:g_blooddata.SpO2;
    UsartPrintf(USART_DEBUG,"Ö¸ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½%3d",g_blooddata.heart);
    UsartPrintf(USART_DEBUG,"Ö¸ï¿½ï¿½Ñªï¿½ï¿½%0.2f",g_blooddata.SpO2);
    
    xQueueOverwrite(Max30102Handler,&g_blooddata);
	
}

