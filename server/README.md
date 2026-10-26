# 중앙 서버

[프로젝트 소개](../README.md) · [전체 구조](../docs/architecture.md) · [API·이벤트 명세](../docs/protocol-spec.md#http-rest)

FastAPI·SQLite로 사용자·배출 이력·포인트를 저장하고, Qt와 Android 사이의 연결·완료·취소 이벤트를 중계합니다.

## 계층과 데이터

```text
routers → services → repositories → SQLite
                ↘ ConnectionManager → WebSocket
```

| 위치 | 책임 |
| --- | --- |
| [routers](app/routers/) | HTTP·WebSocket 경로, 의존성 주입·요청 검증 |
| [services](app/services/) | 로그인·바인딩·정산·포인트 처리 순서 |
| [repositories](app/repositories/) | 사용자·기기·배출 이력 SQL |
| [connection_manager.py](app/connection_manager.py) | bin_id별 kiosk·mobile 소켓 관리 |
| [database.py](app/database.py) | 요청별 비동기 DB 연결, 스키마·인덱스 초기화 |
| [schemas.py](app/schemas.py) | 요청·응답 DTO |
| [exceptions.py](app/exceptions.py) | 공통 `{"detail": "..."}` 오류 응답 |

테이블은 `users`, `kiosks`, `recycle_logs`입니다. 서버 시작 시 기본 키오스크 ID 1을 생성합니다. aiosqlite 연결에 WAL·foreign_keys·busy_timeout 등을 설정하며 별도 커넥션 풀은 구현하지 않습니다.

## 포인트 처리 범위

포인트 증감은 SELECT 후 계산한 값을 덮어쓰는 대신 단일 UPDATE로 처리합니다.

```sql
UPDATE users SET points = points + ? WHERE id = ? RETURNING points;

UPDATE users SET points = points - ?
WHERE id = ? AND points >= ?
RETURNING points;
```

조건부 차감은 잔액 부족 시 갱신하지 않습니다. WAL과 대기 시간은 읽기·쓰기 경합을 완화하지만 SQLite의 쓰기 잠금 자체를 없애지는 않습니다.

정산은 **포인트 적립 → 이력 생성 → 키오스크 IDLE 저장 → 모바일 이벤트 전송** 순서입니다. 각 저장소가 별도로 commit하므로 전체 정산이 하나의 트랜잭션은 아닙니다. 동일 요청의 재전송도 별도 적립으로 처리합니다.

수량·획득 포인트·탄소량은 Qt가 계산해 제출하며 서버가 정책값으로 다시 산출하지 않습니다.

## API와 이벤트

| HTTP 경로 | 역할 |
| --- | --- |
| `GET /health` | 서버 상태 |
| `POST /api/auth/login` | 전화번호로 사용자 조회·생성·이름 갱신 |
| `GET /api/users/{user_id}` | 사용자·잔여 포인트 |
| `GET /api/users/{user_id}/logs` | 사용자 배출 이력 |
| `POST /api/kiosk/bind` | 기기 상태 변경·Qt 연결 이벤트 |
| `POST /api/kiosk/cancel` | IDLE 저장·양측 취소 이벤트 |
| `POST /api/recycle/submit` | 배출 이력·적립 |
| `POST /api/users/deduct` | 잔액 조건부 포인트 차감 |

WebSocket 경로는 `/ws/kiosk/{bin_id}/{client_type}`이며 `client_type`은 `kiosk` 또는 `mobile`입니다. `USER_AUTHENTICATED`·`RECYCLE_COMPLETE`·`SESSION_CANCELLED`를 전송합니다. 실제 JSON은 [통신 명세](../docs/protocol-spec.md#websocket-서버--qtandroid)를 참고합니다.

룸은 단일 서버 프로세스의 메모리에 있고, 역할별 새 소켓은 기존 소켓을 교체합니다. 연결 해제 시 소켓 인스턴스를 비교해 새 연결을 잘못 지우지 않도록 처리합니다.

## 실행

Python 3.10 이상 문법을 사용합니다. 설치 패키지 조건은 [requirements.txt](requirements.txt)를 기준으로 합니다.

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe main.py
```

Linux에서는 `.venv/bin/python`을 사용합니다. 기본 `0.0.0.0:8000`에서 실행하며 `main.py`는 개발용 reload를 활성화합니다.

- 상태: `http://localhost:8000/health`
- 대화형 API: `http://localhost:8000/docs`
- DB: `server/data/smart_recycle.db`
- 설정: [app/config.py](app/config.py)

## 연동 확인과 확장 과제

```powershell
.\.venv\Scripts\python.exe tests/mock_mobile_client.py
```

Mock 클라이언트는 로그인 후 Enter 입력으로 바인딩하고, Qt의 완료 조작에 따른 이벤트를 기다립니다. 모바일을 대체하는 수동 연동 클라이언트이며 자동 회귀 테스트는 아닙니다.

전화번호 입력은 시연용 사용자 식별입니다. OTP·토큰 인증·기기 권한·세션 소유권 검증은 구현하지 않았습니다. 운영 확장 시 인증·중복 정산 방지·통합 트랜잭션·이벤트 복구를 함께 다뤄야 합니다. 이력 저장소에는 limit/offset 인자가 있지만 현재 HTTP 이력 경로는 이를 노출하지 않고 전체 목록을 반환합니다.
