# ⚙️ Smart Recycling Central Backend (FastAPI)

<div align="center">

[![Framework](https://img.shields.io/badge/Framework-FastAPI%200.110%2B-009688?logo=fastapi&logoColor=white)](https://fastapi.tiangolo.com)
[![Language](https://img.shields.io/badge/Python-3.10%2B-3776AB?logo=python&logoColor=white)](https://www.python.org)
[![Database](<https://img.shields.io/badge/Database-aiosqlite%20(WAL)-003B57?logo=sqlite&logoColor=white>)](https://sqlite.org)
[![Protocol](https://img.shields.io/badge/Protocol-REST%20%26%20WebSocket-E4405F)]()
[![Validation](https://img.shields.io/badge/Schema-Pydantic%20V2-E92063?logo=pydantic&logoColor=white)](https://docs.pydantic.dev)
[![Architecture](https://img.shields.io/badge/Architecture-Clean%203--Tier-blue)]()

**스마트 키오스크(Qt C++)와 모바일 앱(Android) 간의 실시간 1:1 세션 브로커링 및 포인트 정산 중앙 서버**  
비동기 논블로킹 I/O, SQLite WAL 동시성 제어, 원자적 SQL 트랜잭션을 통해 데이터 무결성을 보장합니다.

</div>

---

## 📌 핵심 아키텍처 및 엔지니어링 강점 (Engineering Highlights)

### 1. Clean 3-Tier Layered Architecture

관심사 분리(Separation of Concerns) 원칙에 따라 계층 간 의존성을 엄격히 격리했습니다.

- **Presentation (Routers)**: Thin Controller 구조, Pydantic V2 DTO 유효성 검증 및 HTTP/WS 라우팅
- **Domain Service (Services)**: 비즈니스 규칙 오케스트레이션 및 WebSocket 브로드캐스트 분리
- **Data Access (Repositories)**: `aiosqlite` 쿼리 캡슐화 및 순수 SQL 기반 고성능 제어

```
[Qt Kiosk] ──(WS / REST)──┐
                          ▼
             [Presentation Layer]  routers/ (auth, kiosk, users, websocket)
                          │
                          ▼
             [Domain Service]       services/ (kiosk_service, user_service, auth_service)
                          │         connection_manager.py (asyncio.Lock 룸 관리)
                          ▼
             [Data Access Layer]   repositories/ (user_repo, kiosk_repo, log_repo)
                          │
                          ▼
             [SQLite WAL Engine]   data/smart_recycle.db (aiosqlite 비동기 I/O)
                          ▲
[Android App] ─(REST / WS)┘
```

### 2. 원자적 동시성 제어 (Race Condition & Double Spending 방어)

단순 Read-Modify-Write 방식의 애플리케이션 레벨 연산은 동시 요청 시 Lost Update를 유발합니다.  
본 시스템은 SQLite의 **단일 원자적 SQL 문 및 `RETURNING` 절**을 채택하여 동시성 이슈를 원천 차단했습니다.

- **포인트 적립 (Lost Update 방어)**:
  ```sql
  UPDATE users SET points = points + ? WHERE id = ? RETURNING points;
  ```
- **포인트 차감 (Double Spending / 잔액 부족 방어)**:
  ```sql
  UPDATE users SET points = points - ? WHERE id = ? AND points >= ? RETURNING points;
  ```
- **회원 간이 가입/로그인 (원자적 UPSERT)**:
  ```sql
  INSERT INTO users (phone, name) VALUES (?, ?)
  ON CONFLICT(phone) DO UPDATE SET updated_at = CURRENT_TIMESTAMP
  RETURNING id, phone, name, points, created_at;
  ```

### 3. 비동기 이벤트 루프 & SQLite WAL 최적화

- **논블로킹 DB I/O**: `aiosqlite` 비동기 드라이버를 도입하여 디스크 I/O 시 FastAPI 워커 스레드의 이벤트 루프 멈춤(Stop-the-world) 차단.
- **동시 읽기/쓰기 가속**: `PRAGMA journal_mode = WAL;` 및 `PRAGMA busy_timeout = 5000;` 적용으로 동시 읽기/쓰기 락 경합(`database is locked`) 방어.
- **자동 DDL 및 인덱싱**: 서버 부팅 시 Lifespan 이벤트로 `phone`, `user_id`, `created_at` 복합 인덱스 자동 구축.

### 4. WebSocket 세션 보호 및 좀비 연결 퇴출 (`ConnectionManager`)

- **`asyncio.Lock` 룸 격리**: 키오스크 1대당 모바일 1대만 매핑되는 1:1 세션 딕셔너리의 스레드 안전성 보장.
- **좀비 세션 퇴출**: 동일 기기에서 재접속 시 기존 세션을 `1000 NORMAL_CLOSURE`로 강제 종료하여 파일 디스크립터 고갈 및 메모리 누수 방지.

---

## 🔌 API 및 WebSocket 명세 (Specifications)

### REST API Endpoints

| Method | Endpoint                    | Description         | 주요 기능                                           |
| ------ | --------------------------- | ------------------- | --------------------------------------------------- |
| `POST` | `/api/auth/login`           | 간편 로그인 및 가입 | 휴대폰 번호 기반 원자적 UPSERT 등록                 |
| `POST` | `/api/kiosk/bind`           | 키오스크 QR 바인딩  | 모바일 QR 스캔 시 키오스크 화면 배출 모드 전환      |
| `POST` | `/api/kiosk/cancel`         | 세션 중도 취소      | 키오스크 및 모바일 화면 동시 복귀 동기화            |
| `POST` | `/api/recycle/submit`       | 배출 정산 및 적립   | 4대 품목 카운트 정산, 탄소 저감량 집계, 포인트 가산 |
| `GET`  | `/api/users/{user_id}/logs` | 배출 이력 조회      | 사용자별 누적 분리배출 기록 최신순 페이징           |
| `POST` | `/api/users/deduct`         | 리워드 포인트 차감  | 상점 쿠폰 교환 시 조건부 원자적 포인트 차감         |

### WebSocket Real-time Events (`ws://<HOST>:8000/ws/kiosk/{bin_id}/{client_type}`)

- `USER_AUTHENTICATED` (Server ➔ Kiosk): QR 바인딩 성공 시 키오스크 배출 화면 전환
- `SESSION_CANCELLED` (Server ➔ Kiosk & Mobile): 중도 취소 시 양측 디바이스 대기 화면 동기화
- `RECYCLE_COMPLETE` (Server ➔ Mobile): 투입 완료 시 모바일 앱에 정산 결과 푸시

---

## 📁 디렉터리 구조 (Directory Structure)

```
server/
├── app/
│   ├── connection_manager.py     # 1:1 WebSocket 룸 관리자 (asyncio.Lock)
│   ├── database.py               # aiosqlite 비동기 연결 및 DDL 인덱스 생성
│   ├── exceptions.py             # 도메인 커스텀 예외 및 표준 JSON 핸들러
│   ├── schemas.py                # Pydantic V2 요청/응답 스키마
│   ├── repositories/             # Data Access 계층 (원자적 SQL 쿼리)
│   ├── services/                 # Domain Service 계층 (비즈니스 로직)
│   └── routers/                  # Presentation 계층 (REST/WS 엔드포인트)
├── tests/
│   └── mock_mobile_client.py     # 모바일 앱 E2E 통합 테스트 시뮬레이터
├── main.py                       # FastAPI 애플리케이션 진입점 (Lifespan 관리)
└── requirements.txt              # 서버 런타임 의존성
```

---

## 🚀 빠른 시작 (Getting Started)

### 1. 가상환경 준비 및 서버 구동 (Python 3.10+)

```bash
# 1. 가상환경 생성
python -m venv .venv

# 2. 가상환경 활성화 (Windows: .venv\Scripts\activate / Linux·macOS: source .venv/bin/activate)
source .venv/bin/activate

# 3. 의존성 패키지 설치
pip install -r requirements.txt

# 4. 서버 구동 (기본 호스트: 0.0.0.0, 포트: 8000)
python main.py
```

> **자동 DB 초기화**: 서버 첫 구동 시 Lifespan 이벤트에 의해 `data/smart_recycle.db` 생성 및 테이블/복합 인덱스 DDL이 자동 적용됩니다.  
> **Swagger API 명세서**: 브라우저에서 `http://localhost:8000/docs` 접속 시 대화형 테스트 가능.

### 2. E2E 통합 시뮬레이터 검증 (모바일 기기 없이 단독 테스트)

```bash
# 모바일 로그인 -> QR 키오스크 바인딩 -> 실시간 WebSocket 정산 수신 전과정 자동 시뮬레이션
python tests/mock_mobile_client.py
```
