"""플러그인 기반 2-Stage 객체 세부 품질 검사(라벨, 오염 등) 파이프라인 모듈."""

from typing import Any

import cv2
import numpy as np
from configs.config import Category, InspectionPipelineConfig, InspectionTaskConfig

from core.trt_engine import TensorRTEngine


class ClassifierInspector:
    """단일 경량 분류(MobileNetV3 등) 모델 기반 품질 평가 모듈."""

    def __init__(self, task_cfg: InspectionTaskConfig) -> None:
        """엔진 파일 실재 여부를 검증하고, 미존재 시 Graceful Bypass 모드로 초기화."""
        self.cfg = task_cfg
        self.engine: TensorRTEngine | None = None
        self.is_ready = False

        if not self.cfg.enabled:
            print(f"[INSPECTOR] [{self.cfg.task_id}] 설정에 의해 비활성화됨 (DISABLED)")
            return

        if self.cfg.engine_path.exists():
            try:
                self.engine = TensorRTEngine(self.cfg.engine_path)
                self.is_ready = True
                print(
                    f"[INSPECTOR] [{self.cfg.task_id}] 엔진 로드 성공 (ACTIVE: {self.cfg.engine_path.name})"
                )
            except Exception as exc:
                print(f"[INSPECTOR ERROR] [{self.cfg.task_id}] 엔진 로드 실패: {exc}")
        else:
            # 모델 담당자가 아직 학습 중인 경우: 시스템 다운 없이 우아하게 바이패스 모드로 진입
            print(
                f"[INSPECTOR WARN] [{self.cfg.task_id}] 엔진 파일 없음 "
                f"({self.cfg.engine_path.name}) -> 자동 바이패스(BYPASS) 모드로 안전 구동"
            )

    def evaluate(self, blob: np.ndarray) -> tuple[bool, float | None, str | None]:
        """추론 실행 및 합격/불합격 판정.

        Returns:
            (passed: bool, score: float | None, fail_reason: str | None)
        """
        if not self.is_ready or self.engine is None:
            # 아직 준비되지 않은 모델은 통과(Bypass)로 처리하여 타 기능 정상 검증 보장
            return True, None, None

        raw_output = self.engine.execute(blob)  # shape (1, 1)
        score = float(np.squeeze(raw_output))

        if self.cfg.is_positive_fail:
            is_fail = score >= self.cfg.threshold
        else:
            is_fail = score < self.cfg.threshold

        if is_fail:
            return False, score, self.cfg.fail_reason
        return True, score, None

    def destroy(self) -> None:
        """GPU VRAM 및 CUDA 리소스 안전 해제."""
        if self.engine is not None:
            self.engine.destroy()
            self.engine = None
            self.is_ready = False


class InspectionPipeline:
    """검출 객체별 등록된 검사 태스크들을 일괄 조율하고 종합 리포트를 산출하는 오케스트레이터."""

    def __init__(self, config: InspectionPipelineConfig) -> None:
        """등록된 태스크 목록에 기반하여 검사 모듈들을 동적으로 바인딩."""
        self.cfg = config
        self.inspectors: list[ClassifierInspector] = [
            ClassifierInspector(task) for task in self.cfg.tasks
        ]

    def inspect_crop(self, crop_bgr: np.ndarray, category: Category) -> dict[str, Any]:
        """BBox 영역에 대해 해당 카테고리에 할당된 모든 검사를 단일 전처리로 순차 평가."""
        h, w = crop_bgr.shape[:2]
        if h < self.cfg.min_crop_size or w < self.cfg.min_crop_size:
            return {
                "passed": False,
                "reasons": ["CROP_TOO_SMALL"],
                "details": {},
            }

        # 224x224 RGB NHWC float32 공통 전처리 (1회만 수행하여 다중 엔진이 메모리 공유)
        resized = cv2.resize(crop_bgr, (224, 224), interpolation=cv2.INTER_LINEAR)
        rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.float32)
        blob = np.expand_dims(rgb, axis=0)

        all_passed = True
        reasons: list[str] = []
        details: dict[str, Any] = {}

        for inspector in self.inspectors:
            # 해당 카테고리(예: Category.PET) 전용 검사기만 선별 실행
            if inspector.cfg.target_category != category:
                continue

            passed, score, fail_reason = inspector.evaluate(blob)
            status_str = (
                "PASS"
                if passed and score is not None
                else "FAIL"
                if not passed
                else "BYPASS"
            )

            details[inspector.cfg.task_id] = {
                "passed": passed,
                "score": round(score, 3) if score is not None else None,
                "status": status_str,
            }

            if not passed:
                all_passed = False
                if fail_reason:
                    reasons.append(fail_reason)

        return {
            "passed": all_passed,
            "reasons": reasons,
            "details": details,
        }

    def destroy(self) -> None:
        """모든 하위 검사 모듈의 자원을 안전하게 일괄 해제."""
        for inspector in self.inspectors:
            inspector.destroy()
