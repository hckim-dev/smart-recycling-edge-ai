
#ifndef SERVO_H
#define SERVO_H

typedef enum
{
    SERVO_CH0 = 0,
    SERVO_CH1 = 1,
    SERVO_CH2 = 2,
    SERVO_COUNT = 3
} Servo_Ch;

void Servo_Init(void);
void Servo_Set_Angle(Servo_Ch ch, unsigned char angle);
unsigned char Servo_Get_Angle(Servo_Ch ch);

void Servo_Set_Angle_Speed(Servo_Ch ch, unsigned char angle, unsigned short deg_per_sec);

void Servo_Update(void);

unsigned char Servo_Is_Moving(Servo_Ch ch);

#endif
