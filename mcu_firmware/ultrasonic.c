#include "device_driver.h"
#include "ultrasonic.h"
#include <stdio.h>

// 4채널이 TIM4 프리런 카운터 하나를 공유 - 완전 동시측정은 아니고 순차 측정
// (동시측정 필요하면 4개 EXTI 인터럽트로 재설계해야 함)
// CH1은 PC0/PC1에서 값이 튀는 문제가 있어 PC9/PC12로 이전함
static const unsigned char TRIG_PIN[ULTRA_COUNT] = {2, 9, 4, 10};
static const unsigned char ECHO_PIN[ULTRA_COUNT] = {3, 12, 5, 11};

// TIM4는 16비트라 unsigned short 뺄셈으로 계산해야 롤오버(0xFFFF->0) 구간도 정확함
static inline unsigned short Tim4_Elapsed_us(unsigned short start)
{
    return (unsigned short)((unsigned short)TIM4->CNT - start);
}

// TIM4가 1us마다 증가하도록 세팅돼 있어 busy-wait만으로 정확한 us 지연이 가능
static void Delay_us(unsigned int us)
{
    unsigned short start = (unsigned short)TIM4->CNT;
    while (Tim4_Elapsed_us(start) < us);
}

// Trig/Echo 4채널 GPIO + 공유 TIM4 카운터를 한 번에 세팅 - main.c에서 부팅 시 1회 호출
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

    // TIM4는 원래 시스템 틱 용도로 안 쓰여서 재활용 - 1us 프리런 카운터로 세팅
    // (SysTick은 1ms 단위라 echo 펄스폭 재기엔 너무 성김)
    Macro_Set_Bit(RCC->APB1ENR, 2U);
    TIM4->PSC = (unsigned int)(TIMXCLK / 1000000.0 + 0.5) - 1U;
    TIM4->ARR = 0xFFFFU;
    Macro_Set_Bit(TIM4->EGR, 0U);
    Macro_Clear_Bit(TIM4->SR, 0U);
    Macro_Set_Bit(TIM4->CR1, 0U);
}

// Trig 펄스 -> Echo 대기 -> 펄스폭 측정까지 한 채널을 동기식으로 끝까지 처리
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
        // 센서 무응답 대비 타임아웃: 2ms(2000us) 내 에코 시작 없으면 센서 미연결로 판단
        if (Tim4_Elapsed_us(t_start) > 2000U)
        {
            return -1.0f;
        }
    }

    t_start = (unsigned short)TIM4->CNT;
    while (GPIOC->IDR & (1U << echo))
    {
        // 수거함 최대 깊이(40cm) 감안: 4ms(4000us, 약 68cm) 초과 시 사거리 초과로 조기 반환
        // 고장/단선 시 루프 블로킹 시간을 최소화하여 서보 램프 및 UART 수신 지연 방어
        if (Tim4_Elapsed_us(t_start) > 4000U)
        {
            return -1.0f;
        }
    }

    pulse_us = Tim4_Elapsed_us(t_start);

    // 58은 음속 340m/s 기준 왕복거리 환산 관용 상수 (cm = us / 58)
    float dist_cm = (float)pulse_us / 58.0f;
    if (dist_cm <= 0.0f || dist_cm > 60.0f)
    {
        return -1.0f; // 수거함 사거리(60cm) 초과 이상치 배제
    }

    return dist_cm;
}