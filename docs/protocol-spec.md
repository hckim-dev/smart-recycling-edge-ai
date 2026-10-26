# 통신 프로토콜

[문서 목록](README.md) · [시스템 구조](architecture.md)

현재 송신·수신 코드의 메시지를 기준으로 합니다. 예시의 사용자·ID·잔액은 설명용 값이며, 별도 명시가 없으면 필드는 최상위에 위치합니다.

| 경로 | 전송 방식 | 역할 |
| --- | --- | --- |
| Jetson → Qt | TCP 9000, 길이 헤더 + JPEG + JSON | 영상·검출·장치 상태 |
| Qt → Jetson | 같은 TCP, 개행 JSON | 수신기에서 지원하는 수동 제어. 현재 Qt 송신 구현 없음 |
| Jetson ↔ STM32 | UART 115200bps 8N1 | 개폐 명령·제어 상태·적재율 |
| Qt·Android ↔ 서버 | HTTP 8000 | 사용자·연결·정산·이력·차감 |
| 서버 ↔ Qt·Android | WebSocket 8000 | 연결·완료·취소 이벤트 |

## TCP: Jetson → Qt

기준: [송신](../edge_jetson/stream/socket_server.py), [수신](../pc_dashboard/network/jetson_client.cpp), [Qt 데이터 모델](../pc_dashboard/configs/app_config.h)

서버는 Qt 클라이언트 하나를 처리합니다. 스트림의 한 메시지는 다음 순서입니다.

```text
uint32 BE: JPEG 바이트 수 (4B)
uint32 BE: JSON 바이트 수 (4B)
JPEG bytes
UTF-8 JSON bytes
```

헤더에는 매직 바이트·버전·체크섬이 없습니다. TCP의 한 read/write와 메시지 한 개는 일치하지 않습니다. Qt는 누적 버퍼에서 완전한 길이가 확보된 메시지를 순서대로 소비합니다.

| 항목 | 현재 값·의미 |
| --- | --- |
| JPEG | OpenCV imencode, quality 75 |
| 이미지 길이 상한 | Qt 수신기 10MiB |
| JSON 길이 상한 | Qt 수신기 64KiB |
| 누적 버퍼 상한 | Qt 수신기 20MiB |
| 초과·비정상 길이 | 현재 버퍼 비움. 매직 헤더를 찾는 복구는 없음 |
| 소켓 옵션 | 송신 측 TCP_NODELAY, 송신 timeout 1초 |

### 메타데이터

```json
{
  "timestamp": 1790000000.125,
  "fps": 30.0,
  "infer_ms": 18.5,
  "detections": [
    {
      "class_id": 2,
      "class_name": "pet",
      "category": "PET",
      "confidence": 0.94,
      "box": [120, 80, 240, 320],
      "inspection": {
        "passed": false,
        "reasons": ["LABEL_ATTACHED"],
        "details": {
          "pet_label": {"passed": false, "score": 0.88, "status": "FAIL"},
          "pet_content": {"passed": true, "score": 0.12, "status": "PASS"}
        }
      }
    }
  ],
  "bin_levels": {"paper": 20, "can": 35, "pet": 60, "vinyl": 10},
  "door": {"item": "ALL", "state": "CLOSED"}
}
```

| 필드 | 의미 |
| --- | --- |
| `timestamp` | Jetson의 Unix 초 단위 시각 |
| `fps` | 메인 루프 처리 간격의 역수. 고유 카메라 프레임 수가 아님 |
| `infer_ms` | 검출·PET 검사 구간. 전송·화면 표시 시간은 포함하지 않음 |
| `detections` | 검출 목록. 최종 클래스 순서는 PAPER/CAN/PET/VINYL |
| `box` | 원본 영상 좌표 `[x1, y1, x2, y2]` |
| `inspection` | 검사 실행 대상에 포함될 때 추가되는 결과 |
| `bin_levels` | MCU에서 캐시한 적재율. UART 순서와 같은 품목 매핑 |
| `door` | MCU 제어 상태와 Jetson이 연결한 최근 명령 품목 |

검사 상태는 PASS/FAIL/BYPASS, 사유는 LABEL_ATTACHED/CONTAMINATED/CROP_TOO_SMALL 등을 사용합니다. 준비되지 않은 검사기는 `score: null`과 BYPASS를 반환합니다. 현재 메인 루프는 작은 Crop의 검사 호출을 생략할 수도 있습니다.

### 역방향 수동 제어

Jetson 수신기는 다음 개행 JSON을 처리합니다.

```json
{"action": "OPEN", "item": "PET"}
```

```json
{"action": "CLOSE"}
```

각 JSON 뒤에 LF를 붙입니다. MCU에서 유효한 OPEN 품목은 PAPER/CAN/PET/VINYL입니다. 수동 OPEN은 자동 검사·유지 조건을 거치지 않으며 명령 ACK는 없습니다.

## UART: Jetson ↔ STM32

기준: [Jetson 파서](../edge_jetson/stream/protocol.py), [MCU 명령 처리](../mcu_firmware/main.c), [MCU RX](../mcu_firmware/isr.c)

MCU USART2는 PA2 TX·PA3 RX입니다. 대상 연결의 시리얼 포트 경로는 Jetson 설정에서 지정합니다. Jetson은 LF로 명령을 마치고, MCU 표준 출력은 CRLF를 사용합니다.

| 방향 | 메시지 | 처리 |
| --- | --- | --- |
| Jetson → MCU | `$DOOR_OPEN:PAPER\n` | 품목별 하단 목표 설정 후 상단 구동 예약 |
| Jetson → MCU | `$DOOR_CLOSE\n` | 개방 명령 처리 후 2초가 지난 뒤 닫힘 요청 반영 |
| 수동 제어 → MCU | `$BIN_RESET\n` 또는 `reset\n` | 필터 상태 초기화 |
| 수동 제어 → MCU | `1 90 60\n` | 1~3번 서보, 0~180° 논리 각도, 선택적 °/s |
| MCU → Jetson | `$DOOR_STATE:OPEN\r\n` | 제어 상태 변경 시 보고. CLOSED도 동일 |
| MCU → Jetson | `$BIN:20/35/60/10\r\n` | 약 2초 간격, PAPER/CAN/PET/VINYL의 적재율 |

OPEN은 PAPER/CAN/PET/VINYL을 지원합니다. `ALL`은 Jetson 내부 기본 품목명이며 MCU의 유효한 분류 명령이 아닙니다.

`$DOOR_STATE`는 상태 변경 때 전송하며 별도 1초 heartbeat는 없습니다. OPEN은 명령 처리 시의 논리 상태이므로 실제 기구 개방 완료 보고가 아닙니다. Jetson은 최근 OPEN 명령의 품목을 붙이고 CLOSED이면 ALL로 표시합니다.

MCU RX는 NUL 포함 32바이트 단일 라인 버퍼입니다. 이전 줄의 처리 대기 중인 바이트는 버리며 재전송·ACK·sequence 번호는 없습니다. 일반 진단 출력도 섞일 수 있으므로 Jetson은 지원하는 `$BIN`·`$DOOR_STATE`만 상태로 해석합니다.

## HTTP REST

기준: [라우터](../server/app/routers/), [요청·응답 모델](../server/app/schemas.py), [서비스](../server/app/services/)

| Method | 경로 | 요청·응답 |
| --- | --- | --- |
| GET | `/health` | 상태·서비스명 |
| POST | `/api/auth/login` | phone·선택적 name → 사용자 프로필 |
| GET | `/api/users/{user_id}` | 사용자 프로필·points |
| GET | `/api/users/{user_id}/logs` | user_id·total_count·logs |
| POST | `/api/kiosk/bind` | bin_id·user_id → 연결 결과 |
| POST | `/api/kiosk/cancel` | bin_id·선택적 user_id·reason → 취소 결과 |
| POST | `/api/recycle/submit` | 품목 수량·보상 → log_id·적립 결과 |
| POST | `/api/users/deduct` | user_id·amount·선택적 description → 잔여 포인트 |

이력 HTTP 경로는 limit/offset 쿼리를 받지 않고 전체 이력을 최신순으로 반환합니다. phone 로그인은 본인 인증 토큰을 발급하지 않습니다.

### 로그인과 바인딩

```json
{"phone": "01000000000", "name": "시연 사용자"}
```

로그인은 `id, phone, name, points, created_at`을 반환합니다. 신규 사용자는 points 0이며 기존 전화번호는 이름 갱신 규칙을 적용합니다.

```json
{"bin_id": 1, "user_id": 42}
```

바인딩 응답:

```json
{
  "status": "SUCCESS",
  "message": "키오스크 세션이 성공적으로 활성화되었습니다.",
  "bin_id": 1,
  "user_id": 42
}
```

### 정산 제출

수량과 보상은 Qt가 계산합니다. `items`로 중첩하지 않습니다. 예시는 캔 1개·PET 2개, 초기 잔액 0을 가정합니다.

```json
{
  "bin_id": 1,
  "user_id": 42,
  "can_count": 1,
  "pet_count": 2,
  "paper_count": 0,
  "vinyl_count": 0,
  "carbon_saved_g": 55.4,
  "earned_points": 150
}
```

```json
{
  "status": "SUCCESS",
  "log_id": 1,
  "earned_points": 150,
  "total_points": 150
}
```

비회원은 `user_id: null`을 제출하고 응답의 `total_points`도 null입니다. 수량·earned_points·carbon_saved_g는 생략 시 0, 음수는 검증 오류입니다. 서버는 품목 수량으로 보상을 다시 계산하지 않습니다.

### 취소와 차감

```json
{"bin_id": 1, "user_id": 42, "reason": "USER_CANCELLED"}
```

취소 응답은 `status, message, bin_id`입니다. user_id는 null 또는 생략할 수 있습니다.

```json
{"user_id": 42, "amount": 50, "description": "시연 상품 교환"}
```

차감은 양수 amount가 필요하며 잔액 조건을 만족할 때만 갱신합니다.

```json
{
  "status": "SUCCESS",
  "user_id": 42,
  "deducted_amount": 50,
  "remaining_points": 100,
  "description": "시연 상품 교환"
}
```

## WebSocket: 서버 ↔ Qt/Android

경로: `/ws/kiosk/{bin_id}/{client_type}`. 역할은 `kiosk` 또는 `mobile`입니다. 예시는 모두 `event` 필드를 사용하는 최상위 JSON이며 `type`·`data` 래퍼·session_id는 없습니다.

### USER_AUTHENTICATED: 서버 → Qt

```json
{
  "event": "USER_AUTHENTICATED",
  "user_id": 42,
  "name": "시연 사용자",
  "phone": "01000000000",
  "points": 0
}
```

이벤트명은 기존 코드의 식별자입니다. 사용자·기기 존재 확인 후 바인딩을 알리는 것이며 전화번호 본인 인증을 의미하지 않습니다.

### RECYCLE_COMPLETE: 서버 → Android

```json
{
  "event": "RECYCLE_COMPLETE",
  "user_id": 42,
  "earned_points": 150,
  "total_points": 150,
  "carbon_saved_g": 55.4,
  "can_count": 1,
  "pet_count": 2,
  "paper_count": 0,
  "vinyl_count": 0
}
```

탄소 단위는 g입니다. 정산 완료 이벤트는 모바일에 전송하며 Qt에는 별도 완료 WS 이벤트를 보내지 않습니다. Qt는 REST 응답을 받습니다.

### SESSION_CANCELLED: 서버 → 양측

```json
{
  "event": "SESSION_CANCELLED",
  "bin_id": 1,
  "user_id": 42,
  "reason": "USER_CANCELLED"
}
```

서버는 클라이언트가 보낸 텍스트를 수신·로깅하지만 이를 별도 제어 메시지로 중계하지 않습니다. 연결 교체·종료 처리는 [ConnectionManager](../server/app/connection_manager.py)를 기준으로 합니다.

## 오류 처리와 보장 범위

| 상황 | 현재 동작 |
| --- | --- |
| 사용자·키오스크 없음 | HTTP 404 |
| 잔액 부족·공백만 있는 전화번호 | HTTP 400 |
| 필수 필드 누락·빈 전화번호(`""`)·범위 오류 | HTTP 422 |
| 처리 중 내부 오류 | HTTP 500, 공통 `detail` |
| 잘못된 WS 역할 | close 1008 |
| 같은 룸·역할 재접속 | 기존 소켓 close 1000 후 교체 |
| TCP 송수신 실패 | Jetson이 연결 해제, Qt는 재접속 타이머 사용 |
| Jetson CLOSE 누락 | MCU가 개방 명령 처리 후 10초 경과를 Super Loop에서 확인해 중립 복귀 요청 |

정산 요청의 중복 방지·전체 저장의 원자성·이벤트 전달 보장은 구현하지 않았습니다. API와 소켓 접근에 별도 사용자·기기 인증도 없습니다. [현재 책임 경계와 확장 과제](architecture.md#검증-범위와-후속-과제)를 함께 확인합니다.
