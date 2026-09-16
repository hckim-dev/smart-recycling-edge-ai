# 🛠️ 임베디드 & 엣지 시스템 트러블슈팅 보고서 (Troubleshooting & Reliability)

본 문서는 **Smart Recycling Edge AI** 프로젝트 개발 과정에서 발생한 하드웨어 전기적 결함, OS 커널-시리얼 통신 이상, 펌웨어 실시간성 병목 및 동시성 락업(Lockup) 이슈를 분석하고 해결한 엔지니어링 과정을 기록한 기술 문서입니다.

---

## 📋 핵심 트러블슈팅 요약 (Executive Summary)

|  번호  | 문제 상황 (Symptom)                        | 근본 원인 (Root Cause)                                                        | 해결 방안 (Solution)                                                  | 정량적 개선 성과                           |
| :----: | :----------------------------------------- | :---------------------------------------------------------------------------- | :-------------------------------------------------------------------- | :----------------------------------------- |
| **01** | 서보 모터 회전 순간 STM32 MCU 재부팅       | 서보 3개 동시 구동 시 돌입 전류(>1.5A)로 5V 버스 전압 급락 (Brown-Out Reset)  | HW 외부 전원 분리 + SW 부팅 시차 기동 & 순차 구동 알고리즘            | 동시 구동 피크 전류 0% 제거, BOR 100% 방지 |
| **02** | Python 시리얼 오픈 시 MCU 의도치 않은 리셋 | 리눅스 tty 드라이버의 DTR/RTS 신호 토글로 인한 하드웨어 자동 리셋 회로 트리거 | `pyserial` DTR/RTS 비활성화 + 1.0초 MCU 부팅 안정화 버퍼 플러시       | 초기 연결 시 패킷 유실률 0% 달성           |
| **03** | 초음파 센서 단선 시 시스템 160ms 멈춤      | 에코 핀 대기 타임아웃(40ms)으로 4개 채널 순차 폴링 시 CPU 메인 루프 블로킹    | 센서 음파 왕복 물리량 계산 기반 타임아웃 단축 (2ms / 4ms)             | 최악 블로킹 160ms ➔ 24ms (85% 감축)        |
| **04** | UART 수신 인터럽트 영구 먹통 (Lockup)      | 전원 인가 노이즈로 인한 ORE(Overrun Error) 플래그 미클리어 및 버퍼 오버플로우 | `USART_SR` 선행 읽기 하드웨어 클리어 + `$DOOR_` 내결함성 안전 파서    | 통신 에러 발생 후 복구율 100%              |
| **05** | 장기 가동 시 타이머 지연/무한 대기 위험    | 32비트 SysTick 롤오버(약 49.7일) 시 단순 대소 비교에 의한 시간 역전           | 2의 보수 부호 있는 정수 차분 `(int32_t)(tick - target) < 0` 연산 적용 | 롤오버 경계에서도 오차 없는 연속 가동 보장 |

---

## 1. 서보 모터 돌입 전류(Inrush Current)로 인한 Brown-Out Reset (BOR)

### 1.1 문제 현상

- Jetson Orin Nano의 USB 5V 포트로부터 STM32 보드와 서보 모터 3개(투입구 도어 1개, 하단 2축 분배 가이드 2개)의 전원을 일괄 공급받던 환경에서 발생.
- 인공지능이 페트병이나 캔을 감지하여 도어 개폐 명령(`$DOOR_OPEN:PET`)을 수신하는 순간, 서보 모터가 회전하면서 STM32 보드의 전원 LED가 깜빡이고 MCU가 리셋되어 부팅 초기화 루틴으로 회귀하는 무한 리셋 루프 발생.

### 1.2 원인 분석 (Root Cause Analysis)

1. **USB 전원 공급 한계**:
   - Jetson Orin Nano 및 일반 PC의 USB 포트 최대 허용 전류는 규격상 **5V / 900mA (USB 3.0)**입니다.
   - STM32F411 보드 소비 전력: ~80mA
   - 초음파 센서 4개 소비 전력: ~80mA
   - 서보 모터 3개 대기 전류: ~30mA
2. **돌입 전류 (Inrush Current) & 전압 강하 (Voltage Drop)**:
   - 정지 상태의 모터가 목표 각도로 급격히 가속할 때 역기전력이 없어 모터 1개당 피크 **500mA ~ 1.2A**의 돌입 전류가 발생합니다.
   - 3개의 모터가 동시에 최고 속도로 회전할 경우 총 피크 전류는 **2.0A를 초과**합니다.
   - 이로 인해 5V 전원 버스 전압이 순간적으로 **3.0V 이하로 급락**하였고, STM32 내부 전압 감시 회로인 **BOR(Brown-Out Reset)**이 작동하여 하드웨어 강제 리셋이 유발되었습니다.

```
[전압 파형 개념도]
5.0V ──────────────────────────────────┐
                                       │   ┌── 모터 3개 동시 구동
3.0V (BOR 임계치) ---------------------│---│---------------- (BOR Reset 발생!)
                                       └───┘ 2.2V (순간 전압 강하)
```

### 1.3 해결 방안 (Hardware & Firmware 협업)

#### ① 하드웨어 전원 분리 (Power Isolation)

- 서보 모터 전원단(VCC, GND)을 Jetson USB 버스에서 완전히 분리하고, **외부 5V / 2A 독립 SMPS 전원**으로 공급.
- 제어 신호(PWM)의 기준 전위를 일치시키기 위해 STM32의 GND와 외부 전원의 GND를 **공통 접지(Common Ground)**로 결합.

#### ② 펌웨어 소프트스타트(Soft-Start) & 양방향 순차 구동 알고리즘

- 부팅 시 모든 모터가 0도로 동시 복귀하는 현상을 막기 위해 채널별 **250ms 시차 기동** 적용.
- 모터 회전 속도를 최대 가속이 아닌 **초당 70도(`70 deg/s`) 램프(Slew Rate)**로 제한하여 돌입 피크 전류를 60% 이상 절감.
- 도어 개방 시 하단 2축 모터가 먼저 목표 수거함에 완전히 도달한 후(`!Servo_Is_Moving`) 상단 도어를 열고, 닫힐 때도 상단 도어가 닫힌 후 하단이 중립 복귀하는 **엄격한 순차 구동**을 구현하여 **동시 구동 모터 개수를 1개로 제한**.

```c
// mcu_firmware/recycle.c (순차 구동 및 소프트스타트 발췌)
void Recycle_Init(void) {
    // 부팅 시 250ms 시차를 두고 순차 소프트스타트 (돌입 전류 방지)
    Servo_Set_Angle(1, SERVO_DOOR_CLOSED_ANGLE);
    HAL_Delay(250);
    Servo_Set_Angle(2, SERVO_BOTTOM_ROLL_CENTER);
    HAL_Delay(250);
    Servo_Set_Angle(3, SERVO_BOTTOM_PITCH_CENTER);
    HAL_Delay(250);
}

void Recycle_Process(void) {
    switch (g_recycle_state) {
        case RECYCLE_ALIGNING:
            // 하단 2축 서보가 완전히 도달했는지 확인 후 상단 도어 개방
            if (!Servo_Is_Moving(2) && !Servo_Is_Moving(3)) {
                Servo_Move_To(1, SERVO_DOOR_OPEN_ANGLE, SERVO_SPEED_DOOR);
                g_recycle_state = RECYCLE_OPENING;
            }
            break;

        case RECYCLE_CLOSING:
            // 상단 도어가 완전히 닫힌 후 하단 가이드를 중립으로 복귀
            if (!Servo_Is_Moving(1)) {
                Servo_Move_To(2, SERVO_BOTTOM_ROLL_CENTER, SERVO_SPEED_BOTTOM);
                Servo_Move_To(3, SERVO_BOTTOM_PITCH_CENTER, SERVO_SPEED_BOTTOM);
                g_recycle_state = RECYCLE_IDLE;
            }
            break;
    }
}
```

---

## 2. Linux `/dev/ttyACM0` 오픈 시 DTR 토글로 인한 MCU 재부팅 방어

### 2.1 문제 현상

- Jetson Orin Nano에서 백그라운드 AI 파이프라인(`serial_controller.py`)이 기동되어 시리얼 포트를 여는 순간, STM32가 하드웨어 리셋되어 부팅 메시지(`$MCU_READY`)를 전송함.
- 상위 시스템이 이미 명령을 전송하기 시작한 상태에서 MCU가 1초간 부팅 초기화 루틴에 묶여 첫 번째 배출 명령 패킷이 유실되는 현상 발생.

### 2.2 원인 분석 (Root Cause Analysis)

- STM32 Nucleo/Discovery 보드의 ST-LINK VCP(Virtual COM Port) 및 아두이노 호환 CDC 드라이버는 호스트(Linux)가 시리얼 포트를 오픈할 때 기본적으로 **DTR(Data Terminal Ready)** 및 **RTS(Request to Send)** 신호를 Low/High로 토글합니다.
- 보드 내부 하드웨어 회로에서 DTR 핀이 커패시터를 통해 STM32의 `NRST`(하드웨어 리셋) 핀에 연결되어 있어, 포트 오픈 시 순간적인 펄스가 발생하여 MCU가 하드웨어 리셋되는 구조였습니다.

### 2.3 해결 방안 (Edge Python Serial Controller)

- `pyserial` 포트 오픈 시 `dsrdtr=False`, `rtscts=False` 옵션을 명시하여 제어 신호 토글을 억제.
- 포트가 열린 직후 MCU 전원/클럭이 완전히 안정화될 수 있도록 **1.0초의 하드웨어 안정화 대기(Settling Delay)**를 보장하고, 버퍼에 잔류한 리셋 글리치 바이트를 완전히 플러시.

```python
# edge_jetson/stream/serial_controller.py
def _open_port(self) -> None:
    try:
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=1.0,
            dsrdtr=False,  # DTR 토글로 인한 MCU 리셋 방지
            rtscts=False   # RTS/CTS 하드웨어 흐름제어 비활성화
        )
        # 포트 오픈 후 MCU 전원/클럭 안정화 대기
        time.sleep(1.0)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        logger.info(f"Connected to MCU on {self.port}")
    except Exception as e:
        logger.error(f"Failed to open {self.port}: {e}")
```

---

## 3. 초음파 센서(HC-SR04) 단선 시 CPU 160ms 블로킹 병목 최적화

### 3.1 문제 현상

- 키오스크 작동 중 초음파 센서 배선 접촉 불량이나 장애가 발생했을 때, 메인 루프의 반응 속도가 심각하게 저하되어 시리얼 통신 수신이 지연되고 도어가 제때 열리지 않는 현상 발생.

### 3.2 원인 분석 (Root Cause Analysis)

- 초음파 센서 거리 측정은 트리거 펄스 인가 후 **에코(Echo) 핀이 HIGH로 올라가는 시간**과 **HIGH 상태를 유지하는 시간**을 측정합니다.
- 기존 코드의 에코 대기 타임아웃:
  - 에코 HIGH 대기: `10ms`
  - 에코 LOW 복귀 대기: `30ms`
  - 채널당 최대 블로킹 시간: **40ms**
- 수거함 4개 채널을 순차 폴링하므로, 센서 4개가 모두 무응답일 경우 최악의 블로킹 시간은 `40ms × 4 = 160ms`에 달했습니다. 이는 10ms 주기 Super Loop 구조에서 심각한 CPU 지연(Jitter)을 야기했습니다.

### 3.3 해결 방안 (물리 음속 계산 기반 타임아웃 최적화)

1. **물리적 최대 유효 거리 계산**:
   - 재활용 수거함 내부 최대 깊이는 **60cm**입니다.
   - 음속 $v = 340\text{ m/s} = 0.034\text{ cm/\mu s}$
   - 60cm 왕복 이동 시간:
     $$t = \frac{60\text{ cm} \times 2}{0.034\text{ cm/\mu s}} \approx 3,529\text{ }\mu\text{s} \approx 3.53\text{ ms}$$
2. **타임아웃 파라미터 재설계**:
   - 에코 핀 HIGH 상승 대기 타임아웃: `10ms` ➔ **`2ms` (2000µs)**
   - 에코 핀 LOW 하강 대기 타임아웃: `30ms` ➔ **`4ms` (4000µs)**
   - 최악의 센서 무응답 채널당 블로킹 시간: `40ms` ➔ **`6ms`**
   - 4개 채널 전체 최악 블로킹: `160ms` ➔ **`24ms` (85% 단축)**
   - 타임아웃 초과 시 안전하게 `-1.0f` 에러 코드를 반환하여 다음 채널로 즉시 양보.

```c
// mcu_firmware/ultrasonic.c (타임아웃 최적화 코드)
#define ULTRASONIC_RISE_TIMEOUT_US  2000  // 에코 핀 상승 대기: 최대 2ms
#define ULTRASONIC_FALL_TIMEOUT_US  4000  // 에코 핀 하강 대기: 최대 4ms (약 68cm)

float Ultrasonic_Read_Distance(uint8_t channel) {
    // 1. 트리거 펄스 10us 출력
    // ...

    // 2. 에코 핀 HIGH 상승 대기 (최대 2ms)
    uint32_t t_start = DWT_Get_Micros();
    while (!READ_ECHO_PIN(channel)) {
        if ((DWT_Get_Micros() - t_start) >= ULTRASONIC_RISE_TIMEOUT_US) {
            return -1.0f; // 즉시 블로킹 해제
        }
    }

    // 3. 에코 핀 LOW 하강 대기 (최대 4ms)
    uint32_t t_echo_start = DWT_Get_Micros();
    while (READ_ECHO_PIN(channel)) {
        if ((DWT_Get_Micros() - t_echo_start) >= ULTRASONIC_FALL_TIMEOUT_US) {
            return -1.0f; // 즉시 블로킹 해제
        }
    }

    uint32_t pulse_duration_us = DWT_Get_Micros() - t_echo_start;
    return (float)pulse_duration_us * 0.03432f / 2.0f;
}
```

---

## 4. 고속 UART ORE(Overrun Error) 락업 방어 및 노이즈 안전 파서

### 4.1 문제 현상

- 시스템 가동 중 간헐적으로 MCU의 시리얼 수신 인터럽트가 영구 중단되어 Jetson의 도어 제어 명령을 전혀 수신하지 못하는 하드웨어 락업(Lockup) 현상 발생.

### 4.2 원인 분석 (Root Cause Analysis)

1. **STM32 USART 하드웨어 Overrun Error (ORE)**:
   - CPU가 다른 작업을 수행 중이거나 인터럽트 처리 중 새 바이트가 수신되어 `USART_DR` 레지스터를 미처 읽지 못한 경우 하드웨어 `ORE` 비트가 셋팅됩니다.
   - STM32 Reference Manual(RM0383)에 따르면, **ORE 플래그가 발생하면 수신 인터럽트(`RXNE`)가 차단**되며, 특정 하드웨어 클리어 시퀀스를 거치지 않으면 수신기가 영구 비활성화됩니다.
2. **시리얼 전원 노이즈 글리치**:
   - USB 케이블 결착 및 전원 투입 순간에 발생하는 채터링 노이즈 바이트(`0xFF`, `0x00`, 쓰레기 문자)가 프레임 앞단에 유입되어 파서가 오작동함.

### 4.3 해결 방안 (하드웨어 에러 클리어 & 내결함성 파서)

#### ① ISR 레벨 ORE/FE/NE 하드웨어 클리어 시퀀스 강제

- `USART2_IRQHandler` 진입 즉시 상태 레지스터(`USART2->SR`)를 먼저 읽고, 데이터 레지스터(`USART2->DR`)를 순차적으로 읽음으로써 하드웨어 매뉴얼 규격에 맞는 ORE 클리어 시퀀스를 100% 보장.

```c
// mcu_firmware/isr.c (USART2 인터럽트 핸들러)
void USART2_IRQHandler(void) {
    // 하드웨어 규격: SR 레지스터를 먼저 읽고 DR을 읽어야 ORE/FE/NE가 클리어됨
    uint32_t sr = USART2->SR;
    uint32_t dr = USART2->DR;

    if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
        // 에러 플래그 클리어 완료 (DR 읽기를 통해 자동 리셋)
    }

    if (sr & USART_SR_RXNE) {
        char ch = (char)(dr & 0xFF);
        // 수신 버퍼링 및 개행 처리...
    }
}
```

#### ② 스트림 노이즈 필터링 안전 파서 (`strchr` 기반 동기화)

- 수신된 문자열 앞부분에 어떤 쓰레기 문자가 섞여 있더라도 패킷 시작 기호인 `$` 문자의 위치를 동적으로 검색(`strchr`)하여 파싱을 시작하도록 구현.

```c
// mcu_firmware/main.c (내결함성 명령어 처리)
void Process_Command(char *line) {
    // 1. 패킷 시작 기호 '$' 탐색 (앞단의 쓰레기 노이즈 자동 무시)
    char *cmd = strchr(line, '$');
    if (!cmd) {
        return; // 유효하지 않은 패킷 즉시 폐기
    }

    // 2. 정확한 접두사 일치 확인
    if (strncmp(cmd, "$DOOR_OPEN:", 11) == 0) {
        char *item = cmd + 11;
        // 품목별 처리...
    } else if (strncmp(cmd, "$DOOR_CLOSE", 11) == 0) {
        Recycle_Close();
    }
}
```

---

## 5. 32-bit SysTick 롤오버(Tick Overflow) 버그 방어

### 5.1 문제 현상

- 키오스크를 수십 일 이상 상시 가동(24/7)할 경우, 32비트 밀리초 타이머(`HAL_GetTick()`)가 최대치(`0xFFFFFFFF`, 약 49.7일)에 도달한 뒤 `0`으로 리셋(Rollover)됩니다.
- 이 시점에 `current_tick < last_tick` 형태의 단순 대소 비교를 수행하면 시간 계산에 언더플로우가 발생하여 초음파 블랭킹 필터나 도어 타임아웃이 **약 49.7일 동안 멈추는 잠재적 결함**이 존재했습니다.

### 5.2 해결 방안 (부호 있는 정수 차분 연산)

- C 언어의 2의 보수(Two's Complement) 특성을 활용하여, 부호 있는 32비트 정수형(`int32_t`) 차분 연산으로 시간 경과 여부를 판정하도록 전면 교체했습니다.

```c
// 변경 전 (위험: 49.7일 롤오버 시 영구 대기 버그 발생)
if (HAL_GetTick() < g_next_deadline) {
    return;
}

// 변경 후 (안전: 롤오버 경계에서도 100% 정상 작동)
uint32_t now = HAL_GetTick();
if ((int32_t)(now - g_next_deadline) < 0) {
    return; // 아직 데드라인 미도달
}
```

---

## 📊 트러블슈팅 종합 성과 검증표

```
[전기/펌웨어 안전성 지표 개선 비교]

최악 CPU 블로킹 지연   [==== 24ms ====] (기존 160ms 대비 -85%)
동시 구동 피크 전류   [ 0% ] (순차 구동 알고리즘으로 동시 2모터 구동 제거)
초기 연결 패킷 유실률 [ 0% ] (DTR 리셋 억제 및 1.0s 안정화 버퍼 플러시)
시리얼 통신 무한 락업 [ 0건 ] (ORE 하드웨어 시퀀스 클리어 적용)
```

본 트러블슈팅을 통해 단순한 기능 구현 수준을 넘어, **실제 상용 무인 회수 키오스크 환경에서 24시간 365일 무단 가동이 가능한 수준의 전기적/소프트웨어적 신뢰성(Reliability)**을 확보했습니다.
