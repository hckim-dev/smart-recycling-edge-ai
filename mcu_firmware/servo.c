#include "device_driver.h"
#include "servo.h"
#include "timer.h"

extern volatile unsigned long g_sys_tick;

static unsigned char g_servo_angle[SERVO_COUNT] = {0, 0, 0};

static const unsigned char SERVO_INVERT[SERVO_COUNT] = {1, 1, 1};


static float g_servo_current_f[SERVO_COUNT];
static unsigned char g_servo_target[SERVO_COUNT];
static float g_servo_speed_deg_per_ms[SERVO_COUNT];
static unsigned long g_servo_last_tick[SERVO_COUNT];

static unsigned char Servo_Apply_Invert(Servo_Ch ch, unsigned char angle)
{
    if (SERVO_INVERT[ch])
    {
        return (unsigned char)(180 - angle);
    }
    return angle;
}

void Servo_Init(void)
{
    TIM3_PWM4_Init();
    Servo_Set_Angle(SERVO_CH0, 90);
    Servo_Set_Angle(SERVO_CH1, 90);
    Servo_Set_Angle(SERVO_CH2, 90);

    for (int ch = 0; ch < SERVO_COUNT; ch++)
    {
        g_servo_current_f[ch] = 90.0f;
        g_servo_target[ch] = 90;
        g_servo_speed_deg_per_ms[ch] = 0.0f;
    }
}

void Servo_Set_Angle(Servo_Ch ch, unsigned char angle)
{
    if (ch >= SERVO_COUNT) return;
    if (angle > 180) angle = 180;

    g_servo_angle[ch] = angle; 

    unsigned char eff_angle = Servo_Apply_Invert(ch, angle);
    unsigned short pulse_us = 500 + ((unsigned short)eff_angle * 2000 / 180);
    TIM3_PWM_Set_Pulse((unsigned char)ch, pulse_us);
}

unsigned char Servo_Get_Angle(Servo_Ch ch)
{
    if (ch >= SERVO_COUNT) return 0;
    return g_servo_angle[ch];
}

void Servo_Set_Angle_Speed(Servo_Ch ch, unsigned char angle, unsigned short deg_per_sec)
{
    if (ch >= SERVO_COUNT) return;
    if (angle > 180) angle = 180;

    g_servo_target[ch] = angle;

    if (deg_per_sec == 0)
    {
        Servo_Set_Angle(ch, angle);
        g_servo_current_f[ch] = (float)angle;
        g_servo_speed_deg_per_ms[ch] = 0.0f;
        return;
    }

    g_servo_speed_deg_per_ms[ch] = (float)deg_per_sec / 1000.0f;
    g_servo_last_tick[ch] = g_sys_tick;
}


unsigned char Servo_Is_Moving(Servo_Ch ch)
{
    if (ch >= SERVO_COUNT) return 0;
    return (g_servo_speed_deg_per_ms[ch] > 0.0f) ? 1 : 0;
}

void Servo_Update(void)
{
    for (int ch = 0; ch < SERVO_COUNT; ch++)
    {
        if (g_servo_speed_deg_per_ms[ch] <= 0.0f) continue;

        unsigned long now = g_sys_tick;
        unsigned long elapsed_ms = now - g_servo_last_tick[ch];
        if (elapsed_ms == 0) continue;
        g_servo_last_tick[ch] = now;

        float target = (float)g_servo_target[ch];
        float step = g_servo_speed_deg_per_ms[ch] * (float)elapsed_ms;

        if (g_servo_current_f[ch] < target)
        {
            g_servo_current_f[ch] += step;
            if (g_servo_current_f[ch] >= target)
            {
                g_servo_current_f[ch] = target;
                g_servo_speed_deg_per_ms[ch] = 0.0f;
            }
        }
        else
        {
            g_servo_current_f[ch] -= step;
            if (g_servo_current_f[ch] <= target)
            {
                g_servo_current_f[ch] = target;
                g_servo_speed_deg_per_ms[ch] = 0.0f;
            }
        }

        unsigned char cur_angle = (unsigned char)(g_servo_current_f[ch] + 0.5f);
        g_servo_angle[ch] = cur_angle; // 논리 각도 그대로 저장

        unsigned char eff_angle = Servo_Apply_Invert((Servo_Ch)ch, cur_angle);
        unsigned short pulse_us = 500 + ((unsigned short)eff_angle * 2000 / 180);
        TIM3_PWM_Set_Pulse((unsigned char)ch, pulse_us);
    }
}