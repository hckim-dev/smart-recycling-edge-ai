// servo.c 위에 얹은 재활용 분류 트리 제어: Jetson이 넘긴 재질에 맞춰 3모터(TOP/LEFT/RIGHT)로
// 경로를 잡고, 이후 자동 닫힘(DOOR_CLOSE 명령/최대개방 타임아웃)까지 담당한다.
#ifndef RECYCLE_H
#define RECYCLE_H

typedef enum
{
    RECYCLE_NONE = 0,  // 아직 분류 결과 없음 / 알 수 없는 문자열
    RECYCLE_PET,
    RECYCLE_CAN,
    RECYCLE_PAPER,
    RECYCLE_VINYL
} RecycleType;

typedef enum
{
    GATE_CLOSED = 0,
    GATE_OPEN
} GateState;

void Recycle_Init(void);

RecycleType Recycle_Type_From_String(const char *s);

void Recycle_Door_Open(RecycleType type);

// Jetson이 $DOOR_CLOSE를 보냈을 때 호출 - 최소 개방시간을 지키며 닫는다
void Recycle_Door_Close_Request(void);

GateState Recycle_Get_Gate_State(void);

int Recycle_Gate_State_Changed(void);

// main 루프에서 주기적으로 호출: 0=유지, 1=DOOR_CLOSE 명령으로 닫음, 2=최대개방 타임아웃으로 닫음
int Recycle_Auto_Close_Update(void);

// main 루프에서 Servo_Update() 직후 매번 호출.
// 아래쪽 세부분류 모터(PAPERCAN_CH/PETVINYL_CH) 2개가 목표각에 다 도착한 뒤에야
// TOP 모터를 움직이기 시작해서, 세부분류 위치가 잡히기 전에 투입구가 열려
// 쓰레기가 엉뚱한 통으로 떨어지는 걸 막는다.
void Recycle_Update(void);

RecycleType Recycle_Get_Open_Type(void);

// RecycleType(PET/CAN/PAPER/VINYL) -> BinType(bin_filter.h) 인덱스 매핑
// bin_filter.h를 recycle.h가 직접 include하지 않도록 int로 반환 (BinType과 값은 호환됨: 0=PAPER,1=CAN,2=PET,3=VINYL)
int Recycle_Type_To_Bin_Index(RecycleType type);

#endif
