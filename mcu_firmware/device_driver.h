#ifndef DEVICE_DRIVER_H
#define DEVICE_DRIVER_H

// 거의 모든 .c가 이 헤더 하나만 include하도록 레지스터 정의/매크로/설정값을 모아둠
#include "stm32f4xx.h"
#include "option.h"
#include "macro.h"
#include "malloc.h"

extern void Clock_Init(void); // 구현: clock.c - main.c가 부팅 시 한 번만 호출

// isr.c(수신 조립)와 main.c(파싱)가 같은 크기를 알아야 해서 여기서 공유
#define RX_LINE_BUF_SIZE 32

extern void Uart2_Init(int baud);              // 구현: uart.c
extern void Uart2_RX_Interrupt_Enable(int en); // 구현: uart.c - isr.c의 핸들러 등록 전/후 시점 제어용
extern void Uart1_Init(int baud);          // 구현: uart.c
extern void Uart1_Send_Byte(char data);    // 구현: uart.c
extern void Dbg_Log(const char *fmt, ...); // 구현: main.c
#endif
