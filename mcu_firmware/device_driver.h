#ifndef DEVICE_DRIVER_H
#define DEVICE_DRIVER_H

// 공통 하드웨어 레지스터 정의, 클럭 매크로 및 드라이버 헤더
#include "stm32f4xx.h"
#include "option.h"
#include "macro.h"
#include "uart.h"

extern void Clock_Init(void); // 구현: clock.c - main.c가 부팅 시 한 번만 호출

// isr.c(수신 조립)와 main.c(파싱)가 공유하는 RX 라인 버퍼 크기
#define RX_LINE_BUF_SIZE 32

extern void Dbg_Log(const char *fmt, ...); // 구현: main.c
#endif
