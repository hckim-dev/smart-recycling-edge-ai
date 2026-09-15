// RTOS 없이 super loop 구조: 매 반복마다 (1) UART 명령 처리 (2) 서보 램프 진행
// (3) 주기적 도어/적재량 보고를 블로킹 없이 번갈아 확인
// 적재율 계산은 bin_filter.c(블랭킹->10샘플 이상치제거평균->비대칭EMA)에 위임한다.
#include "device_driver.h"
#include "timer.h"
#include "ultrasonic.h"
#include "servo.h"
#include "recycle.h"
#include "bin_filter.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern volatile unsigned long g_sys_tick;

extern volatile char g_rx_line[RX_LINE_BUF_SIZE];
extern volatile unsigned char g_rx_line_ready;

// UART1(디버그 전용, PA9=TX)로만 나가는 로그. printf()는 UART2(Jetson 프로토콜)로
// 나가므로 절대 섞이지 않는다. isr.c / recycle.c 등 device_driver.h를 include하는
// 어디서든 바로 호출 가능.
void Dbg_Log(const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (char *p = buf; *p; p++) Uart1_Send_Byte(*p);
}

static void Sys_Init(int baud)
{
    SCB->CPACR |= (0x3 << 10 * 2) | (0x3 << 11 * 2);
    Clock_Init();
    Uart2_Init(baud);
    Uart1_Init(115200); // 디버그 콘솔 (PA9/PA10, 별도 USB-TTL 어댑터 필요)
    setvbuf(stdout, NULL, _IONBF, 0);
}

// 사람이 ComPortMaster 등으로 직접 테스트할 때 쓰는 디버그 명령 경로
// (Jetson 프로토콜인 $DOOR_OPEN 등과는 별개 - Main()에서 '$' 여부로 갈라짐)
static void Handle_Servo_Command(const char *line)
{
    // "reset" 한 줄 입력하면 4개 통 적재율 전부 0%로 수동 리셋
    if (strcmp(line, "reset") == 0)
    {
        BinFilter_Reset_All();
        printf("Bin filter reset: all 4 bins -> 0%%\n");
        return;
    }

    int servo_num = 0;
    int angle = 0;
    int speed = 0;

    int n = sscanf(line, "%d %d %d", &servo_num, &angle, &speed);
    if (n != 3 && n != 2)
    {
        printf("Invalid command: \"%s\" (format: <servo 1~3> <angle 0~180> [speed deg/s], or \"reset\")\n", line);
        return;
    }

    if (servo_num < 1 || servo_num > 3)
    {
        printf("Invalid servo number: %d (1~3만 가능)\n", servo_num);
        return;
    }

    if (angle < 0 || angle > 180)
    {
        printf("Invalid angle: %d (0~180만 가능)\n", angle);
        return;
    }

    if (speed < 0)
    {
        printf("Invalid speed: %d (0 이상만 가능)\n", speed);
        return;
    }

    Servo_Ch ch = (Servo_Ch)(servo_num - 1);
    Servo_Set_Angle_Speed(ch, (unsigned char)angle, (unsigned short)speed);

    if (speed > 0)
        printf("Servo%d -> %d deg (speed %d deg/s)\n", servo_num, angle, speed);
    else
        printf("Servo%d -> %d deg (instant)\n", servo_num, angle);
}

// BinType(0=PAPER,1=CAN,2=PET,3=VINYL) 순서와 정확히 일치하는 초음파 채널 매핑
static const Ultra_Ch BIN_ULTRA_CH[BIN_COUNT] = { ULTRA_CH0, ULTRA_CH1, ULTRA_CH2, ULTRA_CH3 };

static void Report_Door_State(void)
{
    printf("$DOOR_STATE:%s\n", (Recycle_Get_Gate_State() == GATE_OPEN) ? "OPEN" : "CLOSED");
}

// 4개 통 raw 거리를 읽어 bin_filter로 필터링한 뒤, 최종 적재율(%)만 Jetson에 보고
static void Report_Bin_Fill(void)
{
    float raw_paper = Ultra_Read_cm(BIN_ULTRA_CH[0]);
    float raw_can   = Ultra_Read_cm(BIN_ULTRA_CH[1]);
    float raw_pet   = Ultra_Read_cm(BIN_ULTRA_CH[2]);
    float raw_vinyl = Ultra_Read_cm(BIN_ULTRA_CH[3]);

    BinFilter_Update(BIN_PAPER, raw_paper, g_sys_tick);
    BinFilter_Update(BIN_CAN,   raw_can,   g_sys_tick);
    BinFilter_Update(BIN_PET,   raw_pet,   g_sys_tick);
    BinFilter_Update(BIN_VINYL, raw_vinyl, g_sys_tick);

    int p_paper = BinFilter_Get_Percent(BIN_PAPER);
    int p_can   = BinFilter_Get_Percent(BIN_CAN);
    int p_pet   = BinFilter_Get_Percent(BIN_PET);
    int p_vinyl = BinFilter_Get_Percent(BIN_VINYL);

    printf("$BIN:%d/%d/%d/%d\n", p_paper, p_can, p_pet, p_vinyl);
}

// Jetson 쪽에서 보내는 '$'로 시작하는 프로토콜 명령 처리 (분류 결과에 따른 도어 제어)
static void Handle_Jetson_Command(const char *line)
{
    Dbg_Log("[RX] \"%s\"\n", line); // 젯슨 쪽 [SERIAL TX] 로그와 글자 단위로 대조

    if (strncmp(line, "$DOOR_OPEN:", 11) == 0)
    {
        const char *type_str = line + 11;
        RecycleType type = Recycle_Type_From_String(type_str);

        if (type == RECYCLE_NONE)
        {
            printf("Invalid $DOOR_OPEN type: \"%s\" (PET/CAN/PAPER/VINYL만 가능)\n", type_str);
            return;
        }

        Recycle_Door_Open(type);

        int bin_idx = Recycle_Type_To_Bin_Index(type);
        if (bin_idx >= 0)
        {
            BinFilter_Notify_Drop((BinType)bin_idx, g_sys_tick);
        }

        printf("Door open -> %s\n", type_str);
    }
    else if (strcmp(line, "$DOOR_CLOSE") == 0)
    {
        Recycle_Door_Close_Request();
        printf("$DOOR_CLOSE received\n");
    }
    else if (strcmp(line, "$BIN_RESET") == 0)
    {
        // 젯슨 쪽에서도 필요하면 언제든 4개 통 적재율 전부 0%로 리셋 가능
        BinFilter_Reset_All();
        printf("$BIN_RESET done\n");
    }
    else
    {
        printf("Unknown command from Jetson: \"%s\"\n", line);
    }
}

void Main(void)
{
    unsigned long last_tick = 0L;

    Sys_Init(115200);
    printf("\n=== Recycling Sorter (Servo + Ultrasonic + Jetson UART) ===\n");
    printf("Command format: <servo 1~3> <angle 0~180> [speed deg/s]  or \"reset\"\n");
    printf("Jetson protocol: $DOOR_OPEN:<PET|CAN|PAPER|VINYL> / $DOOR_CLOSE / $BIN_RESET\n");

    Timer_Init();
    Ultra_Init();
    Recycle_Init();
    BinFilter_Init();

    // 캘리브레이션: 센서가 통 입구 위 10cm에 장착, 통 깊이는 30cm
    // -> 빈 통(0%) = 10+30 = 40cm, 가득 참(100%) = 10cm
    // 센서 8cm 위 장착 + 통 깊이 30cm -> 빈 통(0%)=38, 가득 참(100%)=8
    BinFilter_Config_Distance(BIN_PAPER, 38.0f, 8.0f);
    BinFilter_Config_Distance(BIN_CAN,   38.0f, 8.0f);
    BinFilter_Config_Distance(BIN_PET,   38.0f, 8.0f);
    BinFilter_Config_Distance(BIN_VINYL, 38.0f, 8.0f);

    Uart2_RX_Interrupt_Enable(1);

    for (;;)
    {
        if (g_rx_line_ready)
        {
            const char *line = (const char *)g_rx_line;
            const char *dollar = strchr(line, '$');

            // 라인 내에 '$'가 존재하면 노이즈가 앞에 섞였더라도 Jetson 명령어로 안전 파싱
            if (dollar != NULL)
                Handle_Jetson_Command(dollar);
            else
                Handle_Servo_Command(line);

            g_rx_line_ready = 0;
        }

        if (Recycle_Gate_State_Changed())
        {
            Report_Door_State();
        }

        Servo_Update();
        Recycle_Update(); // 아래 2개 모터 도착 확인 후 TOP 모터를 뒤이어 출발시킴 (동시 이동 방지)

        {
            int reason = Recycle_Auto_Close_Update();

            if (reason == 1)
                printf("Door close: DOOR_CLOSE command\n");
            else if (reason == 2)
                printf("Door close: max open timeout (10s)\n");
        }

        if ((g_sys_tick - last_tick) >= 2000)
        {
            last_tick = g_sys_tick;

            Report_Bin_Fill();
        }
    }
}