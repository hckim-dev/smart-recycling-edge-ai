/**
 * 연속 인식 디바운스 및 하드웨어 투입 연동 비전 카운팅 FSM 구현부.
 */
#include "recycle_session_controller.h"
#include "theme_constants.h"
#include <QDebug>

RecycleSessionController::RecycleSessionController(QObject* parent)
    : QObject(parent)
{
}

void RecycleSessionController::startSession(bool isMember, const QString& userName, int userId)
{
    m_isActive = true;
    m_summary.reset();
    m_summary.isMember = isMember;
    m_summary.userName = userName;
    m_userId = userId;

    m_consecutiveDetections = 0;
    m_lastCategory = RecycleCategory::UNKNOWN;
    m_itemCounted = false;
    m_doorWasOpen = false;

    emit sigSessionUpdated(m_summary);
    emit sigGuideBannerRequested(static_cast<int>(UITheme::Recycle::BannerType::READY), QString());
    emit sigDetectionBoxUpdated("", 0.0, 0, QRect());
}

void RecycleSessionController::finishSession()
{
    m_isActive = false;
    m_consecutiveDetections = 0;
    m_itemCounted = false;
    m_doorWasOpen = false;
    m_userId = -1;
}

void RecycleSessionController::cancelSession()
{
    m_isActive = false;
    m_summary.reset();
    m_consecutiveDetections = 0;
    m_itemCounted = false;
    m_doorWasOpen = false;
    m_userId = -1;
}

void RecycleSessionController::processFrameMetadata(const FrameMetadata& meta)
{
    if (!m_isActive) {
        return;
    }

    const bool hasDetection = !meta.detections.isEmpty();
    const Detection top = hasDetection ? meta.detections.first() : Detection();

    // 1. 하드웨어 도어 개폐 상태 엣지(Rising Edge) 추적
    const bool doorJustOpened = (!m_doorWasOpen && meta.door.isOpen);
    const bool doorJustClosed = (m_doorWasOpen && !meta.door.isOpen);
    m_doorWasOpen = meta.door.isOpen;

    // [핵심] MCU로부터 도어가 '실제로 열렸다'는 물리 센서 신호 수신 시 -> 비로소 투입 카운트(+1) 및 포인트 확정!
    if (doorJustOpened) {
        RecycleCategory targetCat = Config::parseCategory(meta.door.item);
        if (targetCat == RecycleCategory::UNKNOWN) {
            targetCat = m_lastCategory;
        }

        if (targetCat != RecycleCategory::UNKNOWN) {
            m_summary.addItem(targetCat, 1);
            emit sigSessionUpdated(m_summary);
            emit sigItemCounted(targetCat, m_summary);

            qDebug() << "[Session] 하드웨어 도어 개방 확인 -> 투입 품목 카운트 가산:"
                     << Config::getCategoryNameKo(targetCat)
                     << "(누적 포인트:" << m_summary.totalPoints << "P)";
        }
    }

    // 2. 도어가 열려 있는 동안: 투입 진행 중인 물체의 중복 카운트 방지 및 개방 안내 배너 유지 (Interlock)
    if (meta.door.isOpen) {
        m_consecutiveDetections = 0;
        emit sigDetectionBoxUpdated(top.className, top.confidence, 0, top.box);

        const RecycleCategory doorCat = Config::parseCategory(meta.door.item);
        QString doorItemName;
        if (doorCat != RecycleCategory::UNKNOWN) {
            doorItemName = Config::getCategoryNameKo(doorCat);
        } else if (m_lastCategory != RecycleCategory::UNKNOWN) {
            doorItemName = Config::getCategoryNameKo(m_lastCategory);
        } else {
            doorItemName = (meta.door.item.isEmpty() || meta.door.item == "ALL") ? "투입구" : meta.door.item;
        }

        emit sigGuideBannerRequested(static_cast<int>(UITheme::Recycle::BannerType::DOOR_OPEN), doorItemName);
        return;
    }

    // 3. 도어가 방금 닫혔을 때: 다음 물체 인식을 위해 상태 리셋
    if (doorJustClosed) {
        m_consecutiveDetections = 0;
        m_lastCategory = RecycleCategory::UNKNOWN;
        m_itemCounted = false;
    }

    // 4. 검출 객체 부재 또는 미분류: 대기 상태 복귀
    if (!hasDetection || top.category == RecycleCategory::UNKNOWN) {
        m_consecutiveDetections = 0;
        m_lastCategory = RecycleCategory::UNKNOWN;

        emit sigDetectionBoxUpdated("", 0.0, 0, QRect());
        emit sigGuideBannerRequested(static_cast<int>(UITheme::Recycle::BannerType::READY), QString());
        return;
    }

    // 5. 2단계 세부 품질 검사 (라벨 부착, 오염 등) 불합격 여부 판정
    if (top.inspection.hasInspection && !top.inspection.passed) {
        m_consecutiveDetections = 0;
        m_lastCategory = RecycleCategory::UNKNOWN;

        const bool hasLabel = top.inspection.reasons.contains(QLatin1String(Config::JetsonProtocol::REASON_LABEL_ATTACHED));
        const bool hasContam = top.inspection.reasons.contains(QLatin1String(Config::JetsonProtocol::REASON_CONTAMINATED));

        QString warnBanner;
        QString inspectNote;

        if (hasLabel && hasContam) {
            warnBanner = UITheme::Recycle::Text::GUIDE_LABEL_CONTAM_WARN;
            inspectNote = UITheme::Recycle::Text::NOTE_LABEL_AND_CONTAM;
        } else if (hasLabel) {
            warnBanner = UITheme::Recycle::Text::GUIDE_LABEL_WARN;
            inspectNote = UITheme::Recycle::Text::NOTE_LABEL_ATTACHED;
        } else if (hasContam) {
            warnBanner = UITheme::Recycle::Text::GUIDE_CONTAM_WARN;
            inspectNote = UITheme::Recycle::Text::NOTE_CONTAMINATED;
        } else {
            warnBanner = UITheme::Recycle::Text::GUIDE_INSPECT_FAIL;
            inspectNote = UITheme::Recycle::Text::NOTE_GENERAL_FAIL;
        }

        emit sigDetectionBoxUpdated(top.className, top.confidence, 0, top.box, false, inspectNote);
        emit sigGuideBannerRequested(static_cast<int>(UITheme::Recycle::BannerType::WARNING), warnBanner);
        return;
    }

    // 6. 정상 검출 품목: 변경 감지 시 이전 디바운스 카운트 초기화
    if (top.category != m_lastCategory) {
        m_lastCategory = top.category;
        m_consecutiveDetections = 0;
    }

    m_consecutiveDetections++;
    emit sigDetectionBoxUpdated(top.className, top.confidence, m_consecutiveDetections, top.box, true, QString());

    // 7. 비전 인식 단계 배너 안내: 1.5초(45프레임) 유지 시 확인 완료 배너, 진행 중일 시 인식 중 배너
    if (m_consecutiveDetections >= Config::STABLE_FRAME_THRESHOLD) {
        emit sigGuideBannerRequested(static_cast<int>(UITheme::Recycle::BannerType::CONFIRMED),
            Config::getCategoryNameKo(top.category));
    } else {
        emit sigGuideBannerRequested(static_cast<int>(UITheme::Recycle::BannerType::ANALYZING),
            Config::getCategoryNameKo(top.category));
    }
}