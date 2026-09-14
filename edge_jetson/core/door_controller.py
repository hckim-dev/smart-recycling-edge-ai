"""비전 감지 결과 기반 디바운스 및 안전 타이머 적용 수거함 도어 FSM 제어 모듈."""

import time
from typing import Any

from configs.config import DetectionKey, DoorConfig
from stream.protocol import DoorAction, DoorState
from stream.serial_controller import SerialController


class AutoDoorController:
    """연속 검출 디바운스로 개방하고, 최소 유지 시간과 최대 타임아웃을 보장하는 도어 상태 제어기."""

    def __init__(self, serial_ctrl: SerialController, config: DoorConfig):
        """도어 제어 FSM 상태 및 디바운스/타이머 카운터 초기화."""
        self.serial_ctrl = serial_ctrl
        self.config = config

        self.current_state: DoorState = DoorState.CLOSED
        self.active_item: str | None = None
        self.door_open_timestamp: float = 0.0

        self.candidate_item: str | None = None
        self.candidate_start_time: float = 0.0
        self.consecutive_count: int = 0
        self.lost_count: int = 0

    def request_open(self, item: str | None = None) -> bool:
        """명시적 도어 개방 처리."""
        curr_time = time.time()
        target_item = (item or self.candidate_item or "ALL").upper()
        if self.serial_ctrl.send_command(DoorAction.OPEN, target_item):
            self.current_state = DoorState.OPEN
            self.active_item = target_item
            self.door_open_timestamp = curr_time
            self.lost_count = 0
            print(f"[DOOR] 명시적 명령 도어 개방: {target_item}")
            return True
        return False

    def request_close(self) -> bool:
        """명시적 도어 폐쇄 처리."""
        if self.serial_ctrl.send_command(DoorAction.CLOSE):
            self.current_state = DoorState.CLOSED
            self.active_item = None
            self.candidate_item = None
            self.candidate_start_time = 0.0
            self.consecutive_count = 0
            self.lost_count = 0
            print("[DOOR] 명시적 명령 도어 폐쇄")
            return True
        return False

    def process_detections(self, detections: list[dict[str, Any]]) -> None:
        """프레임별 검출 결과를 FSM에 투입하여 도어 상태 추적 및 자동 개폐 처리."""
        curr_time = time.time()
        top_item = self._extract_top_item(detections)

        if self.current_state == DoorState.CLOSED:
            if self.config.auto_open:
                self._handle_closed_state(top_item, curr_time)
            else:
                self._update_candidate(top_item, curr_time)
        elif self.current_state == DoorState.OPEN:
            self._handle_open_state(top_item, curr_time)

    def _extract_top_item(self, detections: list[dict[str, Any]]) -> str | None:
        """프레임 내 검출 객체 중 최고 신뢰도를 가진 품목 카테고리명(대문자) 반환.

        단, 2단계 세부 품질 검사(inspection) 결과가 존재하고 불합격(passed == False)인
        경우 도어 자동 개방 대상에서 안전하게 배제합니다.
        """
        if not detections:
            return None
        best_det = max(detections, key=lambda x: x.get(DetectionKey.CONFIDENCE, 0.0))

        # 2-Stage 세부 품질 검사 결과 반영 (라벨 부착, 오염 등 불합격 시 개방 차단)
        inspection = best_det.get("inspection")
        if inspection is not None and not inspection.get("passed", True):
            return None

        item = best_det.get(DetectionKey.CATEGORY) or best_det.get(
            DetectionKey.CLASS_NAME, ""
        )
        return item.upper() or None

    def _update_candidate(self, top_item: str | None, curr_time: float) -> None:
        """카메라 앞 후보 품목 및 안정 감지 카운트 추적."""
        if top_item is None:
            self.candidate_item = None
            self.consecutive_count = 0
            self.candidate_start_time = 0.0
            return

        if top_item == self.candidate_item:
            self.consecutive_count += 1
        else:
            self.candidate_item = top_item
            self.consecutive_count = 1
            self.candidate_start_time = curr_time

    def _handle_closed_state(self, top_item: str | None, curr_time: float) -> None:
        """닫힘 상태(자동 모드 전용): 1.5초 이상 연속 인식 충족 시 자동 OPEN 명령 송신."""
        if top_item is None:
            self.candidate_item = None
            self.consecutive_count = 0
            self.candidate_start_time = 0.0
            return

        if top_item == self.candidate_item:
            self.consecutive_count += 1
        else:
            self.candidate_item = top_item
            self.consecutive_count = 1
            self.candidate_start_time = curr_time

        elapsed = curr_time - self.candidate_start_time

        # 1.5초(stable_sec) 유지 시간 및 최소 프레임 수 충족 시 자동 도어 개방
        if (
            elapsed >= self.config.stable_sec
            and self.consecutive_count >= self.config.stable_frames
            and self.serial_ctrl.send_command(DoorAction.OPEN, top_item)
        ):
            self.current_state = DoorState.OPEN
            self.active_item = top_item
            self.door_open_timestamp = curr_time
            self.lost_count = 0
            print(
                f"[DOOR] 자동 개방: {top_item} (유지 시간: {elapsed:.2f}초, 프레임: {self.consecutive_count})"
            )

    def _handle_open_state(self, top_item: str | None, curr_time: float) -> None:
        """열림 상태: 최소 홀드 시간 보장 및 부재 카운트 초과(또는 안전 최대 타임아웃) 시 CLOSE 명령 송신."""
        elapsed_open = curr_time - self.door_open_timestamp
        is_max_timeout = elapsed_open >= self.config.max_open_sec

        # 투입 중 도어 끼임 사고 방지를 위해 최소 유지 시간 동안은 닫힘 검사 유보 (최대 타임아웃 시 강제 통과)
        if not is_max_timeout and elapsed_open < self.config.min_hold_sec:
            return

        if top_item == self.active_item and not is_max_timeout:
            self.lost_count = 0
        else:
            self.lost_count += 1

        # 물체 부재 카운트 초과 또는 장시간 개방 방지용 타임아웃 도달 시 도어 폐쇄
        if (
            self.lost_count >= self.config.lost_tolerance or is_max_timeout
        ) and self.serial_ctrl.send_command(DoorAction.CLOSE):
            self.current_state = DoorState.CLOSED
            self.active_item = None
            self.candidate_item = None
            self.consecutive_count = 0
            self.lost_count = 0
