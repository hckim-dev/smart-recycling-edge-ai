"""STM32 MCU 연동 UART 시리얼 I/O 및 가상 시뮬레이션 모듈."""

import threading
import time
from contextlib import suppress

import serial
from serial import SerialException

from stream.protocol import (
    DEFAULT_DOOR_ITEM,
    BinLevels,
    DoorAction,
    DoorState,
    DoorStatus,
    McuPacketType,
    ProtocolParser,
)


class SerialController:
    """스레드 동기화 락 기반 UART 송수신 및 하드웨어 미연결 시뮬레이션 지원 클래스."""

    def __init__(
        self,
        port: str = "/dev/ttyTHS1",
        baudrate: int = 115200,
        timeout: float = 0.1,
        enabled: bool = False,
    ):
        """통신 파라미터 초기화 및 포트 연결 (비활성화 시 가상 모드 기동)."""
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.enabled = enabled

        self.ser: serial.Serial | None = None
        self.running: bool = False
        self.rx_thread: threading.Thread | None = None

        self._lock = threading.Lock()
        self._bin_levels = BinLevels()
        self._door_status = DoorStatus()
        self._last_commanded_item: str = DEFAULT_DOOR_ITEM

        if self.enabled:
            self._connect()
        else:
            print("[SERIAL] 시뮬레이션 모드로 시작합니다.")

    def _connect(self):
        """UART 인터페이스 열기 및 백그라운드 수신(RX) 워커 스레드 생성."""
        if serial is None:
            print(
                "[SERIAL] pyserial 모듈이 설치되지 않아 시뮬레이션 모드로 동작합니다."
            )
            self.ser = None
            self.enabled = False
            return

        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                write_timeout=self.timeout,
                dsrdtr=False,
                rtscts=False,
            )
            # 포트 오픈(DTR 토글) 직후 MCU 리셋 및 소프트스타트 완료까지 1.0초 대기
            time.sleep(1.0)
            with suppress(Exception):
                self.ser.reset_input_buffer()
                self.ser.reset_output_buffer()

            self.running = True
            self.rx_thread = threading.Thread(
                target=self._rx_loop, name="SerialRxWorker", daemon=True
            )
            self.rx_thread.start()
            print(f"[SERIAL] 연결 성공 -> {self.port} ({self.baudrate} bps)")
        except (SerialException, OSError) as e:
            print(f"[SERIAL ERROR] 포트 연결 실패: {e}")
            self.ser = None
            self.enabled = False

    def _rx_loop(self):
        """MCU 텔레메트리 패킷 상시 수신 및 스레드 안전 내부 캐시 갱신."""
        while self.running and self.ser is not None:
            try:
                line = self.ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                packet_type, data = ProtocolParser.parse_mcu_line(line)
                if packet_type == McuPacketType.BIN and isinstance(data, BinLevels):
                    with self._lock:
                        self._bin_levels = data
                elif packet_type == McuPacketType.DOOR and isinstance(data, DoorStatus):
                    with self._lock:
                        # 물리 센서 닫힘 완료 시 제어 품목을 기본값(ALL)으로 리셋
                        reported_item = (
                            self._last_commanded_item
                            if data.state == DoorState.OPEN
                            else DEFAULT_DOOR_ITEM
                        )
                        self._door_status = DoorStatus(
                            item=reported_item, state=data.state
                        )

            except (SerialException, OSError):
                time.sleep(0.01)

    def send_command(self, action: DoorAction, item_name: str | None = None) -> bool:
        """도어 제어 패킷 생성 및 전송 (가상 모드 시 내부 상태 동기화)."""
        with self._lock:
            if action == DoorAction.OPEN:
                self._last_commanded_item = (item_name or DEFAULT_DOOR_ITEM).upper()
            else:
                self._last_commanded_item = DEFAULT_DOOR_ITEM

        payload = ProtocolParser.encode_door_command(action, item_name)
        success = self._write(payload)

        # 실제 시리얼 포트 미연결 환경에서도 FSM 로직 검증이 가능하도록 가상 상태 동기화
        if success and (not self.enabled or self.ser is None):
            with self._lock:
                state = (
                    DoorState.OPEN if action == DoorAction.OPEN else DoorState.CLOSED
                )
                self._door_status = DoorStatus(
                    item=self._last_commanded_item, state=state
                )

        return success

    def _write(self, text: str) -> bool:
        """바이트 스트림 송신 및 버퍼 비우기(Flush)."""
        if not self.enabled or self.ser is None:
            print(f"[SERIAL SIMULATE TX] -> {text.strip()}")
            return True

        try:
            self.ser.write(text.encode("utf-8"))
            self.ser.flush()
            print(f"[SERIAL TX] -> {text.strip()}")
            return True
        except (SerialException, OSError) as e:
            print(f"[SERIAL ERROR] 송신 실패: {e}")
            return False

    def get_latest_data(self) -> tuple[dict[str, int], dict[str, str]]:
        """스레드 락 기반 최신 적재율 및 도어 상태 반환."""
        with self._lock:
            return self._bin_levels.to_dict(), self._door_status.to_dict()

    def close(self):
        """수신 스레드 안전 조인 및 시리얼 통신 리소스 해제."""
        self.running = False
        if self.rx_thread and self.rx_thread.is_alive():
            self.rx_thread.join(timeout=0.5)
            self.rx_thread = None

        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
                print(f"[SERIAL] {self.port} 연결 해제 완료")
            except (SerialException, OSError):
                pass
            finally:
                self.ser = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        self.close()
