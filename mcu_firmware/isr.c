// 함수 이름이 벡터표(스타트업/링커 스크립트)에 고정돼 있어 이 이름 그대로
// 정의만 하면 자동으로 연결됨(약한 심볼 오버라이드) - 여기서 등록하는 절차 없음
#include "device_driver.h"
#include <stdio.h>

extern volatile unsigned long g_sys_tick;

volatile char g_rx_line[RX_LINE_BUF_SIZE];
volatile unsigned char g_rx_line_ready = 0;
static unsigned char s_rx_idx = 0;

// 처리 함수가 없는 인터럽트가 걸렸을 때의 안전장치 - 원인 모를 오동작 대신
// 예외 번호를 남기고 확실히 멈춰서 디버깅 가능한 상태로 만듦
void _Invalid_ISR(void)
{

	unsigned int r = Macro_Extract_Area(SCB->ICSR, 0x1ff, 0);
	printf("\nInvalid_Exception: %d!\n", r);
	printf("Invalid_ISR: %d!\n", r - 16);
	for (;;)
		;
}

// 별도 플래그 클리어가 없는 이유: SysTick은 읽기만 해도 COUNTFLAG/NVIC pending이
// 자동으로 정리되는 코어 타이머라 여기선 카운트만 올리면 됨
void SysTick_Handler(void)
{
	g_sys_tick++;
}

// 한 글자씩 들어오는 UART 수신을 한 줄 버퍼로 조립만 함 - 실제 명령 해석은
// ISR을 짧게 유지하기 위해 main.c 루프로 미룸
void USART2_IRQHandler(void)
{
	// SR 레지스터를 먼저 읽고 DR을 읽어야 ORE(Overrun Error), FE, NE 등의 하드웨어 락업이 클리어됨
	volatile unsigned int sr = USART2->SR;
	unsigned char ch = (unsigned char)(USART2->DR & 0xFF);
	(void)sr;

	// 메인 루프가 이전 줄을 아직 못 읽었으면 덮어쓰지 않고 그냥 버림
	if (g_rx_line_ready)
		return;

	// 전원 인가 시의 노이즈 펄스나 비정상 글리치 문자 필터링 (버퍼 첫 글자는 출력 가능한 ASCII여야 함)
	if (s_rx_idx == 0 && (ch < 32 || ch > 126))
		return;

	if (ch == '\r' || ch == '\n')
	{
		if (s_rx_idx > 0)
		{
			g_rx_line[s_rx_idx] = '\0';
			g_rx_line_ready = 1;
			s_rx_idx = 0;
		}
	}
	else if (s_rx_idx < (RX_LINE_BUF_SIZE - 1))
	{
		g_rx_line[s_rx_idx++] = (char)ch;
	}
	else
	{
		// 개행 없이 버퍼가 꽉 찼으면 쓰레기 데이터 누적으로 판단하여 버퍼 리셋
		s_rx_idx = 0;
	}
}
