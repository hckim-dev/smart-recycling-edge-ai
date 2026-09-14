"""스마트 재활용 키오스크 엣지 파이프라인 최상위 메인 루프 모듈."""

import signal
import time
from contextlib import suppress

from configs.config import (
    MODEL_CLASS_MAP,
    Category,
    DetectionKey,
    ProtocolKey,
    cfg,
)
from core.camera import CameraStream
from core.detector import YOLOv11Detector
from core.door_controller import AutoDoorController
from core.inspector import InspectionPipeline
from core.trt_engine import TensorRTEngine
from stream.protocol import ClientAction, ClientCommand
from stream.serial_controller import SerialController
from stream.socket_server import StreamSocketServer
from utils.keyboard import NonBlockingKeyReader


def main():
    """모듈 초기화, AI 객체 검출, 도어 FSM 제어 및 관제 PC 스트리밍 파이프라인 구동."""
    print("=" * 60)
    print("[EDGE AI] 스마트 분리수거 비전 시스템 부팅 중...")
    print("=" * 60)

    # 1. 하드웨어 및 파이프라인 서브시스템 초기화
    camera = CameraStream(
        device_id=cfg.cam.device_id,
        width=cfg.cam.width,
        height=cfg.cam.height,
        fps=cfg.cam.fps,
        buffer_size=cfg.cam.buffer_size,
        flip_horizontal=cfg.cam.flip_horizontal,
    )

    trt_engine = TensorRTEngine(engine_path=cfg.model.engine_path)

    detector = YOLOv11Detector(
        engine=trt_engine,
        input_shape=cfg.model.input_shape,
        conf_thresh=cfg.model.conf_threshold,
        iou_thresh=cfg.model.iou_threshold,
        class_map=MODEL_CLASS_MAP,
    )

    # 2단계 세부 검사(라벨, 오염 등) 플러그인 파이프라인 초기화
    inspector_pipeline = InspectionPipeline(config=cfg.inspection)

    socket_server = StreamSocketServer(
        host=cfg.net.host,
        port=cfg.net.port,
        jpeg_quality=cfg.net.jpeg_quality,
        timeout=cfg.net.socket_timeout,
    )

    serial_ctrl = SerialController(
        port=cfg.serial.port,
        baudrate=cfg.serial.baudrate,
        timeout=cfg.serial.timeout,
        enabled=cfg.serial.enabled,
    )

    door_ctrl = AutoDoorController(serial_ctrl=serial_ctrl, config=cfg.door)

    key_reader = NonBlockingKeyReader()
    is_running = True

    # SIGINT(Ctrl+C) 및 SIGTERM 수신 시 안전 종료 플래그 설정
    def handle_signal(sig, frame):
        nonlocal is_running
        print("\n[STOP] 종료 시그널 수신")
        is_running = False

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    print(f"\n[SERVER] 파이프라인 준비 완료 (TCP Port: {cfg.net.port})")
    prev_time = time.time()

    try:
        # 2. 실시간 엣지 파이프라인 메인 루프
        while is_running:
            # 논블로킹 키 입력 감지 ('q' 입력 시 종료)
            key = key_reader.get_key()
            if key and key.lower() == "q":
                break

            # 비차단 클라이언트 접속 폴링 및 관제 PC(Qt) 제어 명령 수신
            if not socket_server.is_connected:
                socket_server.accept_client()
            else:
                for raw_cmd in socket_server.receive_commands():
                    cmd = ClientCommand.from_dict(raw_cmd)
                    if cmd is None:
                        continue
                    if cmd.action == ClientAction.OPEN:
                        door_ctrl.request_open(cmd.item)
                    elif cmd.action == ClientAction.CLOSE:
                        door_ctrl.request_close()

            ret, frame = camera.read()
            if not ret or frame is None:
                time.sleep(0.002)  # 프레임 대기 시 CPU 과점유(Busy-wait) 방지
                continue

            # 비전 AI 추론 및 지연시간(Latency) 계측
            t0 = time.time()
            detections = detector.detect(frame)

            # 2-Stage 세부 품질 검사 (Crop & Inspect): 대상 품목(PET 등) 선별 평가
            if cfg.inspection.enabled and detections:
                h, w = frame.shape[:2]
                for det in detections:
                    cat_str = det.get(DetectionKey.CATEGORY, "")
                    try:
                        cat = Category(cat_str)
                    except ValueError:
                        cat = Category.UNKNOWN

                    # 2단계 검사 태스크가 등록된 품목인 경우 BBox 안전 클리핑 후 세부 품질 검사 실행
                    if inspector_pipeline.has_tasks_for(cat):
                        box = det.get(DetectionKey.BOX, [0, 0, 0, 0])
                        x1 = max(0, min(w - 1, int(box[0])))
                        y1 = max(0, min(h - 1, int(box[1])))
                        x2 = max(0, min(w, int(box[2])))
                        y2 = max(0, min(h, int(box[3])))
                        min_size = cfg.inspection.min_crop_size

                        if (x2 - x1) >= min_size and (y2 - y1) >= min_size:
                            crop = frame[y1:y2, x1:x2]
                            det[DetectionKey.INSPECTION.value] = (
                                inspector_pipeline.inspect_crop(crop, cat)
                            )

            infer_ms = (time.time() - t0) * 1000.0

            # 감지 결과 기반 수거함 도어 FSM 상태 전이
            door_ctrl.process_detections(detections)

            # 파이프라인 실효 처리 속도(FPS) 계산
            curr_time = time.time()
            time_diff = curr_time - prev_time
            fps = 1.0 / time_diff if time_diff > 0 else 0.0
            prev_time = curr_time

            # 관제 PC 연결 시에만 JPEG 압축 및 텔레메트리 바이너리 전송 (불필요한 연산 방지)
            if socket_server.is_connected:
                bin_levels, door_status = serial_ctrl.get_latest_data()
                meta = {
                    ProtocolKey.TIMESTAMP.value: curr_time,
                    ProtocolKey.FPS.value: round(fps, 1),
                    ProtocolKey.INFER_MS.value: round(infer_ms, 2),
                    ProtocolKey.DETECTIONS.value: detections,
                    ProtocolKey.BIN_LEVELS.value: bin_levels,
                    ProtocolKey.DOOR.value: door_status,
                }
                socket_server.send_frame(frame, meta)

    finally:
        # 3. 종료 시 하드웨어 I/O, 네트워크 및 GPU 메모리 안전 일괄 해제
        print("\n[CLEANUP] 전체 리소스를 안전하게 해제합니다...")
        with suppress(Exception):
            key_reader.restore()
        with suppress(Exception):
            socket_server.close()
        with suppress(Exception):
            serial_ctrl.close()
        with suppress(Exception):
            inspector_pipeline.destroy()
        with suppress(Exception):
            trt_engine.destroy()
        with suppress(Exception):
            camera.release()


if __name__ == "__main__":
    main()
