/**
 * 스마트 재활용 키오스크 전역 상수, 네트워크 프로토콜 및 데이터 모델 정의 헤더.
 */
#pragma once
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>
#include <QVector>

// 키오스크 UI 및 시스템 생명주기 관리 FSM 상태
enum class KioskState {
    IDLE,
    AUTH_WAIT,
    RECYCLING,
    RESULT,
    MAINTENANCE,
    ERROR_STATE
};

// 재활용 대상 4종 품목 식별 열거형
enum class RecycleCategory {
    UNKNOWN = -1,
    PAPER = 0,
    CAN = 1,
    PET = 2,
    VINYL = 3
};

namespace Config {

constexpr int CATEGORY_COUNT = 4;

// Jetson 엣지 디바이스 TCP 소켓 스트리밍 및 버퍼링 제약
constexpr char DEFAULT_JETSON_IP[] = "10.10.15.48";
constexpr quint16 JETSON_PORT = 9000;
constexpr int SOCKET_BUFFER_RESERVE = 512 * 1024;
constexpr int AUTO_RECONNECT_INTERVAL_MS = 2000;
constexpr quint32 HEADER_SIZE = 8;
constexpr quint32 MAX_IMAGE_SIZE = 10 * 1024 * 1024; // 프레임 버퍼 오버플로우 및 OOM 방지용 상한 (10MB)
constexpr quint32 MAX_JSON_SIZE = 64 * 1024;
constexpr int MAX_BUFFER_CAPACITY = 20 * 1024 * 1024; // 패킷 손상 시 무한 누적 방지를 위한 버퍼 강제 플러시 상한 (20MB)

// FastAPI 중앙 백엔드 REST/WebSocket 접속 파라미터
constexpr char DEFAULT_BACKEND_HOST[] = "10.10.15.8";
constexpr quint16 DEFAULT_BACKEND_PORT = 8000;
constexpr int DEFAULT_BIN_ID = 1;

// 비전 객체 인식 확정 판정 임계치
constexpr int STABLE_FRAME_THRESHOLD = 25; // 동일 클래스 안정 인식 시 바운스 필터링 통과 기준 프레임 수
constexpr double MIN_CONFIDENCE_THRESHOLD = 0.80; // 객체 검출 최소 신뢰도 하한선

// 하드웨어 수거함 임계치 및 세션 타임아웃
constexpr int MAX_BIN_CAPACITY = 100;
constexpr int BIN_FULL_WARNING_PERCENT = 80;
constexpr int RESULT_DISPLAY_TIMEOUT_SEC = 10;
constexpr int RECYCLE_SESSION_TIMEOUT_SEC = 60;

namespace Demo {
    inline const QString MEMBER_USER_ID = "회원";
}

namespace Backend {
    inline const QString WS_URL_FMT = "ws://%1:%2/ws/kiosk/%3/kiosk";
    inline const QString API_SUBMIT_PATH = "http://%1:%2/api/recycle/submit";
    inline const QString API_CANCEL_PATH = "http://%1:%2/api/kiosk/cancel";

    namespace Event {
        inline const QString USER_AUTHENTICATED = "USER_AUTHENTICATED";
        inline const QString EMERGENCY_STOP = "EMERGENCY_STOP";
        inline const QString SESSION_CANCELLED = "SESSION_CANCELLED";
    }

    namespace Key {
        inline const QString EVENT = "event";
        inline const QString USER_ID = "user_id";
        inline const QString NAME = "name";
        inline const QString PHONE = "phone";
        inline const QString POINTS = "points";
        inline const QString BIN_ID = "bin_id";
        inline const QString PAPER_COUNT = "paper_count";
        inline const QString CAN_COUNT = "can_count";
        inline const QString PET_COUNT = "pet_count";
        inline const QString VINYL_COUNT = "vinyl_count";
        inline const QString CARBON_SAVED = "carbon_saved_g";
        inline const QString EARNED_PTS = "earned_points";
        inline const QString LOG_ID = "log_id";
        inline const QString TOTAL_POINTS = "total_points";
        inline const QString REASON = "reason";
    }
}

namespace Auth {
    inline const QString DEEPLINK_SCHEME = "smartrecycle://kiosk/auth";
    inline const QString DEEPLINK_PAYLOAD_FMT = "%1?bin_id=%2";
}

namespace EcoTree {
    inline constexpr int THRESHOLD_STAGE_1 = 2;
    inline constexpr int THRESHOLD_STAGE_2 = 4;
    inline constexpr double FRAME_RATIO_BASE = 0.30;
    inline constexpr double FRAME_RATIO_STAGE_1 = 0.55;
    inline constexpr double FRAME_RATIO_STAGE_2 = 0.80;
}

// 품목별 리워드 포인트 및 단위 탄소 저감 계수 메타데이터
struct ItemMeta {
    RecycleCategory category;
    const char* nameKo;
    const char* nameEn;
    int unitPoint;
    double unitCarbonG;
};

inline constexpr ItemMeta ITEM_METAS[CATEGORY_COUNT] = {
    { RecycleCategory::PAPER, "종이", "PAPER", 30, 8.5 },
    { RecycleCategory::CAN, "캔", "CAN", 50, 25.0 },
    { RecycleCategory::PET, "페트", "PET", 50, 15.2 },
    { RecycleCategory::VINYL, "비닐", "VINYL", 10, 5.0 }
};

// 품목별 지급 단위 포인트 반환
inline int getPoint(RecycleCategory cat)
{
    int idx = static_cast<int>(cat);
    return (idx >= 0 && idx < CATEGORY_COUNT) ? ITEM_METAS[idx].unitPoint : 0;
}

// 품목별 단위 탄소 저감량(g CO2) 반환
inline double getCarbonG(RecycleCategory cat)
{
    int idx = static_cast<int>(cat);
    return (idx >= 0 && idx < CATEGORY_COUNT) ? ITEM_METAS[idx].unitCarbonG : 0.0;
}

// 품목 영문 명칭 반환
inline const char* getCategoryNameEn(RecycleCategory cat)
{
    int idx = static_cast<int>(cat);
    return (idx >= 0 && idx < CATEGORY_COUNT) ? ITEM_METAS[idx].nameEn : "UNKNOWN";
}

// 품목 국문 표준 명칭 반환
inline const char* getCategoryNameKo(RecycleCategory cat)
{
    int idx = static_cast<int>(cat);
    return (idx >= 0 && idx < CATEGORY_COUNT) ? ITEM_METAS[idx].nameKo : "미확인";
}

// YOLO 모델 출력 인덱스(0: 종이, 1: 캔, 2: 페트, 3: 비닐) 1:1 직결 메타데이터 구조체
struct ModelClassMeta {
    int classId;
    RecycleCategory category;
    const char* nameEn;
    const char* nameKo;
};

inline constexpr ModelClassMeta MODEL_CLASS_METAS[CATEGORY_COUNT] = {
    { 0, RecycleCategory::PAPER, "PAPER", "종이" },
    { 1, RecycleCategory::CAN,   "CAN",   "캔" },
    { 2, RecycleCategory::PET,   "PET",   "페트" },
    { 3, RecycleCategory::VINYL, "VINYL", "비닐" }
};

// 모델 인덱스(0~3) -> 도메인 카테고리 1:1 직접 변환
inline RecycleCategory modelIndexToCategory(int classId)
{
    if (classId >= 0 && classId < CATEGORY_COUNT) {
        return MODEL_CLASS_METAS[classId].category;
    }
    return RecycleCategory::UNKNOWN;
}

// 수신된 품목 문자열("PAPER", "CAN" 등)을 메타데이터 테이블과 비교 매핑
inline RecycleCategory parseCategory(const QString& name)
{
    const QString upper = name.toUpper().trimmed();
    if (upper.isEmpty()) {
        return RecycleCategory::UNKNOWN;
    }

    for (const auto& meta : MODEL_CLASS_METAS) {
        if (upper == meta.nameEn || upper == meta.nameKo) {
            return meta.category;
        }
    }
    return RecycleCategory::UNKNOWN;
}

} // namespace Config

// Jetson 엣지 텔레메트리 TCP 패킷 내 JSON 키 규격
namespace Config::JetsonProtocol {
inline constexpr char KEY_TIMESTAMP[] = "timestamp";
inline constexpr char KEY_FPS[] = "fps";
inline constexpr char KEY_INFER_MS[] = "infer_ms";
inline constexpr char KEY_DETECTIONS[] = "detections";
inline constexpr char KEY_BIN_LEVELS[] = "bin_levels";
inline constexpr char KEY_DOOR[] = "door";

inline constexpr char KEY_ITEM[] = "item";
inline constexpr char KEY_STATE[] = "state";
inline constexpr char STATE_OPEN[] = "OPEN";
inline constexpr char STATE_CLOSED[] = "CLOSED";

inline constexpr char KEY_PAPER[] = "paper";
inline constexpr char KEY_CAN[] = "can";
inline constexpr char KEY_PET[] = "pet";
inline constexpr char KEY_VINYL[] = "vinyl";

inline constexpr char KEY_CLASS_ID[] = "class_id";
inline constexpr char KEY_CLASS_NAME[] = "class_name";
inline constexpr char KEY_CONFIDENCE[] = "confidence";
inline constexpr char KEY_BOX[] = "box";

inline constexpr char KEY_INSPECTION[] = "inspection";
inline constexpr char KEY_PASSED[] = "passed";
inline constexpr char KEY_REASONS[] = "reasons";
inline constexpr char KEY_DETAILS[] = "details";

// 2단계 세부 품질 검사 사유 코드 (Edge AI Jetson 추론 결과와 규격 일치)
inline constexpr char REASON_LABEL_ATTACHED[] = "LABEL_ATTACHED";
inline constexpr char REASON_CONTAMINATED[] = "CONTAMINATED";
}

// 물리 수거함 구역별 센서 적재율(%) 데이터 모델
struct BinStatus {
    int paper { 0 };
    int can { 0 };
    int pet { 0 };
    int vinyl { 0 };

    bool operator==(const BinStatus& o) const
    {
        return paper == o.paper && can == o.can && pet == o.pet && vinyl == o.vinyl;
    }

    bool operator!=(const BinStatus& o) const
    {
        return !(*this == o);
    }

    static BinStatus fromJson(const QJsonObject& obj)
    {
        using namespace Config::JetsonProtocol;
        BinStatus status;
        status.paper = obj.value(KEY_PAPER).toInt();
        status.can = obj.value(KEY_CAN).toInt();
        status.pet = obj.value(KEY_PET).toInt();
        status.vinyl = obj.value(KEY_VINYL).toInt();
        return status;
    }
};

// MCU 제어 하드웨어 투입구 도어 개폐 상태 모델
struct HardwareDoorStatus {
    QString item { "ALL" };
    bool isOpen { false };

    static HardwareDoorStatus fromJson(const QJsonObject& obj)
    {
        using namespace Config::JetsonProtocol;
        HardwareDoorStatus status;
        status.item = obj.value(KEY_ITEM).toString("ALL");
        status.isOpen = (obj.value(KEY_STATE).toString().toUpper() == STATE_OPEN);
        return status;
    }
};

// 2단계 세부 품질 검사 (라벨 부착, 오염 등) 결과 모델
struct InspectionResult {
    bool hasInspection { false };
    bool passed { true };
    QStringList reasons { };

    static InspectionResult fromJson(const QJsonObject& obj)
    {
        using namespace Config::JetsonProtocol;
        InspectionResult res;
        res.hasInspection = true;
        res.passed = obj.value(QLatin1String(KEY_PASSED)).toBool(true);
        const QJsonArray rArr = obj.value(QLatin1String(KEY_REASONS)).toArray();
        for (const QJsonValue& v : rArr) {
            res.reasons.append(v.toString());
        }
        return res;
    }
};

// 비전 엔진 검출 BBox 좌표 및 도메인 분류 정보 모델
struct Detection {
    int classId { -1 };
    QString className { };
    double confidence { 0.0 };
    QRect box { };
    RecycleCategory category { RecycleCategory::UNKNOWN };
    InspectionResult inspection { };

    static Detection fromJson(const QJsonObject& obj)
    {
        using namespace Config::JetsonProtocol;
        Detection d;
        d.classId = obj.value(KEY_CLASS_ID).toInt();
        d.className = obj.value(KEY_CLASS_NAME).toString();
        d.confidence = obj.value(KEY_CONFIDENCE).toDouble();

        // 1차: YOLO 모델 출력 인덱스(0: 종이, 1: 캔, 2: 페트, 3: 비닐) 기반 직접 매핑
        d.category = Config::modelIndexToCategory(d.classId);

        // 2차: 인덱스 매핑 실패 시 클래스명 문자열 기반 폴백 매핑
        if (d.category == RecycleCategory::UNKNOWN) {
            d.category = Config::parseCategory(d.className);
        }

        const QJsonArray bArr = obj.value(KEY_BOX).toArray();
        if (bArr.size() >= 4) {
            d.box = QRect(QPoint(bArr[0].toInt(), bArr[1].toInt()),
                QPoint(bArr[2].toInt(), bArr[3].toInt()));
        }

        if (obj.contains(QLatin1String(KEY_INSPECTION)) && obj.value(QLatin1String(KEY_INSPECTION)).isObject()) {
            d.inspection = InspectionResult::fromJson(obj.value(QLatin1String(KEY_INSPECTION)).toObject());
        }
        return d;
    }
};

// 프레임 단위 영상 메타데이터 및 하드웨어 텔레메트리 통합 패킷
struct FrameMetadata {
    double timestamp { 0.0 };
    double fps { 0.0 };
    double inferMs { 0.0 };
    QVector<Detection> detections { };
    BinStatus binLevels { };
    HardwareDoorStatus door { };

    static FrameMetadata fromJson(const QJsonObject& obj)
    {
        using namespace Config::JetsonProtocol;
        FrameMetadata meta;
        meta.timestamp = obj.value(KEY_TIMESTAMP).toDouble();
        meta.fps = obj.value(KEY_FPS).toDouble();
        meta.inferMs = obj.value(KEY_INFER_MS).toDouble();

        const QJsonArray detArray = obj.value(KEY_DETECTIONS).toArray();
        for (const QJsonValue& val : detArray) {
            if (val.isObject()) {
                meta.detections.append(Detection::fromJson(val.toObject()));
            }
        }

        if (obj.contains(KEY_BIN_LEVELS) && obj.value(KEY_BIN_LEVELS).isObject()) {
            meta.binLevels = BinStatus::fromJson(obj.value(KEY_BIN_LEVELS).toObject());
        }

        if (obj.contains(KEY_DOOR) && obj.value(KEY_DOOR).isObject()) {
            meta.door = HardwareDoorStatus::fromJson(obj.value(KEY_DOOR).toObject());
        }

        return meta;
    }
};

// 사용자 단일 투입 세션 누적 배출량 및 보상 계산 집계 모델
struct SessionSummary {
    bool isMember { false };
    QString userName { };
    int paperCount { 0 };
    int canCount { 0 };
    int petCount { 0 };
    int vinylCount { 0 };
    int totalPoints { 0 };
    double totalCarbonG { 0.0 };

    // 품목 카운트 가산 및 리워드 메트릭 갱신
    void addItem(RecycleCategory cat, int count = 1)
    {
        switch (cat) {
        case RecycleCategory::PAPER:
            paperCount += count;
            break;
        case RecycleCategory::CAN:
            canCount += count;
            break;
        case RecycleCategory::PET:
            petCount += count;
            break;
        case RecycleCategory::VINYL:
            vinylCount += count;
            break;
        default:
            break;
        }
        recalculate();
    }

    // 누적 수량 기반 총 포인트 및 탄소 저감량 일괄 재산출
    void recalculate()
    {
        totalPoints = (paperCount * Config::getPoint(RecycleCategory::PAPER)) + (canCount * Config::getPoint(RecycleCategory::CAN)) + (petCount * Config::getPoint(RecycleCategory::PET)) + (vinylCount * Config::getPoint(RecycleCategory::VINYL));

        totalCarbonG = (paperCount * Config::getCarbonG(RecycleCategory::PAPER)) + (canCount * Config::getCarbonG(RecycleCategory::CAN)) + (petCount * Config::getCarbonG(RecycleCategory::PET)) + (vinylCount * Config::getCarbonG(RecycleCategory::VINYL));
    }

    void reset()
    {
        *this = SessionSummary();
    }
};

#endif // APP_CONFIG_H