#include "device_driver.h"
#include "recycle.h"
#include "servo.h"
#include <string.h>

// Jetson이 판별한 재질(PET/CAN/PAPER/VINYL)을 받아 3단 분류 트리(모터 3개)를 구동하는 상위 로직.
// TOP(투입구)이 먼저 (종이,캔) 그룹 vs (페트,비닐) 그룹으로 가르고,
// 그 아래 PETVINYL_CH/PAPERCAN_CH 모터가 각자 맡은 그룹을 다시 2개로 갈라 최종 4분류를 완성한다.
extern volatile unsigned long g_sys_tick;

#define TOP_CH       SERVO_CH0  // PC6 - 투입구: 종이/캔 그룹 vs 페트/비닐 그룹 분기
#define PETVINYL_CH  SERVO_CH1  // PC7 - 페트/비닐 그룹 세부분류: 기본(=PET) / 비닐
#define PAPERCAN_CH  SERVO_CH2  // PC8 - 종이/캔 그룹 세부분류: 기본(=종이) / 캔

// TOP(PC6): 기본 90도(닫힘), 30도->종이/캔 그룹, 150도->페트/비닐 그룹
#define TOP_ANGLE_NEUTRAL      90
#define TOP_ANGLE_PAPER_CAN    30
#define TOP_ANGLE_PET_VINYL    150

// PETVINYL(PC7): 기본 130도(=PET 낙하 위치), 30도->비닐 낙하
#define PETVINYL_ANGLE_DEFAULT_PET  130
#define PETVINYL_ANGLE_VINYL        30

// PAPERCAN(PC8): 기본 150도(=종이 낙하 위치), 50도->캔 낙하
#define PAPERCAN_ANGLE_DEFAULT_PAPER  150
#define PAPERCAN_ANGLE_CAN             50

// 도어 서보 회전 속도(초당 각도) - 너무 빠르면 기구가 부러지는 문제로 낮춤.
// 원래 "무제한 최고속"이었던 걸 체감상 60% 정도로 낮춘 값. 더 느리게/빠르게 원하면 이 숫자만 조절하면 됨.
#define DOOR_SERVO_SPEED_DEG_PER_SEC 120

static volatile GateState s_gate_state = GATE_CLOSED;
static volatile GateState s_gate_state_prev = GATE_CLOSED;

// MIN_OPEN: Jetson이 DOOR_CLOSE를 너무 일찍 보내도 최소 이 시간까지는 경로를 유지 (투입 시간 보장)
// MAX_OPEN: Jetson이 DOOR_CLOSE를 못 보내는 상황(오탐/통신 유실) 대비 failsafe
#define DOOR_MIN_OPEN_MS 2000
#define DOOR_MAX_OPEN_MS 10000

static RecycleType s_open_type = RECYCLE_NONE;
static unsigned long s_gate_open_tick = 0;
static volatile unsigned char s_close_requested = 0;

// TOP 모터를 아래 두 모터보다 먼저/동시에 움직이지 않기 위한 대기 상태.
// Recycle_Door_Open()은 아래 두 모터만 먼저 이동시키고, TOP은 여기 걸어만 둔다.
// -> Recycle_Update()가 매 루프 아래 두 모터의 도착 여부를 확인해서 TOP을 나중에 출발시킴
static volatile unsigned char s_top_move_pending = 0;
static RecycleType s_pending_route_type = RECYCLE_NONE;

// 품목의 그룹(종이/캔 vs 페트/비닐)에 따른 TOP 목표각
static unsigned char Top_Angle_For(RecycleType type)
{
    switch (type)
    {
    case RECYCLE_PAPER:
    case RECYCLE_CAN:
        return TOP_ANGLE_PAPER_CAN;
    case RECYCLE_PET:
    case RECYCLE_VINYL:
        return TOP_ANGLE_PET_VINYL;
    default:
        return TOP_ANGLE_NEUTRAL;
    }
}

// 아래쪽 세부분류 모터(PAPERCAN_CH/PETVINYL_CH) 2개만 먼저 목표각으로 이동.
// TOP_CH는 여기서 건드리지 않는다 - Recycle_Update()가 이 둘의 도착을 확인한 뒤 출발시킴.
// Servo_Set_Angle_Speed로 램프 이동시킴 (DOOR_SERVO_SPEED_DEG_PER_SEC로 부드럽게)
// -> Main() 루프에서 Servo_Update()가 계속 불려야 실제로 움직임이 진행됨
static void Set_Bottom_Route(RecycleType type)
{
    switch (type)
    {
    case RECYCLE_PAPER:
        Servo_Set_Angle_Speed(PAPERCAN_CH, PAPERCAN_ANGLE_DEFAULT_PAPER, DOOR_SERVO_SPEED_DEG_PER_SEC);
        Servo_Set_Angle_Speed(PETVINYL_CH, PETVINYL_ANGLE_DEFAULT_PET, DOOR_SERVO_SPEED_DEG_PER_SEC);
        break;
    case RECYCLE_CAN:
        Servo_Set_Angle_Speed(PAPERCAN_CH, PAPERCAN_ANGLE_CAN, DOOR_SERVO_SPEED_DEG_PER_SEC);
        Servo_Set_Angle_Speed(PETVINYL_CH, PETVINYL_ANGLE_DEFAULT_PET, DOOR_SERVO_SPEED_DEG_PER_SEC);
        break;
    case RECYCLE_PET:
        Servo_Set_Angle_Speed(PETVINYL_CH, PETVINYL_ANGLE_DEFAULT_PET, DOOR_SERVO_SPEED_DEG_PER_SEC);
        Servo_Set_Angle_Speed(PAPERCAN_CH, PAPERCAN_ANGLE_DEFAULT_PAPER, DOOR_SERVO_SPEED_DEG_PER_SEC);
        break;
    case RECYCLE_VINYL:
        Servo_Set_Angle_Speed(PETVINYL_CH, PETVINYL_ANGLE_VINYL, DOOR_SERVO_SPEED_DEG_PER_SEC);
        Servo_Set_Angle_Speed(PAPERCAN_CH, PAPERCAN_ANGLE_DEFAULT_PAPER, DOOR_SERVO_SPEED_DEG_PER_SEC);
        break;
    default:
        break;
    }
}

static void Set_Neutral(void)
{
    Servo_Set_Angle_Speed(TOP_CH, TOP_ANGLE_NEUTRAL, DOOR_SERVO_SPEED_DEG_PER_SEC);
    Servo_Set_Angle_Speed(PETVINYL_CH, PETVINYL_ANGLE_DEFAULT_PET, DOOR_SERVO_SPEED_DEG_PER_SEC);
    Servo_Set_Angle_Speed(PAPERCAN_CH, PAPERCAN_ANGLE_DEFAULT_PAPER, DOOR_SERVO_SPEED_DEG_PER_SEC);
}

// 전원 인가 직후 3모터를 기본 위치로 맞춰 상태 불일치를 방지
// (부팅 시 초기 위치잡기는 굳이 천천히 갈 필요 없어 즉시이동 그대로 둠)
void Recycle_Init(void)
{
    Servo_Init();

    s_gate_state = GATE_CLOSED;
    s_gate_state_prev = s_gate_state;
}

// Jetson이 UART로 보내는 문자열 프로토콜과 내부 enum 사이의 경계 지점
RecycleType Recycle_Type_From_String(const char *s)
{
    if (strcmp(s, "PET") == 0)   return RECYCLE_PET;
    if (strcmp(s, "CAN") == 0)   return RECYCLE_CAN;
    if (strcmp(s, "PAPER") == 0) return RECYCLE_PAPER;
    if (strcmp(s, "VINYL") == 0) return RECYCLE_VINYL;
    return RECYCLE_NONE;
}

// 품목에 맞는 경로로 3모터를 이동(DOOR_SERVO_SPEED_DEG_PER_SEC로 부드럽게) - 물리적 게이트가 따로 없어 이 각도 자체가 "열림"
// 순서 보장: 아래쪽 세부분류 모터(PAPERCAN_CH/PETVINYL_CH) 2개를 먼저 출발시키고,
// TOP_CH(투입구)는 Recycle_Update()가 그 2개의 도착을 확인한 뒤에 출발시킨다.
// (동시에 움직이면 TOP이 먼저 열려서 세부분류 위치가 안 잡힌 채 쓰레기가 떨어질 수 있음)
void Recycle_Door_Open(RecycleType type)
{
    if (type == RECYCLE_NONE)
    {
        return;
    }

    Set_Bottom_Route(type);

    s_pending_route_type = type;
    s_top_move_pending = 1; // TOP은 아직 출발 안 시킴 - Recycle_Update()가 이어받음

    s_gate_state = GATE_OPEN;
    s_open_type = type;
    s_gate_open_tick = g_sys_tick;
    s_close_requested = 0;
}

// 아래 두 모터가 목표각에 다 도착했을 때만 TOP_CH를 출발시킨다.
// main 루프에서 Servo_Update() 직후 매번 호출되어야 함.
void Recycle_Update(void)
{
    if (!s_top_move_pending)
    {
        return;
    }

    if (!Servo_Is_Moving(PAPERCAN_CH) && !Servo_Is_Moving(PETVINYL_CH))
    {
        Servo_Set_Angle_Speed(TOP_CH, Top_Angle_For(s_pending_route_type), DOOR_SERVO_SPEED_DEG_PER_SEC);
        s_top_move_pending = 0;
    }
}

static void Do_Close(void)
{
    s_top_move_pending = 0; // 닫는 도중이면 TOP 지연 출발 예약은 취소 (Set_Neutral이 TOP도 즉시 되돌림)
    Set_Neutral();
    s_gate_state = GATE_CLOSED;
    s_open_type = RECYCLE_NONE;
    s_close_requested = 0;
}

// Jetson이 $DOOR_CLOSE(카메라에서 물체 사라짐)를 보냈을 때 호출.
// 최소 개방시간을 못 채웠으면 바로 닫지 않고 플래그만 세워 Recycle_Auto_Close_Update가 나중에 닫는다.
void Recycle_Door_Close_Request(void)
{
    s_close_requested = 1;
}

GateState Recycle_Get_Gate_State(void)
{
    return s_gate_state;
}

// main 루프가 매번 상태를 출력하지 않고 "바뀐 순간"에만 리포트하도록 하는 엣지 감지
int Recycle_Gate_State_Changed(void)
{
    if (s_gate_state != s_gate_state_prev)
    {
        s_gate_state_prev = s_gate_state;
        return 1;
    }
    return 0;
}

// main 루프가 주기적으로 호출: 0=유지, 1=DOOR_CLOSE 명령으로 닫음, 2=최대개방 타임아웃으로 닫음
int Recycle_Auto_Close_Update(void)
{
    if (s_gate_state != GATE_OPEN)
    {
        return 0;
    }

    unsigned long elapsed_open = g_sys_tick - s_gate_open_tick;

    if (elapsed_open >= DOOR_MAX_OPEN_MS)
    {
        Do_Close();
        return 2;
    }

    if (s_close_requested && elapsed_open >= DOOR_MIN_OPEN_MS)
    {
        Do_Close();
        return 1;
    }

    return 0;
}

RecycleType Recycle_Get_Open_Type(void)
{
    return s_open_type;
}

// bin_filter.h의 BinType과 값이 호환되게 맞춤: 0=PAPER, 1=CAN, 2=PET, 3=VINYL
// (recycle.c가 bin_filter.h를 include하지 않도록 int로만 반환 - main.c에서 BinType으로 캐스팅해 사용)
int Recycle_Type_To_Bin_Index(RecycleType type)
{
    switch (type)
    {
    case RECYCLE_PAPER: return 0; // BIN_PAPER
    case RECYCLE_CAN:   return 1; // BIN_CAN
    case RECYCLE_PET:   return 2; // BIN_PET
    case RECYCLE_VINYL: return 3; // BIN_VINYL
    default:            return -1;
    }
}