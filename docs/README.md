# 기술 문서

[프로젝트 소개](../README.md)에서 시연과 개발 역할을 확인한 뒤, 관심 영역에 따라 아래 문서로 이어갈 수 있습니다.

| 확인할 내용 | 문서 |
| --- | --- |
| 장치별 책임, 스레드·인터럽트, 상태 전이 | [시스템 아키텍처](architecture.md) |
| 장치·모델 준비와 전체 구동 순서 | [실행 안내](getting-started.md) |
| 실제 송수신 필드·메시지·오류 처리 | [통신 프로토콜](protocol-spec.md) |
| 영상 전송, 분류 기구, 센서·시리얼 설계 | [설계와 트러블슈팅](troubleshooting.md) |
| 데이터 분할, 모델 비교, 평가 결과 | [AI 학습과 평가](../ai_models/Readme.md) |

## 모듈별 구현과 실행

[Jetson](../edge_jetson/README.md) · [STM32](../mcu_firmware/README.md) · [Qt](../pc_dashboard/README.md) · [서버](../server/README.md) · [Android](../mobile_app/README.md)

모듈 README는 해당 영역의 설정·빌드·실행을, 이 디렉터리는 여러 모듈에 걸친 설계와 연동을 설명합니다.

## 발표·시연 자료

- [통합 시연 영상](https://github.com/user-attachments/assets/a7b54b54-8b72-4728-b04c-dced2dc5ca10)
- [발표자료 PDF](presentation/smart_recycling_final_report.pdf): 발표 당시 기록입니다. 현재 구현 명세는 코드와 위 기술 문서를 기준으로 합니다.
- [시연 GIF](assets/demo/demo_preview.gif) · [장치·화면 이미지](assets/images/)

현재 코드의 동작, 노트북에 저장된 평가 출력, 발표 당시 결과를 구분해 다룹니다. 설정된 FPS나 이론상 대역폭을 실제 장치의 측정 결과로 해석하지 않습니다.

저장소 유지관리: [자격 증명 관리와 Secret Scan](credential-security.md)
