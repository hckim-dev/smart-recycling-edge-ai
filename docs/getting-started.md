# 전체 시스템 실행 안내

[프로젝트 소개](../README.md) · [시스템 구조](architecture.md) · [통신 규약](protocol-spec.md)

이 문서는 전체 장치의 연결·구동 순서를 안내합니다. 패키지 설치와 빌드 명령은 각 모듈 README에 모았습니다.

## 준비 항목

| 영역 | 필요한 환경·자료 |
| --- | --- |
| 엣지 추론 | Jetson Orin Nano, USB 카메라, TensorRT 10.x·CUDA·OpenCV·PyCUDA 환경 |
| 분류 제어 | STM32F411xE, ST-Link, 서보 3개, 초음파 4채널, 기구·전원·배선 |
| 키오스크 | Qt Widgets·Network·WebSockets, C++17 컴파일러, qmake |
| 서버 | Python 환경과 [서버 의존성](../server/requirements.txt) |
| 앱 | Android Studio 또는 Gradle 환경, Android 8.0/API 26 이상 기기 |
| 연결 | Jetson-Qt TCP, Qt·앱-서버 HTTP/WS, Jetson-MCU 시리얼 경로 |

MCU 핀맵·클럭·툴체인은 [펌웨어 README](../mcu_firmware/README.md), Android SDK·Gradle·JDK 설정은 [앱 README](../mobile_app/README.md)를 기준으로 합니다. 실제 JetPack·TensorRT 버전과 카메라 지원 모드는 대상 장치에서 확인해야 합니다.

## 1. 모델 준비

다음 파일을 `edge_jetson/models/`에 배치합니다.

| 파일 | 역할·입력 |
| --- | --- |
| `recycle_detect_yolo11s.engine` | 필수 재질 검출 엔진. 640×640 RGB, NCHW, 0~1 |
| `pet_label_efficientnet.engine` | PET 라벨 검사. 224×224 RGB, NHWC, 0~255 |
| `pet_content_mobilenetv3.engine` | PET 내용물 검사. 224×224 RGB, NHWC, 0~255 |

실제 dtype·입출력 shape는 엔진에서 확인해야 합니다. 분류기 정규화는 내보낸 모델 그래프에 포함하는 구성입니다. 최종 검출 클래스 순서는 PAPER/CAN/PET/VINYL입니다.

가중치·ONNX·TensorRT 엔진·데이터는 저장소에 포함하지 않습니다. [노트북](../ai_models/Readme.md#노트북과-재현)에 ONNX 내보내기 코드가 있으며, 타깃 TensorRT 엔진 생성 스크립트는 포함하지 않습니다.

검출 엔진이 없으면 시작할 수 없습니다. PET 엔진이 없거나 로드에 실패하면 해당 검사는 BYPASS로 통과 처리하므로, 전체 검사 시연 전 엔진별 ACTIVE 로그를 확인합니다.

## 2. 연결 주소 설정

| 설정 파일 | 확인할 항목 |
| --- | --- |
| [Jetson config.py](../edge_jetson/configs/config.py) | 카메라 ID·해상도, `SerialConfig.port`·baudrate, TCP port |
| [Qt app_config.h](../pc_dashboard/configs/app_config.h) | `DEFAULT_JETSON_IP`·`JETSON_PORT`, `DEFAULT_BACKEND_HOST`·port, `DEFAULT_BIN_ID` |
| [서버 config.py](../server/app/config.py) | HOST·PORT, 기본 키오스크 ID, DB 경로 |
| [앱 Constants.kt](../mobile_app/app/src/main/java/com/hocheol/smartrecyclingedgeai/utils/Constants.kt) | `BASE_URL`과 `WS_BASE_URL` 모두 같은 서버 주소로 설정 |

기본 포트는 서버 8000, Jetson TCP 9000입니다. 앱과 Qt가 같은 서버·키오스크 ID를 사용해야 합니다. 앱 주소의 `localhost`는 PC 서버가 아니라 Android 기기 자신을 가리킵니다.

## 3. 구동 순서

| 순서 | 작업 | 정상 시작 확인 |
| --- | --- | --- |
| 1 | [서버 실행](../server/README.md#실행) | `GET /health` 응답, `/docs` 접속 |
| 2 | [MCU 빌드·플래시](../mcu_firmware/README.md#빌드와-플래시) | 초기 구동·배선 확인, UART `$BIN:` 보고 |
| 3 | [Jetson 실행](../edge_jetson/README.md#실행) | 카메라·엔진 로드, 시리얼 연결, TCP 바인딩 로그 |
| 4 | [Qt 실행](../pc_dashboard/README.md#빌드와-실행) | Jetson 연결 표시, 적재율 갱신, 서버 WS 연결 로그 |
| 5 | [앱 설치·실행](../mobile_app/README.md#빌드와-실행) | 사용자 정보 조회 후 QR 연결 |

Jetson 시리얼 로그가 SIMULATE 모드이면 물리 장치 연결을 확인한 상태가 아닙니다. 대상 포트의 접근 권한과 배선을 먼저 확인합니다. 권한 점검은 [시리얼 점검 절차](troubleshooting.md#시리얼-연결과-장치-점검)에 정리했습니다.

## 4. 통합 동작 확인

1. Qt 대기 화면의 QR을 앱으로 스캔합니다.
2. 앱의 연결 상태와 Qt의 회원 배출 화면 전환을 확인합니다.
3. 재활용품을 제시하고 품목·PET 검사 결과·MCU 구동 순서를 확인합니다.
4. Qt에서 도어 OPEN 전이마다 수량이 한 번 증가하는지 확인합니다.
5. Qt의 투입 완료 버튼을 누르고 앱 완료 이벤트와 배출 이력을 확인합니다.
6. 새 세션에서 Qt·앱 각각의 취소가 상대 화면에 반영되는지 확인합니다.

실제 물체 수와 집계, 기구 도착 여부는 따로 관찰해야 합니다. 현재 카운트 자체는 실제 낙하 확인을 의미하지 않습니다.

## 부분 실행과 개발 모드

| 방법 | 확인 가능한 범위 |
| --- | --- |
| 서버의 `/docs` | 사용자·기기·정산·포인트 API의 개별 호출 |
| [mock_mobile_client.py](../server/tests/mock_mobile_client.py) | 모바일 앱을 대신한 로그인·바인딩·결과 수신. 실행 중 Enter 입력과 Qt의 완료 조작 필요 |
| MCU UART 수동 명령 | 분류 경로·도어·필터 초기화 개별 동작 |
| `SerialConfig.enabled = False` | Jetson의 UART 가상 상태 처리. 카메라·검출 엔진은 여전히 필요 |
| Qt QR 영역 클릭 | 앱 없이 회원 형태의 화면 시연. 실제 사용자 ID 연결·계정 적립은 별도 |

Mock 클라이언트는 전체 장치의 자동 E2E 테스트가 아닙니다. 서버만 실행한 경우 Qt의 세션·집계·정산을 대신 수행하지 않습니다.

## 종료

Jetson은 `q` 또는 종료 신호로 메인 루프를 끝내고 카메라·소켓·시리얼·엔진 자원을 정리합니다. MCU는 개방 명령 처리 후 10초가 지나면 Super Loop에서 중립 복귀를 요청합니다. Qt·앱 세션 종료와 물리 기구 정지는 서로 다른 경로이므로 종료 전 실제 장치 상태도 확인합니다.
