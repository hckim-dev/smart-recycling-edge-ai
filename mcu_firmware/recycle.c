#include "device_driver.h"
#include "recycle.h"
#include "servo.h"
#include <string.h>


extern volatile unsigned long g_sys_tick;
static volatile unsigned char s_bottom_close_pending = 0;
#define TOP_CH       SERVO_CH0  // PC6 - 투입구: 종이/캔 그룹 vs 페트/비닐 그룹 분기
#define PETVINYL_CH  SERVO_CH1  // PC7 - 페트/비닐 그룹 세부분류: 기본(=PET) / 비닐
#define PAPERCAN_CH  SERVO_CH2  // PC8 - 종이/캔 그룹 세부분류: 기본(=종이) / 캔


#define TOP_ANGLE_NEUTRAL      90
#define TOP_ANGLE_PAPER_CAN    30
#define TOP_ANGLE_PET_VINYL    150


#define PETVINYL_ANGLE_DEFAULT_PET  130
#define PETVINYL_ANGLE_VINYL        30


#define PAPERCAN_ANGLE_DEFAULT_PAPER  150
#define PAPERCAN_ANGLE_CAN             50

// 도어 서보 회전 속도(초당 각도) - 너무 빠르면 기구가 부러지는 문제로 낮춤.
#define DOOR_SERVO_SPEED_DEG_PER_SEC 120

static volatile GateState s_gate_state = GATE_CLOSED;
static volatile GateState s_gate_state_prev = GATE_CLOSED;

#define DOOR_MIN_OPEN_MS 2000
#define DOOR_MAX_OPEN_MS 10000

static RecycleType s_open_type = RECYCLE_NONE;
static unsigned long s_gate_open_tick = 0;
static volatile unsigned char s_close_requested = 0;

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

    // 3개 모터가 동시에 움직이면 피크 돌입전류(1.5A~2.5A)로 인해
    // 5V 전압 강하(Brown-Out Reset)가 발생하므로, 200~250ms 간격으로 1개씩 순차 정렬한다.
    Servo_Set_Angle(PETVINYL_CH, PETVINYL_ANGLE_DEFAULT_PET);
    for (volatile unsigned long i = 0; i < 800000; i++);

    Servo_Set_Angle(PAPERCAN_CH, PAPERCAN_ANGLE_DEFAULT_PAPER);
    for (volatile unsigned long i = 0; i < 800000; i++);

    Servo_Set_Angle(TOP_CH, TOP_ANGLE_NEUTRAL);
    for (volatile unsigned long i = 0; i < 800000; i++);

    s_gate_state = GATE_CLOSED;
    s_gate_state_prev = s_gate_state;
    s_top_move_pending = 0;
    s_bottom_close_pending = 0;
}

RecycleType Recycle_Type_From_String(const char *s)
{
    if (strcmp(s, "PET") == 0)   return RECYCLE_PET;
    if (strcmp(s, "CAN") == 0)   return RECYCLE_CAN;
    if (strcmp(s, "PAPER") == 0) return RECYCLE_PAPER;
    if (strcmp(s, "VINYL") == 0) return RECYCLE_VINYL;
    return RECYCLE_NONE;
}

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