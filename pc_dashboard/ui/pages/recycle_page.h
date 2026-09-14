/**
 * 실시간 카메라 스트림 렌더링, BBox 오버레이 및 배출 세션 대시보드 UI 헤더.
 */
#pragma once
#ifndef RECYCLE_PAGE_H
#define RECYCLE_PAGE_H

#include "app_config.h"
#include "theme_constants.h"
#include <QColor>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QWidget>

namespace Ui {
class RecyclePage;
}

class EcoTreeController;

class RecyclePage : public QWidget {
    Q_OBJECT

public:
    explicit RecyclePage(QWidget* parent = nullptr);
    ~RecyclePage() override;

    // 세션 사용자 모드(회원/비회원)에 따른 환영 문구 및 리워드 영역 초기화
    void startSession(bool isMember, const QString& userName = QString());
    // 비전 오버레이, 투입 수량, 포인트 및 에코 트리 위젯 수치 리셋
    void resetState();

    // 수신 영상 프레임 해상도 맞춤 스케일링 및 BBox/품목 배지 오버레이 렌더링
    void updateFrame(const QPixmap& pixmap);
    // 검출 객체 메타데이터(라벨/신뢰도/좌표/품질검사) 기반 오버레이 드로잉 속성 갱신
    void updateDetectionState(const QString& className, double confidence, int debounceCount, const QRect& box = QRect(), bool isPassed = true, const QString& inspectNote = QString());
    // 품목별 누적 투입 수량, 탄소 절감량, 합산 포인트 및 에코 트리 성장 단계 동기화
    void updateSessionSummary(const SessionSummary& summary);
    // 상태 머신 전이별 상단 안내 배너 문구 및 컬러 테마 적용
    void setGuideBanner(UITheme::Recycle::BannerType type, const QString& customText = QString());

signals:
    void sigFinishSessionRequested();
    void sigCancelSessionRequested();

private slots:
    void on_btnFinishSession_clicked();
    void on_btnCancelSession_clicked();

private:
    // Qt 스타일시트 동적 프로퍼티 할당 및 unpolish/polish 강제 갱신
    void applyDynamicProperty(QWidget* widget, const char* propName, const QVariant& value);

private:
    Ui::RecyclePage* ui;
    EcoTreeController* m_ecoTree { nullptr };
    QRect m_detectionBox { };
    QColor m_boxColor { UITheme::Recycle::DEFAULT_BOX_COLOR };
    QString m_boxLabel { };
    bool m_isMember { false };
    QString m_userName { };

    // 매 프레임 폰트 텍스트 바운딩 박스 연산 오버헤드를 막기 위한 메트릭스 캐싱
    QFont m_badgeFont;
    QFontMetrics m_badgeFontMetrics;
};

#endif // RECYCLE_PAGE_H