# 📚 Smart Recycling Edge AI — Documentation Center

본 디렉터리는 **Smart Recycling Edge AI** 프로젝트의 시스템 아키텍처 설계, 이종 플랫폼 간 통신 규격, 하드웨어·소프트웨어 트러블슈팅 및 최종 프로젝트 보고서를 종합적으로 관리하는 문서 허브입니다.

---

## 📑 프로젝트 최종 보고서 (Final Report & Presentation)

프로젝트 기획 배경, 하드웨어 기구 설계, 비전 AI 모델 벤치마크 및 시연 평가 결과를 총괄 정리한 공식 리포트입니다.

- 📄 [**스마트 리사이클링 엣지 AI 최종 결과보고서 (PDF)**](presentation/smart_recycling_final_report.pdf)
  - **포함 내용**: 하드웨어 기구물 제작 공정, 8종 AI 모델 벤치마크, 2-Stage 검사 파이프라인 정밀도, 시스템 신뢰성 검증 지표

---

## 🛠️ 핵심 엔지니어링 기술 문서 (Deep-Dive Reports)

| 문서명                                                         | 주요 기술 스택         | 핵심 다루는 내용                                                                                                                                                             |
| :------------------------------------------------------------- | :--------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 📡 [**통합 통신 프로토콜 명세서**](protocol-spec.md)           | TCP, UART, REST, WS    | • 60 FPS 저지연 8B 바이너리 TCP 패킷 규격<br/>• Jetson ↔ STM32 ASCII UART 프로토콜<br/>• FastAPI 중앙 서버 1:1 WebSocket 및 REST API 규격                                    |
| ⚡ [**60 FPS 스트리밍 최적화 보고서**](network-streaming.md)   | TCP, CUDA, TurboJPEG   | • SSH X11 포워딩 대역폭 병목(442 Mbps) 극복<br/>• Custom Binary TCP 스트림 설계로 대역폭 97.3% 절감<br/>• End-to-End 레이턴시 14.1 ms 초저지연 달성 과정                     |
| 🛠️ [**임베디드 & 엣지 트러블슈팅 보고서**](troubleshooting.md) | Bare-Metal C, Linux OS | • 서보 3축 동시 기동 돌입전류(>1.5A) BOR 전압 급락 방지<br/>• 초음파 센서 비동기 타임아웃 단축 (블로킹 85% 감축)<br/>• 리눅스 DTR 리셋 방어 및 UART ORE 락업 하드웨어 클리어 |

---

## 🎬 시스템 시연 및 UI 갤러리 (Assets)

### 1. 실시간 3채널 멀티캠 데모 (`assets/demo/`)

- 🎥 [**시연 프리뷰 GIF**](assets/demo/demo_preview.gif): 엣지 디바이스 추론 ➔ 실시간 패킷 전송 ➔ 키오스크 화면 렌더링 파이프라인

### 2. 하드웨어 및 키오스크 UI (`assets/images/`)

- `hardware_kiosk.jpg`: 4채널 초음파 수거함 및 서보 분류 기구물 실물 사진
- `kiosk_detect_pass.png`: 정상 배출 품목 검출 및 투입구 개방 UI
- `kiosk_inspect_label_warning.png`: 라벨 미제거 1차 품질 경고 오버레이
- `kiosk_inspect_stain_warning.png`: 내부 오염·이물질 2차 품질 경고 오버레이

### 3. 모바일 앱 사용자 플로우 (`assets/images/`)

- `mobile_01_login.png` ➔ `mobile_02_home.png` ➔ `mobile_03_qr_scan.png` ➔ `mobile_04_kiosk_connected.png`
- `mobile_05_recycle_complete.png` ➔ `mobile_06_history.png` ➔ `mobile_07_shop.png` ➔ `mobile_08_coupon_detail.png` ➔ `mobile_09_mypage.png`

---

## 🔗 서브시스템별 세부 README

각 서브시스템의 빌드 및 개발 환경 설정은 아래 개별 문서에서 확인할 수 있습니다.

- [🧠 `edge_jetson/README.md`](../edge_jetson/README.md) : NVIDIA Jetson Orin Nano TensorRT 10 파이프라인
- [⚡ `mcu_firmware/README.md`](../mcu_firmware/README.md) : STM32F411xE 베어메탈 제어 및 센서 필터링
- [🖥️ `pc_dashboard/README.md`](../pc_dashboard/README.md) : Qt 5.15 키오스크 관제 GUI 애플리케이션
- [⚙️ `server/README.md`](../server/README.md) : FastAPI 비동기 백엔드 및 SQLite WAL 데이터베이스
- [📱 `mobile_app/README.md`](../mobile_app/README.md) : Android (Jetpack Compose + MVI) 사용자 앱
