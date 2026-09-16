"""Logitech C270 웹캠 (USB V4L2 백엔드) 기반 초저지연 백그라운드 카메라 프레임 캡처 모듈."""

import threading
import time

import cv2
import numpy as np


class CameraStream:
    """백그라운드 스레드에서 V4L2 캡처 큐를 상시 소모하여 최신 프레임을 갱신하는 래퍼 클래스."""

    def __init__(
        self,
        device_id: int = 0,
        width: int = 640,
        height: int = 480,
        fps: int = 60,
        buffer_size: int = 1,
        flip_horizontal: bool = True,
    ):
        """V4L2 캡처 장치 초기화 및 백그라운드 수신 스레드 기동."""
        self.device_id = device_id
        self.width = width
        self.height = height
        self.fps = fps
        self.flip_horizontal = flip_horizontal

        self.cap: cv2.VideoCapture | None = None
        self.thread: threading.Thread | None = None
        self.frame: np.ndarray | None = None
        self.ret: bool = False
        self.running: bool = False
        self.lock: threading.Lock = threading.Lock()

        self.cap = cv2.VideoCapture(device_id, cv2.CAP_V4L2)
        # USB 버스 대역폭 절감 및 60fps 유지를 위한 MJPG 하드웨어 압축 포맷 강제
        self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        self.cap.set(cv2.CAP_PROP_FPS, fps)
        # 드라이버 내부 링 버퍼 지연(Lag)을 원천 차단하기 위한 버퍼 크기 최소화(1)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, buffer_size)

        if not self.cap.isOpened():
            raise RuntimeError(
                f"[CAMERA ERROR] 카메라 장치({device_id})를 열 수 없습니다."
            )

        self.ret, self.frame = self.cap.read()
        if not self.ret or self.frame is None:
            self.release()
            raise RuntimeError(
                f"[CAMERA ERROR] 카메라({device_id}) 초기 프레임 획득 실패"
            )

        self.running = True
        self.thread = threading.Thread(
            target=self._capture_loop, name="CameraWorker", daemon=True
        )
        self.thread.start()
        print(f"[CAMERA] 백그라운드 캡처 시작 ({width}x{height} @ {fps}fps)")

    def _capture_loop(self):
        """드라이버 버퍼 적체 방지를 위해 최신 프레임을 지속 폴링하는 루프."""
        while self.running:
            if self.cap is None:
                break

            ret, frame = self.cap.read()
            if ret and frame is not None:
                if self.flip_horizontal:
                    frame = cv2.flip(frame, 1)

                with self.lock:
                    self.frame = frame
                    self.ret = ret
            else:
                with self.lock:
                    self.ret = False
                # 프레임 수신 실패 시 점유율 폭주(Busy-wait) 방지용 대기
                time.sleep(0.005)

    def read(self) -> tuple[bool, np.ndarray | None]:
        """스레드 락 기반 최신 BGR 프레임 즉시 반환 (논블로킹)."""
        with self.lock:
            if not self.ret or self.frame is None:
                return False, None
            return True, self.frame

    def release(self):
        """캡처 스레드 안전 종료 및 V4L2 장치 디스크립터 해제."""
        self.running = False

        if self.thread is not None and self.thread.is_alive():
            self.thread.join(timeout=1.0)
            self.thread = None

        if self.cap is not None:
            if self.cap.isOpened():
                self.cap.release()
                print("[CAMERA] 장치 해제 완료")
            self.cap = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.release()

    def __del__(self):
        self.release()
