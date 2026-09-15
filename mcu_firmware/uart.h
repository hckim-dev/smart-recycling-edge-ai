#ifndef UART_H
#define UART_H

#ifdef __cplusplus
extern "C" {
#endif

// USART1: 보조/디버그 콘솔 인터페이스 (PA9=TX, PA10=RX, 115200bps)
void Uart1_Init(int baud);
void Uart1_Send_Byte(char data);
char Uart1_Get_Char(void);
char Uart1_Get_Pressed(void);

// USART2: Jetson 메인 통신 인터페이스 (PA2=TX, PA3=RX, 115200bps)
void Uart2_Init(int baud);
void Uart2_RX_Interrupt_Enable(int en);

#ifdef __cplusplus
}
#endif

#endif // UART_H

