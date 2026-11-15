#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QWidget>

#include "backend/statusseverity.h"

class QTimer;

namespace traceview {

// Status messages as small translucent cards floating over the bottom-right
// corner of the dashboard, instead of text in the status row -- whose label
// grew with each long message and pushed the row's other widgets around.
//
// An overlay child (not a layout item), transparent to the mouse, so the
// canvas under it stays clickable; the cards themselves are see-through
// enough that the dashboard still shows behind them. Up to kMaxCards are
// stacked, newest at the bottom; each fades in, stays for its timeout and
// fades out. A message identical to the newest card refreshes that card (with
// a repeat count) instead of stacking a copy.
//
// Everything is painted here, theme colours read at paint time, so a theme
// switch needs no extra wiring. MainWindow calls setArea() whenever the
// dashboard's area inside the parent moves or resizes.
class NotificationToasts : public QWidget {
    Q_OBJECT

public:
    explicit NotificationToasts(QWidget* parent);

    // `timeoutMs` <= 0 means the default (a status message that used to stay
    // until replaced would otherwise never leave). `source` (a device name,
    // or empty) is shown ahead of the text.
    void post(const QString& text, int timeoutMs, StatusSeverity severity,
              const QString& source = QString());
    // The parent-relative rectangle the cards may occupy; they stack up from
    // its bottom-right corner.
    void setArea(const QRect& area);

    int cardCount() const {
        return int(m_cards.size());
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Card {
        QString text;
        QString source;
        StatusSeverity severity = StatusSeverity::Info;
        int repeats = 1;
        qint64 shownAtMs = 0;
        qint64 expiresAtMs = 0;
    };

    static constexpr int kMaxCards = 4;
    static constexpr int kDefaultTimeoutMs = 6000;
    static constexpr int kMinTimeoutMs = 2500;
    static constexpr int kFadeMs = 250;

    void tick();
    void relayout();
    int cardWidth() const;
    int cardHeight(const Card& card, int width) const;
    QString cardText(const Card& card) const;
    qreal opacityOf(const Card& card, qint64 nowMs) const;

    QList<Card> m_cards;  // oldest first
    QRect m_area;
    QElapsedTimer m_clock;
    QTimer* m_timer = nullptr;
};

}  // namespace traceview
