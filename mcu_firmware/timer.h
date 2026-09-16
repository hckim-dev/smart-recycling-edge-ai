#ifndef __TIMER_H__
#define __TIMER_H__

#define TIM_TICK (20U)
#define TIM_FREQ (1000000.0 / TIM_TICK)
#define TIM_1MS_PLS (TIM_FREQ / 1000.0)

void Timer_Init(void);
void SysTick_1ms_Init(void);

void TIM3_PWM4_Init(void);
void TIM3_PWM_Set_Pulse(unsigned char ch, unsigned short pulse_us);

#endif
