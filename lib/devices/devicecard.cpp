#include "devicecard.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "dashboard/roundedcorners.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
// Same header scale as DashboardCell (see kHeaderHeight/kIconSize/
// kIconMargin/kStatusDotSize in dashboard/dashboardcell.cpp) -- kept as a
// separate copy here (DeviceCard doesn't depend on the dashboard library)
// rather than shared constants, but deliberately the same numbers so the two
// card kinds read as one visual family.
constexpr int kHeaderHeight = 24;
constexpr int kIconSize = 14;
constexpr int kIconMargin = 6;
constexpr int kStatusDotSize = 8;
constexpr int kBodyMargin = 10;
constexpr int kBodyLineHeight = 18;
constexpr int kSignalBarsCount = 5;
constexpr int kSignalIconWidth = 16;
constexpr int kSignalIconHeight = 11;

// Maps an RSSI reading to a 1-5 bar count, same rough dBm bands phone/router
// UIs use for Wi-Fi (this is ESP-NOW, same 2.4 GHz radio characteristics).
// Never 0 -- signalText/signalBarsForRssi are only ever shown for a peer
// confirmed online, so there is always at least a (weak) signal to report.
int signalBarsForRssi(qint8 rssiDbm) {
    if (rssiDbm >= -55) return 5;
    if (rssiDbm >= -65) return 4;
    if (rssiDbm >= -75) return 3;
    if (rssiDbm >= -85) return 2;
    return 1;
}

// Classic ascending-bar Wi-Fi glyph: `level` bars lit in `litColor`, the rest
// dimmed -- same "draw it, don't fake it" approach as drawCommTypeIcon/
// drawGearIcon below.
void drawSignalBars(QPainter& painter, const QRect& r, int level, const QColor& litColor,
                    const QColor& dimColor) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const qreal gap = r.width() * 0.1;
    const qreal barWidth = (r.width() - gap * (kSignalBarsCount - 1)) / kSignalBarsCount;
    for (int i = 0; i < kSignalBarsCount; ++i) {
        const qreal heightFrac = 0.3 + 0.7 * (qreal(i + 1) / kSignalBarsCount);
        const qreal barHeight = r.height() * heightFrac;
        const qreal x = r.left() + i * (barWidth + gap);
        const qreal y = r.bottom() - barHeight;
        painter.setBrush(i < level ? litColor : dimColor);
        const qreal radius = barWidth * 0.25;
        painter.drawRoundedRect(QRectF(x, y, barWidth, barHeight), radius, radius);
    }
    painter.restore();
}

// Chip/plug glyph for CommType::Btp -- a rounded body with pin legs on each
// side, same "draw it, don't fake it" live-QPainter approach as
// dashboard/dashboardcell.cpp's drawTypeIcon(). Switches on CommType (rather
// than a string id) so adding a new protocol without a case here is a
// compile warning, not a silent blank icon.
void drawCommTypeIcon(QPainter& painter, const QRect& r, CommType type, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.topLeft());
    const qreal s = r.width();

    switch (type) {
        case CommType::Btp: {
            QPen pinPen(color, 1.3);
            pinPen.setCapStyle(Qt::FlatCap);
            painter.setPen(pinPen);
            constexpr int kPins = 3;
            for (int i = 0; i < kPins; ++i) {
                const qreal y = s * (0.2 + i * 0.3);
                painter.drawLine(QPointF(0, y), QPointF(s * 0.14, y));
                painter.drawLine(QPointF(s * 0.86, y), QPointF(s, y));
            }
            painter.setPen(QPen(color, 1.3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(s * 0.14, s * 0.1, s * 0.72, s * 0.8), s * 0.12,
                                    s * 0.12);
            break;
        }
    }
    painter.restore();
}

// Settings gear glyph -- deliberately a fresh copy of DashboardCell's
// drawGearIcon() rather than a shared function: that one lives in
// dashboardcell.cpp's anonymous namespace (private to that TU), and this
// widget isn't meant to depend on the dashboard library at all.
void drawGearIcon(QPainter& painter, const QRect& r, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.center());
    const qreal s = r.width();
    const qreal bodyRadius = s * 0.28;
    const qreal toothOuterRadius = s * 0.44;

    QPen toothPen(color, s * 0.12);
    toothPen.setCapStyle(Qt::FlatCap);
    painter.setPen(toothPen);
    constexpr int kTeeth = 6;
    for (int i = 0; i < kTeeth; ++i) {
        painter.save();
        painter.rotate(360.0 / kTeeth * i);
        painter.drawLine(QPointF(0, -bodyRadius), QPointF(0, -toothOuterRadius));
        painter.restore();
    }

    painter.setPen(QPen(color, 1.3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(0, 0), bodyRadius, bodyRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(0, 0), bodyRadius * 0.32, bodyRadius * 0.32);
    painter.restore();
}

// "<>" code-bracket glyph for the script icon -- same "draw it, don't fake
// it" procedural approach as drawGearIcon() just above.
void drawScriptIcon(QPainter& painter, const QRect& r, const QColor& color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(r.topLeft());
    const qreal s = r.width();

    QPen pen(color, s * 0.12);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const qreal midY = s * 0.5;
    const qreal apexInset = s * 0.16;
    painter.drawLine(QPointF(s * 0.42, s * 0.16), QPointF(apexInset, midY));
    painter.drawLine(QPointF(apexInset, midY), QPointF(s * 0.42, s * 0.84));

    painter.drawLine(QPointF(s * 0.58, s * 0.16), QPointF(s - apexInset, midY));
    painter.drawLine(QPointF(s - apexInset, midY), QPointF(s * 0.58, s * 0.84));

    painter.restore();
}
}  // namespace

DeviceCard::DeviceCard(QWidget* parent) : QWidget(parent) {
    setFixedSize(kDeviceCardSize);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { update(); });
}

void DeviceCard::setDevice(const Device& device) {
    m_device = device;
    update();
}

void DeviceCard::setSelected(bool selected) {
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    update();
}

QRect DeviceCard::headerRect() const {
    return QRect(0, 0, width(), kHeaderHeight);
}

QRect DeviceCard::gearButtonRect() const {
    const QRect header = headerRect();
    const int y = (header.height() - kIconSize) / 2;
    return QRect(header.right() - kIconMargin - kIconSize + 1, y, kIconSize, kIconSize);
}

QRect DeviceCard::scriptButtonRect() const {
    const QRect gear = gearButtonRect();
    return QRect(gear.left() - kIconMargin - kIconSize, gear.top(), kIconSize, kIconSize);
}

QRect DeviceCard::statusDotRect() const {
    // Same placement math paintEvent() uses to lay out the header row: comm-
    // type icon first, then the dot immediately after it.
    const QRect iconRect(kIconMargin, (kHeaderHeight - kIconSize) / 2, kIconSize, kIconSize);
    const int left = iconRect.right() + kIconMargin;
    return QRect(left, (kHeaderHeight - kStatusDotSize) / 2, kStatusDotSize, kStatusDotSize);
}

void DeviceCard::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();

    // 2px border width, matching DashboardCell's BorderOverlay (see
    // dashboard/dashboardcell.cpp) -- the idle and selected outlines below
    // share one width so selection reads as a color change, not also a
    // thickness jump.
    constexpr qreal kBorderWidth = 2.0;
    const QPainterPath outline =
        partiallyRoundedRect(QRectF(rect()).adjusted(kBorderWidth / 2.0, kBorderWidth / 2.0,
                                                     -kBorderWidth / 2.0, -kBorderWidth / 2.0),
                             kContainerCornerRadius, true, true, true, true);
    painter.fillPath(outline, palette.surface);
    painter.setPen(QPen(palette.border, kBorderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);

    painter.save();
    painter.setClipPath(outline);
    painter.fillRect(headerRect(), palette.surfaceAlt);
    painter.restore();

    const QColor headerFg = palette.textPrimary;
    QRect textRect = headerRect().adjusted(kIconMargin, 0, -kIconMargin, 0);

    const QRect iconRect(kIconMargin, (kHeaderHeight - kIconSize) / 2, kIconSize, kIconSize);
    drawCommTypeIcon(painter, iconRect, m_device.commType, headerFg);
    textRect.setLeft(iconRect.right() + kIconMargin);

    // Connection dot -- three states, not two (topico 35 D.2): red offline,
    // amber "port open but no BTP session" (the "connected and mute" case),
    // green live. Also the click target for connectToggleRequested() (see
    // mousePressEvent()).
    const DeviceLinkState linkState = deviceLinkState(m_device);
    const QColor dotColor = (linkState == DeviceLinkState::Live)      ? palette.success
                            : (linkState == DeviceLinkState::Offline) ? palette.danger
                                                                     : palette.warning;
    const QRect dotRect = statusDotRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(dotColor);
    painter.drawEllipse(dotRect);
    textRect.setLeft(dotRect.right() + kIconMargin);

    textRect.setRight(scriptButtonRect().left() - kIconMargin);
    drawScriptIcon(painter, scriptButtonRect(), headerFg);
    drawGearIcon(painter, gearButtonRect(), headerFg);

    // Signal strength: only a hub child (dongle<->robot ESP-NOW link) has
    // one, and only once the robot is confirmed online -- an offline/unknown
    // peer's last RSSI is stale and would read as a live number it isn't.
    // Bars are the at-a-glance read (a raw dBm number means little without
    // context); the dBm text stays alongside for anyone who wants the exact
    // value. Weak signal (1-2 bars) colors both red.
    const bool showSignal = m_device.transportType == TransportType::HubChannel &&
                            m_device.peerPresenceKnown && m_device.peerOnline;
    QString signalText;
    int signalLevel = 0;
    QColor signalColor = palette.textSecondary;
    QRect signalTextRect;
    QRect signalIconRect;
    if (showSignal) {
        signalText = tr("%1 dBm").arg(m_device.peerRssi);
        signalLevel = signalBarsForRssi(m_device.peerRssi);
        signalColor = (signalLevel <= 2) ? palette.danger : palette.textSecondary;

        const QFontMetrics signalMetrics(painter.font());
        const int signalWidth = signalMetrics.horizontalAdvance(signalText);
        signalTextRect = textRect;
        signalTextRect.setLeft(textRect.right() - signalWidth);
        signalIconRect = QRect(signalTextRect.left() - kIconMargin - kSignalIconWidth,
                               (kHeaderHeight - kSignalIconHeight) / 2, kSignalIconWidth,
                               kSignalIconHeight);
        textRect.setRight(signalIconRect.left() - kIconMargin);
    }

    painter.setPen(headerFg);
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    const QFontMetrics titleMetrics(titleFont);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     titleMetrics.elidedText(m_device.name, Qt::ElideRight, textRect.width()));

    if (showSignal) {
        drawSignalBars(painter, signalIconRect, signalLevel, signalColor, palette.border);
        QFont signalFont = titleFont;
        signalFont.setBold(false);
        painter.setFont(signalFont);
        painter.setPen(signalColor);
        painter.drawText(signalTextRect, Qt::AlignVCenter | Qt::AlignRight, signalText);
    }

    // Body: a word-wrapped description, then (if the device has ever
    // completed a handshake) the BTP version/ID it last reported -- see
    // Device::btpVersion/btpId. Previously that pair only showed up behind
    // the gear, in DeviceConfigDialog's "Reported by device" section;
    // surfaced here too so it's visible without opening that dialog.
    //
    // This used to also start with a comm-type label line above the
    // description -- but with only CommType::Btp existing, that line and the
    // reported line below both just printed the bare word "BTP" with nothing
    // distinguishing the two. Dropped the label line entirely (and with it
    // device.h's commTypeLabel() helper, which had no other caller) and
    // reworded the reported line to lead with "v"/"ID" instead of repeating
    // "BTP".
    const QRect body =
        rect().adjusted(kBodyMargin, kHeaderHeight + kBodyMargin, -kBodyMargin, -kBodyMargin);
    QFont bodyFont = painter.font();
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    const QFontMetrics bodyMetrics(bodyFont);

    QString reportedLine =
        m_device.btpVersion.isEmpty()
            ? (m_device.btpId.isEmpty() ? QString() : tr("ID %1").arg(m_device.btpId))
            : (m_device.btpId.isEmpty()
                   ? tr("v%1").arg(m_device.btpVersion)
                   : tr("v%1 \xC2\xB7 ID %2").arg(m_device.btpVersion, m_device.btpId));
    // Name the amber state in words, not just a dot colour (topico 35 D.2).
    if (linkState == DeviceLinkState::TransportOnly) {
        reportedLine = tr("port open, waiting for BTP session");
    } else if (m_device.transportType == TransportType::HubChannel &&
               linkState == DeviceLinkState::PeerStale) {
        reportedLine = m_device.peerPresenceKnown
                           ? tr("hub link up, no data from robot")
                           : tr("hub link up, locating robot…");
    }
    const int reportedHeight = reportedLine.isEmpty() ? 0 : kBodyLineHeight + 4;

    painter.setPen(palette.textPrimary);
    const QRect descRect(body.left(), body.top(), body.width(), body.height() - reportedHeight);
    painter.drawText(descRect, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                     m_device.description);

    if (!reportedLine.isEmpty()) {
        painter.setPen(palette.textSecondary);
        const QRect reportedRect(body.left(), body.bottom() - kBodyLineHeight, body.width(),
                                 kBodyLineHeight);
        painter.drawText(
            reportedRect, Qt::AlignVCenter | Qt::AlignLeft,
            bodyMetrics.elidedText(reportedLine, Qt::ElideRight, reportedRect.width()));
    }

    // Selection border -- same palette.accent convention as DashboardCell's
    // selected state, minus the color-blend animation (this card's selection
    // never competes with a drag-invalid state the way a dashboard cell's
    // does, so a flat border is enough). Drawn last, on top of everything
    // else: drawing it earlier (right after the base outline) left it
    // partially overpainted by the header's fillRect() in the top strip,
    // showing up as a notch where the border looked thinner across the
    // header than below it.
    if (m_selected) {
        painter.setPen(QPen(palette.accent, kBorderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }
}

void DeviceCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (gearButtonRect().contains(event->position().toPoint())) {
        emit configRequested(m_device.id);
        event->accept();
        return;
    }
    if (scriptButtonRect().contains(event->position().toPoint())) {
        emit scriptRequested(m_device.id);
        event->accept();
        return;
    }
    // Inflated a few px past the dot's tiny 8x8 paint size -- an 8px target
    // is otherwise unreasonably fussy to hit deliberately.
    if (statusDotRect().adjusted(-4, -4, 4, 4).contains(event->position().toPoint())) {
        emit connectToggleRequested(m_device.id);
        event->accept();
        return;
    }
    emit selectRequested(m_device.id);
    event->accept();
}

}  // namespace traceview
