#include "device_driver.h"
#include "ultrasonic.h"
#include <stdio.h>


static const unsigned char TRIG_PIN[ULTRA_COUNT] = {2, 9, 4, 10};
static const unsigned char ECHO_PIN[ULTRA_COUNT] = {3, 12, 5, 11};

static inline unsigned short Tim4_Elapsed_us(unsigned short start)
{
    return (unsigned short)((unsigned short)TIM4->CNT - start);
}

static void Delay_us(unsigned int us)
{
    unsigned short start = (unsigned short)TIM4->CNT;
    while (Tim4_Elapsed_us(start) < us);
}

void Ultra_Init(void)
{

    Macro_Set_Bit(RCC->AHB1ENR, 2U);

    for (int ch = 0; ch < ULTRA_COUNT; ch++)
    {
        unsigned char trig = TRIG_PIN[ch];
        unsigned char echo = ECHO_PIN[ch];

        Macro_Write_Block(GPIOC->MODER, 0x3, 0x1, trig * 2);
        Macro_Clear_Bit(GPIOC->OTYPER, trig);
        Macro_Clear_Bit(GPIOC->ODR, trig);

        Macro_Write_Block(GPIOC->MODER, 0x3, 0x0, echo * 2);
        Macro_Write_Block(GPIOC->PUPDR, 0x3, 0x2, echo * 2);
    }

    Macro_Set_Bit(RCC->APB1ENR, 2U);
    TIM4->PSC = (unsigned int)(TIMXCLK / 1000000.0 + 0.5) - 1U;
    TIM4->ARR = 0xFFFFU;
    Macro_Set_Bit(TIM4->EGR, 0U);
    Macro_Clear_Bit(TIM4->SR, 0U);
    Macro_Set_Bit(TIM4->CR1, 0U);
}


float Ultra_Read_cm(Ultra_Ch ch)
{
    if (ch >= ULTRA_COUNT) return -1.0f;

    unsigned char trig = TRIG_PIN[ch];
    unsigned char echo = ECHO_PIN[ch];
    unsigned short t_start, pulse_us;

    Macro_Clear_Bit(GPIOC->ODR, trig);
    Delay_us(2);
    Macro_Set_Bit(GPIOC->ODR, trig);
    Delay_us(10);
    Macro_Clear_Bit(GPIOC->ODR, trig);

    t_start = (unsigned short)TIM4->CNT;
    while (!(GPIOC->IDR & (1U << echo)))
    {
        if (Tim4_Elapsed_us(t_start) > 10000U) 
        {
            return -1.0f;
        }
    }

    t_start = (unsigned short)TIM4->CNT;

    while (GPIOC->IDR & (1U << echo))
    {
        if (Tim4_Elapsed_us(t_start) > 30000U) 
        {
            return -1.0f;
        }
    }

    pulse_us = Tim4_Elapsed_us(t_start);

    // 58은 음속 340m/s 기준 왕복거리 환산 관용 상수 (cm = us / 58)
    return (float)pulse_us / 58.0f;
}