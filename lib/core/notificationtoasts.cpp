#include "notificationtoasts.h"

#include <algorithm>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>

#include "theme/iconlibrary.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kMargin = 16;          // from the area's edges
constexpr int kSpacing = 8;          // between cards
constexpr int kPadH = 12;            // text inset, left/right (after the stripe)
constexpr int kPadV = 8;             // text inset, top/bottom
constexpr int kStripe = 4;           // severity colour bar on the left
constexpr int kRadius = 8;
constexpr int kMaxWidth = 380;
constexpr int kMaxTextLines = 4;
constexpr int kCardAlpha = 205;      // of 255: the dashboard shows through
constexpr int kTickMs = 33;
constexpr int kClose = 12;           // the X at the top right

QColor severityColor(StatusSeverity severity, const ThemePalette& palette) {
    switch (severity) {
        case StatusSeverity::Success:
            return palette.success;
        case StatusSeverity::Warning:
            return palette.warning;
        case StatusSeverity::Error:
            return palette.danger;
        case StatusSeverity::Info:
            break;
    }
    return palette.accent;
}

}  // namespace

NotificationToasts::NotificationToasts(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    m_clock.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &NotificationToasts::tick);
    hide();
}

void NotificationToasts::post(const QString& text, int timeoutMs, StatusSeverity severity,
                              const QString& source) {
    if (text.trimmed().isEmpty()) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    const int lifetime = qMax(kMinTimeoutMs, timeoutMs > 0 ? timeoutMs : kDefaultTimeoutMs);
    if (!m_cards.isEmpty()) {
        Card& newest = m_cards.last();
        if (newest.text == text && newest.source == source && newest.severity == severity &&
            newest.expiresAtMs > now) {
            ++newest.repeats;
            newest.expiresAtMs = now + lifetime;
            relayout();
            return;
        }
    }
    Card card;
    card.text = text;
    card.source = source;
    card.severity = severity;
    card.shownAtMs = now;
    card.expiresAtMs = now + lifetime;
    m_cards.append(card);
    while (m_cards.size() > kMaxCards) {
        m_cards.removeFirst();
    }
    if (!m_timer->isActive()) {
        m_timer->start();
    }
    relayout();
}

void NotificationToasts::clear() {
    m_cards.clear();
    m_timer->stop();
    relayout();
}

void NotificationToasts::setArea(const QRect& area) {
    m_area = area;
    relayout();
}

void NotificationToasts::tick() {
    const qint64 now = m_clock.elapsed();
    const int before = int(m_cards.size());
    m_cards.erase(std::remove_if(m_cards.begin(), m_cards.end(),
                                 [now](const Card& card) { return card.expiresAtMs <= now; }),
                  m_cards.end());
    if (m_cards.isEmpty()) {
        m_timer->stop();
    }
    if (int(m_cards.size()) != before) {
        relayout();
    } else {
        update();  // fades
    }
}

int NotificationToasts::cardWidth() const {
    const int available = m_area.width() - 2 * kMargin;
    return qMax(120, qMin(kMaxWidth, available));
}

QString NotificationToasts::cardText(const Card& card) const {
    QString text = card.source.isEmpty() ? card.text
                                         : QStringLiteral("%1: %2").arg(card.source, card.text);
    if (card.repeats > 1) {
        text += QStringLiteral("  (×%1)").arg(card.repeats);
    }
    return text;
}

int NotificationToasts::cardHeight(const Card& card, int width) const {
    const QFontMetrics metrics(font());
    const int textWidth = width - kStripe - 2 * kPadH - kClose;
    const QRect bounds = metrics.boundingRect(QRect(0, 0, textWidth, 10000),
                                              Qt::TextWordWrap | Qt::AlignLeft, cardText(card));
    const int lines = qMin(kMaxTextLines, qMax(1, qCeil(qreal(bounds.height()) /
                                                        qreal(metrics.lineSpacing()))));
    return lines * metrics.lineSpacing() + 2 * kPadV;
}

void NotificationToasts::relayout() {
    if (m_cards.isEmpty() || m_area.isEmpty()) {
        hide();
        return;
    }
    const int width = cardWidth();
    int height = 0;
    for (const Card& card : m_cards) {
        height += cardHeight(card, width) + kSpacing;
    }
    height -= kSpacing;
    height = qMin(height, m_area.height() - 2 * kMargin);
    setGeometry(m_area.right() - kMargin - width + 1, m_area.bottom() - kMargin - height + 1,
                width, height);
    // Only the cards take the mouse; the gaps between them stay the canvas's.
    QRegion region;
    for (const QRect& rect : cardRects()) region += rect;
    setMask(region);
    show();
    raise();
    update();
}

QVector<QRect> NotificationToasts::cardRects() const {
    QVector<QRect> rects(m_cards.size());
    const int width = this->width();
    int bottom = height();
    for (int i = int(m_cards.size()) - 1; i >= 0; --i) {
        const int h = cardHeight(m_cards.at(i), width);
        rects[i] = QRect(0, bottom - h, width, h);
        bottom -= h + kSpacing;
    }
    return rects;
}

QRect NotificationToasts::closeRect(const QRect& card) {
    return QRect(card.right() - kPadH / 2 - kClose, card.top() + kPadV, kClose, kClose);
}

void NotificationToasts::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    const QVector<QRect> rects = cardRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (!rects.at(i).contains(pos)) continue;
        if (closeRect(rects.at(i)).adjusted(-6, -6, 6, 6).contains(pos)) {
            m_cards.removeAt(i);
            if (m_cards.isEmpty()) m_timer->stop();
            relayout();
        } else {
            clear();
            emit cardClicked();
        }
        return;
    }
}

qreal NotificationToasts::opacityOf(const Card& card, qint64 nowMs) const {
    const qreal in = qBound(0.0, qreal(nowMs - card.shownAtMs) / kFadeMs, 1.0);
    const qreal out = qBound(0.0, qreal(card.expiresAtMs - nowMs) / kFadeMs, 1.0);
    return qMin(in, out);
}

void NotificationToasts::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const qint64 now = m_clock.elapsed();
    const int width = this->width();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Newest at the bottom: lay the cards out from the bottom edge upwards.
    int bottom = height();
    for (int i = int(m_cards.size()) - 1; i >= 0 && bottom > 0; --i) {
        const Card& card = m_cards.at(i);
        const int h = cardHeight(card, width);
        const QRectF rect(0.5, bottom - h + 0.5, width - 1.0, h - 1.0);
        bottom -= h + kSpacing;

        painter.setOpacity(opacityOf(card, now));

        QColor fill = palette.surface;
        fill.setAlpha(kCardAlpha);
        QColor edge = palette.border;
        edge.setAlpha(kCardAlpha);
        QPainterPath path;
        path.addRoundedRect(rect, kRadius, kRadius);
        painter.setPen(QPen(edge, 1.0));
        painter.setBrush(fill);
        painter.drawPath(path);

        // Severity stripe, clipped to the card's rounded left edge.
        painter.save();
        painter.setClipPath(path);
        painter.fillRect(QRectF(rect.left(), rect.top(), kStripe, rect.height()),
                         severityColor(card.severity, palette));
        painter.restore();

        painter.setPen(palette.textPrimary);
        IconLibrary::instance()
            .icon(QStringLiteral("lucide:x"), palette.textSecondary, kClose)
            .paint(&painter, closeRect(rect.toAlignedRect()));
        painter.setPen(palette.textPrimary);
        const QRectF textRect = rect.adjusted(kStripe + kPadH, kPadV, -kPadH - kClose, -kPadV);
        const QFontMetrics metrics(font());
        QString text = cardText(card);
        // Cap long messages at kMaxTextLines, with an ellipsis on the last.
        const QRect full = metrics.boundingRect(textRect.toRect(), Qt::TextWordWrap, text);
        if (full.height() > textRect.height() + 1) {
            while (text.size() > 1 &&
                   metrics.boundingRect(textRect.toRect(), Qt::TextWordWrap, text + QChar(0x2026))
                           .height() > textRect.height() + 1) {
                text.chop(qMax(1, int(text.size()) / 20));
            }
            text += QChar(0x2026);
        }
        painter.drawText(textRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}

}  // namespace traceview
