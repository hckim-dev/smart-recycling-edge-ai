/**
 * 키오스크 사용자 투입 세션 수명주기, 비전 추론 디바운스 및 보상 누적 제어기 헤더.
 */
#pragma once
#ifndef RECYCLE_SESSION_CONTROLLER_H
#define RECYCLE_SESSION_CONTROLLER_H

#include "app_config.h"
#include <QObject>
#include <QRect>
#include <QString>

class RecycleSessionController : public QObject {
    Q_OBJECT

public:
    explicit RecycleSessionController(QObject* parent = nullptr);
    ~RecycleSessionController() override = default;

    // 투입 세션 개시 및 카운터/상태 초기화
    void startSession(bool isMember, const QString& userName = QString(), int userId = -1);
    // 정상 투입 완료 처리 (누적 통계 유지)
    void finishSession();
    // 세션 강제 취소 및 누적 통계 폐기
    void cancelSession();
    bool isSessionActive() const { return m_isActive; }

    // 프레임 단위 비전 메타데이터 투입 및 도어 연동 판정 처리
    void processFrameMetadata(const FrameMetadata& meta);

    const SessionSummary& sessionSummary() const { return m_summary; }
    int currentUserId() const { return m_userId; }

signals:
    void sigSessionUpdated(const SessionSummary& summary);
    void sigGuideBannerRequested(int bannerType, const QString& customText);
    void sigDetectionBoxUpdated(const QString& className, double confidence, int debounceCount, const QRect& box, bool isPassed = true, const QString& inspectNote = QString());
    void sigItemCounted(RecycleCategory category, const SessionSummary& summary);

private:
    bool m_isActive { false };
    SessionSummary m_summary;
    int m_userId { -1 };

    int m_consecutiveDetections { 0 };
    RecycleCategory m_lastCategory { RecycleCategory::UNKNOWN };
    int m_missCount { 0 }; // 순간적 검출 누락 시 Bounding Box 플리커링 방지용 프레임 유예 카운터
    bool m_itemCounted { false };
    bool m_doorWasOpen { false }; // MCU 하드웨어 도어 이전 프레임 상태 (Rising Edge 검출용)
};

#endif // RECYCLE_SESSION_CONTROLLER_H