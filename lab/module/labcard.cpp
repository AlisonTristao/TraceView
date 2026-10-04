#include "lab/module/labcard.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "theme/iconutils.h"
#include "traceview/framestyle.h"
#include "traceview/thememanager.h"

namespace tracelab {

namespace {
// Same header scale as DeviceCard and DashboardCell (kHeaderHeight/
// kIconSize/kIconMargin/kStatusDotSize there), so the three card kinds read
// as one family.
constexpr int kHeaderHeight = 24;
constexpr int kIconSize = 14;
constexpr int kIconMargin = 6;
constexpr int kStatusDotSize = 8;
constexpr int kBodyMargin = 12;
constexpr int kBodySpacing = 10;
}  // namespace

LabCard::LabCard(const QString& iconPath, const QString& title, QWidget* parent)
    : QWidget(parent), m_iconPath(iconPath), m_title(title) {
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(kBodyMargin, kHeaderHeight + kBodyMargin, kBodyMargin,
                               kBodyMargin);
    m_body->setSpacing(kBodySpacing);
    auto& themes = traceview::ThemeManager::instance();
    connect(&themes, &traceview::ThemeManager::themeChanged, this, [this] { update(); });
    connect(&themes, &traceview::ThemeManager::frameStyleChanged, this, [this] { update(); });
}

void LabCard::setStatus(Tone tone, const QString& text) {
    if (tone == m_tone && text == m_statusText) {
        return;
    }
    m_tone = tone;
    m_statusText = text;
    update(0, 0, width(), kHeaderHeight);
}

void LabCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();

    // The outline exactly as DeviceCard::paintEvent() draws it.
    const traceview::FrameStyle& frame = traceview::ThemeManager::instance().currentFrameStyle();
    constexpr qreal kInset = traceview::kFrameSelectionWidth / 2.0;
    const QPainterPath outline = traceview::frameShapePath(
        QRectF(rect()).adjusted(kInset, kInset, -kInset, -kInset), frame);
    painter.fillPath(outline, palette.surface);
    if (frame.idleOutline && frame.borderWidth > 0.0) {
        painter.setPen(QPen(palette.border, frame.borderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }
    const QRect header(0, 0, width(), kHeaderHeight);
    painter.save();
    painter.setClipPath(outline);
    painter.fillRect(header, palette.surfaceAlt);
    painter.restore();

    const QRect iconRect(kIconMargin, (kHeaderHeight - kIconSize) / 2, kIconSize, kIconSize);
    traceview::drawTintedIcon(painter, iconRect, m_iconPath, palette.textPrimary);
    QRect textRect = header.adjusted(iconRect.right() + kIconMargin, 0, -kIconMargin * 2, 0);

    QColor toneColor;
    switch (m_tone) {
        case Tone::Success:
            toneColor = palette.success;
            break;
        case Tone::Warning:
            toneColor = palette.warning;
            break;
        case Tone::Danger:
            toneColor = palette.danger;
            break;
        case Tone::None:
            break;
    }
    if (toneColor.isValid()) {
        const QRect dotRect(textRect.left(), (kHeaderHeight - kStatusDotSize) / 2,
                            kStatusDotSize, kStatusDotSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(toneColor);
        painter.drawEllipse(dotRect);
        textRect.setLeft(dotRect.right() + kIconMargin);
    }

    QFont statusFont = painter.font();
    statusFont.setBold(false);
    if (!m_statusText.isEmpty()) {
        const int statusWidth = QFontMetrics(statusFont).horizontalAdvance(m_statusText);
        const QRect statusRect(textRect.right() - statusWidth, 0, statusWidth, kHeaderHeight);
        painter.setFont(statusFont);
        painter.setPen(toneColor.isValid() ? toneColor : palette.textSecondary);
        painter.drawText(statusRect, Qt::AlignVCenter | Qt::AlignRight, m_statusText);
        textRect.setRight(statusRect.left() - kIconMargin * 2);
    }

    QFont titleFont = statusFont;
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(palette.textPrimary);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(titleFont).elidedText(m_title, Qt::ElideRight, textRect.width()));
}

}  // namespace tracelab
