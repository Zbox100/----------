#include "blood.h"
#include "usart.h"
#include "head.h"
/**
  ******************************************************************************
  * @file    blood.c
  * @author  ������������
  * @brief   ѪҺ������ת������
  ******************************************************************************
  */

#include "FreeRTOS.h"
#include "queue.h"

extern QueueHandle_t Max30102Handler;
uint16_t g_fft_index = 0;         	 	//fft��������±�

struct compx s1[FFT_N+16];           	//FFT������������S[1]��ʼ��ţ����ݴ�С�Լ�����
struct compx s2[FFT_N+16];           	//FFT������������S[1]��ʼ��ţ����ݴ�С�Լ�����

struct
{
	float 	Hp	;			//Ѫ�쵰��	
	float 	HpO2;			//����Ѫ�쵰��
	
}g_BloodWave;//ѪҺ��������

BloodData g_blooddata = {0};					//ѪҺ���ݴ洢

#define CORRECTED_VALUE			47   			//�궨ѪҺ��������

/*funcation start ------------------------------------------------------------*/
//ѪҺ�����Ϣ����
void blood_data_update(void)
{
	//��־λ��ʹ��ʱ ��ȡFIFO
	g_fft_index=0;
	while(g_fft_index < FFT_N)
	{
		while(MAX30102_INTPin_Read()==0)
		{
			//��ȡFIFO
			max30102_read_fifo();  //read from MAX30102 FIFO2
			//������д��fft���벢������
			if(g_fft_index < FFT_N)
			{
				//������д��fft���벢������
				s1[g_fft_index].real = fifo_red;
				s1[g_fft_index].imag= 0;
				s2[g_fft_index].real = fifo_ir;
				s2[g_fft_index].imag= 0;
				g_fft_index++;
			}
		}
	}
}

//ѪҺ��Ϣת��
void blood_data_translate(void)
{	
	float n_denom;
	uint16_t i;
	//ֱ���˲�
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
	
	//�ƶ�ƽ���˲�
//	UsartPrintf(USART_DEBUG,"***********8 pt Moving Average red******************************************************\r\n");
//	
	for(i = 1;i < FFT_N-1;i++) 
	{
			n_denom= ( s1[i-1].real + 2*s1[i].real + s1[i+1].real);
			s1[i].real=  n_denom/4.00; 
			
			n_denom= ( s2[i-1].real + 2*s2[i].real + s2[i+1].real);
			s2[i].real=  n_denom/4.00; 			
	}
	//�˵�ƽ���˲�
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
	//��ʼ�任��ʾ	
	g_fft_index = 0;	
	//���ٸ���Ҷ�任
	FFT(s1);
	FFT(s2);
	//��ƽ��
//	UsartPrintf(USART_DEBUG,"��ʼFFT�㷨****************************************************************************************************\r\n");
	for(i = 0;i < FFT_N;i++) 
	{
		s1[i].real=sqrtf(s1[i].real*s1[i].real+s1[i].imag*s1[i].imag);
		s2[i].real=sqrtf(s2[i].real*s2[i].real+s2[i].imag*s2[i].imag);
	}
	//���㽻������
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
//	UsartPrintf(USART_DEBUG,"����FFT�㷨***************************************************************************************************************\r\n");
	for(i = 0;i < 50;i++) 
	{
		if(s1[i].real<=10)
			break;
	}
	
	//UsartPrintf(USART_DEBUG,"%d\r\n",(int)i);
	//��ȡ��ֵ��ĺ�����  �������������Ϊ 
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
	
	//ѪҺ��Ϣ��ȡ
	blood_data_update();
	//ѪҺ��Ϣת��
	blood_data_translate();
	//��ʾѪҺ״̬��Ϣ
	g_blooddata.SpO2 = (g_blooddata.SpO2 > 99.99) ? 99.99:g_blooddata.SpO2;
    UsartPrintf(USART_DEBUG,"ָ������%3d",g_blooddata.heart);
    UsartPrintf(USART_DEBUG,"ָ��Ѫ��%0.2f",g_blooddata.SpO2);
    
    xQueueSend(Max30102Handler,&g_blooddata,portMAX_DELAY);
	
}

