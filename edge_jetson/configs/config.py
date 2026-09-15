"""스마트 재활용 키오스크 전역 불변(Frozen) 설정 모듈."""

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

JETSON_ROOT_DIR = Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class CameraConfig:
    """V4L2 카메라 캡처 파라미터 설정."""

    device_id: int = 0
    width: int = 640
    height: int = 480
    fps: int = 60
    buffer_size: int = 1  # 큐 프레임 지연(Lag) 방지용 최소 버퍼 크기
    flip_horizontal: bool = True  # 키오스크 인터랙션용 좌우 반전


class Category(str, Enum):
    """재활용 대상 4종 품목 열거형 (시스템 표준 순서)."""

    UNKNOWN = "UNKNOWN"
    PAPER = "PAPER"
    CAN = "CAN"
    PET = "PET"
    VINYL = "VINYL"


class ProtocolKey(str, Enum):
    """관제 PC 연동 텔레메트리 패킷 JSON 키 규격 (Qt Config::JetsonProtocol과 1:1 대칭)."""

    TIMESTAMP = "timestamp"
    FPS = "fps"
    INFER_MS = "infer_ms"
    DETECTIONS = "detections"
    BIN_LEVELS = "bin_levels"
    DOOR = "door"


class DetectionKey(str, Enum):
    """객체 검출 결과 딕셔너리 키 규격."""

    CLASS_ID = "class_id"
    CLASS_NAME = "class_name"
    CATEGORY = "category"
    CONFIDENCE = "confidence"
    BOX = "box"
    INSPECTION = "inspection"


class InspectionKey(str, Enum):
    """2단계 세부 품질 검사 결과 딕셔너리 키 규격."""

    PASSED = "passed"
    REASONS = "reasons"
    DETAILS = "details"
    SCORE = "score"
    STATUS = "status"


class InspectionStatus(str, Enum):
    """개별 검사 태스크 판정 상태 규격."""

    PASS = "PASS"
    FAIL = "FAIL"
    BYPASS = "BYPASS"


class InspectionReason(str, Enum):
    """공통 검사 불합격(반려) 사유 규격."""

    LABEL_ATTACHED = "LABEL_ATTACHED"
    CONTAMINATED = "CONTAMINATED"
    CROP_TOO_SMALL = "CROP_TOO_SMALL"
    UNKNOWN = "UNKNOWN"


@dataclass(frozen=True)
class ModelClassMeta:
    """YOLO 모델 출력 인덱스와 도메인 품목 정보 간의 1:1 매핑 메타데이터."""

    class_id: int
    name_en: str
    name_ko: str
    category: Category


# YOLO 모델 학습 순서(0: 종이, 1: 캔, 2: 페트, 3: 비닐) 1:1 직결 매핑
MODEL_CLASS_MAP: tuple[ModelClassMeta, ...] = (
    ModelClassMeta(0, "paper", "종이", Category.PAPER),
    ModelClassMeta(1, "can", "캔", Category.CAN),
    ModelClassMeta(2, "pet", "페트", Category.PET),
    ModelClassMeta(3, "vinyl", "비닐", Category.VINYL),
)


@dataclass(frozen=True)
class ModelConfig:
    """YOLOv11 TensorRT 엔진 경로 및 추론 임계값 설정."""

    engine_path: Path = JETSON_ROOT_DIR / "models" / "recycle_detect_yolo11s.engine"
    input_shape: tuple[int, int] = (640, 640)
    conf_threshold: float = 0.80
    iou_threshold: float = 0.45
    # YOLO 모델 학습 클래스 순서 (0: 종이, 1: 캔, 2: 페트, 3: 비닐)
    class_names: tuple[str, ...] = tuple(meta.name_en for meta in MODEL_CLASS_MAP)


@dataclass(frozen=True)
class InspectionTaskConfig:
    """개별 2단계 세부 검사 작업(Task) 선언적 명세."""

    task_id: str  # 고유 작업 ID (예: "pet_label", "pet_contamination")
    target_category: Category  # 대상 품목 열거형 (예: Category.PET)
    engine_path: Path  # TensorRT 엔진 경로
    input_shape: tuple[int, int] = (224, 224)  # (H, W)
    threshold: float = 0.50  # 판정 기준 확률 (0.0 ~ 1.0)
    is_positive_fail: bool = True  # True: score >= threshold 일 때 불량(Fail), False: score < threshold 일 때 불량
    fail_reason: str = InspectionReason.UNKNOWN.value  # 불량 판정 시 리포트 사유
    enabled: bool = True  # 활성화 플래그


@dataclass(frozen=True)
class InspectionPipelineConfig:
    """2단계 세부 검사 파이프라인 전역 설정."""

    enabled: bool = True
    min_crop_size: int = 40  # 너무 작은 노이즈 BBox 무시 기준 (px)
    tasks: tuple[InspectionTaskConfig, ...] = field(
        default_factory=lambda: (
            # 1. PET 라벨 부착 여부 검사 (MobileNetV3 기반)
            InspectionTaskConfig(
                task_id="pet_label",
                target_category=Category.PET,
                engine_path=JETSON_ROOT_DIR / "models" / "pet_label_mobilenetv3.engine",
                input_shape=(224, 224),
                threshold=0.50,
                is_positive_fail=True,
                fail_reason=InspectionReason.LABEL_ATTACHED.value,
                enabled=True,
            ),
            # 2. PET 오염/내용물 잔여 검사 (EfficientNet 기반)
            InspectionTaskConfig(
                task_id="pet_content",
                target_category=Category.PET,
                engine_path=JETSON_ROOT_DIR
                / "models"
                / "pet_content_efficientnet.engine",
                input_shape=(224, 224),
                threshold=0.50,
                is_positive_fail=True,
                fail_reason=InspectionReason.CONTAMINATED.value,
                enabled=True,
            ),
        )
    )


@dataclass(frozen=True)
class NetworkConfig:
    """관제 PC 연동 TCP 영상 스트리밍 소켓 설정."""

    host: str = "0.0.0.0"
    port: int = 9000
    jpeg_quality: int = 75  # 전송 대역폭 절감과 화질 간 최적 균형값
    socket_timeout: float = 1.0


@dataclass(frozen=True)
class SerialConfig:
    """STM32 MCU UART 시리얼 통신 설정."""

    port: str = "/dev/ttyMCU"
    baudrate: int = 115200
    timeout: float = 0.1
    enabled: bool = True


@dataclass(frozen=True)
class DoorConfig:
    """수거함 도어 FSM 디바운스 및 타임아웃 파라미터."""

    auto_open: bool = True  # True: AI 감지 안정 유지 시 자동 개방 활성화
    stable_sec: float = 1.0  # 오검출 방지용 안정 인식 최소 유지 시간 (초)
    stable_frames: int = 15  # 안정 판정을 위한 최소 연속 유효 프레임 수
    min_hold_sec: float = 3.0  # 투입 안전을 위한 최소 개방 유지 시간 (초)
    lost_tolerance: int = 30  # 투입 중 물체 가림 및 모션 블러 발생 시 조기 폐쇄(손끼임) 방지 유예 프레임 수 (약 1.0초)
    miss_tolerance: int = 3  # 순간적인 검출 누락(조명/블러) 발생 시 연속 상태 보존을 위한 드롭아웃 허용 한도
    max_open_sec: float = 10.0  # 모터 과열 보호 및 방치 방지용 최대 개방 제한 시간 (초)


@dataclass(frozen=True)
class AppConfig:
    """전체 서브시스템 통합 설정 컨테이너."""

    cam: CameraConfig = field(default_factory=CameraConfig)
    model: ModelConfig = field(default_factory=ModelConfig)
    inspection: InspectionPipelineConfig = field(
        default_factory=InspectionPipelineConfig
    )
    net: NetworkConfig = field(default_factory=NetworkConfig)
    serial: SerialConfig = field(default_factory=SerialConfig)
    door: DoorConfig = field(default_factory=DoorConfig)


# 전역 설정 싱글톤 인스턴스
cfg = AppConfig()
