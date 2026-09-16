#ifndef ULTRASONIC_H
#define ULTRASONIC_H

typedef enum
{
    ULTRA_CH0 = 0,
    ULTRA_CH1 = 1,
    ULTRA_CH2 = 2,
    ULTRA_CH3 = 3,
    ULTRA_COUNT = 4
} Ultra_Ch;

void Ultra_Init(void);

float Ultra_Read_cm(Ultra_Ch ch);

#endif
