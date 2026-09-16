#ifndef DEVICE_DRIVER_H
#define DEVICE_DRIVER_H


#include "stm32f4xx.h"
#include "option.h"
#include "macro.h"
#include "malloc.h"

extern void Clock_Init(void); // 구현: clock.c - main.c가 부팅 시 한 번만 호출


#define RX_LINE_BUF_SIZE 32

extern void Uart2_Init(int baud);              // 구현: uart.c
extern void Uart2_RX_Interrupt_Enable(int en); // 구현: uart.c - isr.c의 핸들러 등록 전/후 시점 제어용
extern void Uart1_Init(int baud);          // 구현: uart.c
extern void Uart1_Send_Byte(char data);    // 구현: uart.c
extern void Dbg_Log(const char *fmt, ...); // 구현: main.c
#endif
