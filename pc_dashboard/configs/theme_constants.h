/**
 * 키오스크 UI 테마 색상, 동적 배너 스타일시트 및 정적 텍스트 리소스 정의 헤더.
 */
#pragma once
#ifndef THEME_CONSTANTS_H
#define THEME_CONSTANTS_H

#include "app_config.h"
#include <QColor>
#include <QSize>
#include <QString>

namespace UITheme {

constexpr char PROP_MEMBER[] = "member";
constexpr char PROP_BANNER_STATUS[] = "bannerStatus";
inline constexpr const char* PROP_ACTIVE = "active";
inline const QString FONT_FAMILY = "Pretendard";

// 품목별 비전 BBox 및 UI 시각 피드백 테마 컬러 반환
inline QColor getCategoryColor(RecycleCategory cat)
{
    switch (cat) {
    case RecycleCategory::PAPER:
        return QColor("#F59E0B"); // 호박색 (Paper)
    case RecycleCategory::CAN:
        return QColor("#10B981"); // 에메랄드 (Can)
    case RecycleCategory::PET:
        return QColor("#38BDF8"); // 하늘색 (PET)
    case RecycleCategory::VINYL:
        return QColor("#A855F7"); // 보라색 (Vinyl)
    default:
        return QColor("#10B981");
    }
}

namespace Header {
    inline const QString STATUS_ONLINE = "● AI VISION ONLINE";
    inline const QString STATUS_OFFLINE = "○ AI VISION OFFLINE";
    inline const QString TELEMETRY_FMT = "FPS: %1 | Infer: %2ms";
}

namespace Idle {
    inline constexpr int QR_DISPLAY_SIZE = 280;
    inline constexpr int QR_QUIET_ZONE_MODULES = 4;
    inline constexpr qreal QR_CORNER_RADIUS = 24.0;
}

namespace Recycle {

    inline constexpr int BADGE_FONT_SIZE = 22;
    inline constexpr int BOX_PEN_WIDTH = 4;
    inline constexpr int BADGE_PAD_X = 14;
    inline constexpr int BADGE_PAD_Y = 8;
    inline const QString DEFAULT_BOX_COLOR = "#10B981";
    inline const QColor COLOR_INSPECTION_FAIL { "#EF4444" };

    constexpr char GREETING_MEMBER[] = "font-size: 30px; font-weight: 900; color: #10B981; background: transparent;";
    constexpr char GREETING_GUEST[] = "font-size: 30px; font-weight: 900; color: #38BDF8; background: transparent;";
    constexpr char POINTS_MEMBER[] = "font-size: 42px; font-weight: 900; color: #38BDF8; background: transparent;";
    constexpr char POINTS_GUEST[] = "font-size: 32px; font-weight: 800; color: #F59E0B; background: transparent;";
    constexpr char BANNER_TEMPLATE[] = "background-color: %1; border: 2px solid %2; border-radius: 16px; color: %3; font-size: 22px; font-weight: 800; padding: 10px;";

    // 비전 인식 및 하드웨어 연동 상태에 따른 동적 안내 배너 타입
    enum class BannerType {
        READY,
        ANALYZING,
        CONFIRMED,
        WARNING,
        DOOR_OPEN
    };

    struct BannerThemeDef {
        const char* textColor;
        const char* borderColor;
        const char* bgColor;
    };

    // 배너 상태별 스타일 테마 반환
    inline BannerThemeDef getBannerTheme(BannerType type)
    {
        switch (type) {
        case BannerType::READY:
            return { "#38BDF8", "#0284C7", "#0D1927" };
        case BannerType::ANALYZING:
            return { "#FBBF24", "#D97706", "rgba(245, 158, 11, 0.15)" };
        case BannerType::CONFIRMED:
            return { "#10B981", "#10B981", "rgba(16, 185, 129, 0.15)" };
        case BannerType::WARNING:
            return { "#FCA5A5", "#EF4444", "rgba(239, 68, 68, 0.2)" };
        case BannerType::DOOR_OPEN:
            return { "#34D399", "#059669", "rgba(5, 150, 105, 0.2)" };
        }
        return { "#FFFFFF", "#334155", "transparent" };
    }

    namespace Text {
        inline const QString DEFAULT_MEMBER_NAME = "회원";
        constexpr char GREETING_MEMBER_FMT[] = "👤 %1 님 환영합니다";
        constexpr char GREETING_GUEST[] = "👤 비회원 간편 투입";
        constexpr char SESSION_MODE_MEMBER[] = "투입 완료 후 포인트가 자동으로 적립됩니다.";
        constexpr char SESSION_MODE_GUEST[] = "비회원 모드로 동작 중입니다 (포인트 미적립).";
        constexpr char REWARD_HEADER_MEMBER[] = "💰 현재 세션 획득 리워드";
        constexpr char REWARD_HEADER_GUEST[] = "⚠️ 비회원 미적립 혜택 안내";
        constexpr char VIDEO_INITIALIZING[] = "Jetson AI 비전 스트림 연결 대기 중...";
        constexpr char GUIDE_READY[] = "🎯 카메라 중앙 영역에 재활용품을 놓아주세요";
        constexpr char GUIDE_ANALYZING_FMT[] = "⏳ %1 인식 중... 고정해 주세요";
        constexpr char GUIDE_CONFIRMED_FMT[] = "✅ %1 확인 완료! 투입구가 자동으로 열립니다";
        constexpr char GUIDE_DOOR_OPEN_FMT[] = "🚪 %1 투입구 개방 중 | 물품을 투입해 주세요";
        constexpr char GUIDE_GENERAL_WARN[] = "⚠️ 미인식 품목 감지";
        constexpr char GUIDE_LABEL_WARN[] = "⚠️ 비닐 라벨을 떼어낸 후 다시 올려주세요";
        constexpr char GUIDE_CONTAM_WARN[] = "⚠️ 내부 이물질을 세척한 후 다시 올려주세요";
        constexpr char GUIDE_LABEL_CONTAM_WARN[] = "⚠️ 라벨을 제거하고 내부를 세척해 주세요";
        constexpr char GUIDE_INSPECT_FAIL[] = "⚠️ 품질 검사 미통과: 라벨이나 이물질을 확인해 주세요";
        constexpr char NOTE_LABEL_ATTACHED[] = "라벨 미제거";
        constexpr char NOTE_CONTAMINATED[] = "오염 감지";
        constexpr char NOTE_LABEL_AND_CONTAM[] = "라벨/오염";
        constexpr char NOTE_GENERAL_FAIL[] = "품질 불합격";
        constexpr char BADGE_INSPECT_FMT[] = "%1 [%2]";
        constexpr char BADGE_FAIL_FMT[] = "%1 [불합격]";
        constexpr char POINTS_MEMBER_FMT[] = "+ %1 P";
        constexpr char POINTS_GUEST_FMT[] = "%1 P (미적립)";
        constexpr char CARBON_SAVED_FMT[] = "🌱 절감 탄소량: %1g CO2";
    }
}

namespace EcoTree {
    inline const QString RESOURCE_PATH = ":/images/tree_grow.gif";
    inline const QSize DISPLAY_SIZE { 300, 300 };
    inline constexpr int MOVIE_SPEED = 200;
    inline constexpr int DEFAULT_FRAME_COUNT = 120;

    namespace Text {
        inline const QString STATUS_BASE = "재활용품을 넣어 나무에 잎을 틔워보세요!";
        inline const QString STATUS_STAGE_1_FMT = "🌱 연둣빛 새싹 잎이 자라나요! (총 %1개)";
        inline const QString STATUS_STAGE_2_FMT = "🌿 푸른 잎으로 무성해지는 중! (총 %1개)";
        inline const QString STATUS_STAGE_3_FMT = "🌳 지구를 살리는 울창한 나무 완성! (총 %1개)";
    }
}

namespace Result {
    inline const QString CONFETTI_RESOURCE_PATH = ":/images/confetti.gif";
    inline const QSize CONFETTI_DISPLAY_SIZE { 640, 300 };
    inline constexpr int CONFETTI_SPEED = 130;
    inline constexpr int COUNTDOWN_INTERVAL_MS = 1000;
    inline constexpr int ANIM_POINTS_DURATION_MS = 900;
    inline constexpr int ANIM_CARBON_DURATION_MS = 1100;

    namespace Text {
        inline const QString COUNT_UNIT_FMT = "%1 개";
        inline const QString POINTS_PLUS_FMT = "+ %1 P";
        inline const QString POINTS_ZERO = "0 P";
        inline const QString EMPTY_DASH = "-";
        inline const QString NOTICE_MEMBER_FMT = "✅ %1 님의 계정으로 포인트가 안전하게 적립되었습니다.";
        inline const QString NOTICE_GUEST = "ℹ️ 비회원 이용 세션 (모바일 앱 가입 후 스캔 시 포인트가 적립됩니다)";
        inline const QString POINTS_GUEST = "0 P (비회원)";
        inline const QString CARBON_FMT = "%1 g CO₂";
        inline const QString COUNTDOWN_BTN_FMT = "확인 (%1초 후 처음으로 이동)";
    }
}

} // namespace UITheme

#endif // THEME_CONSTANTS_H