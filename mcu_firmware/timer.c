#include "device_driver.h"
#include "timer.h"

volatile unsigned long g_sys_tick = 0;

void Timer_Init(void)
{
	SysTick_1ms_Init();
	TIM3_PWM4_Init();
}

void SysTick_1ms_Init(void)
{
	SysTick_Config(SYSCLK / 1000U);
}

void TIM3_PWM4_Init(void)
{
	Macro_Set_Bit(RCC->AHB1ENR, 2U);

	Macro_Write_Block(GPIOC->MODER, 0x3, 0x2, 12U);
	Macro_Write_Block(GPIOC->MODER, 0x3, 0x2, 14U);
	Macro_Write_Block(GPIOC->MODER, 0x3, 0x2, 16U);

	Macro_Write_Block(GPIOC->AFR[0], 0xF, 0x2, 24U);
	Macro_Write_Block(GPIOC->AFR[0], 0xF, 0x2, 28U);
	Macro_Write_Block(GPIOC->AFR[1], 0xF, 0x2, 0U);

	Macro_Set_Bit(RCC->APB1ENR, 1U);

	TIM3->PSC = (unsigned int)(TIMXCLK / 1000000.0 + 0.5) - 1U;
	TIM3->ARR = 20000U - 1U;

	Macro_Write_Block(TIM3->CCMR1, 0x7, 0x6, 4U);
	Macro_Set_Bit(TIM3->CCMR1, 3U);
	Macro_Write_Block(TIM3->CCMR1, 0x7, 0x6, 12U);
	Macro_Set_Bit(TIM3->CCMR1, 11U);
	Macro_Write_Block(TIM3->CCMR2, 0x7, 0x6, 4U);
	Macro_Set_Bit(TIM3->CCMR2, 3U);

	Macro_Set_Bit(TIM3->CCER, 0U);
	Macro_Set_Bit(TIM3->CCER, 4U);
	Macro_Set_Bit(TIM3->CCER, 8U);

	Macro_Set_Bit(TIM3->EGR, 0U);
	Macro_Clear_Bit(TIM3->SR, 0U);

	Macro_Set_Bit(TIM3->CR1, 7U);
	Macro_Set_Bit(TIM3->CR1, 0U);
}


void TIM3_PWM_Set_Pulse(unsigned char ch, unsigned short pulse_us)
{
	switch (ch)
	{
	case 0: TIM3->CCR1 = pulse_us; break;
	case 1: TIM3->CCR2 = pulse_us; break;
	case 2: TIM3->CCR3 = pulse_us; break;
	default: break;
	}
}
