#include "theme/busyspinner.h"

#include <QEvent>
#include <QPainter>
#include <QTimer>

#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kDiameter = 28;
constexpr int kMargin = 12;
constexpr int kTickCount = 12;
constexpr int kIntervalMs = 80;
constexpr qreal kOuterRadius = kDiameter / 2.0 - 2.0;
constexpr qreal kInnerRadius = kOuterRadius - 7.0;
}  // namespace

BusySpinner::BusySpinner(QWidget* parent) : QWidget(parent) {
    setFixedSize(kDiameter, kDiameter);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();

    if (parent) {
        parent->installEventFilter(this);
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(kIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 360 / kTickCount) % 360;
        update();
    });

    // Repainted with whatever palette is current next time it ticks -- no
    // need to force a repaint just for a theme switch.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this] { update(); });
}

void BusySpinner::start() {
    reposition();
    raise();
    show();
    m_timer->start();
}

void BusySpinner::stop() {
    m_timer->stop();
    hide();
}

bool BusySpinner::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void BusySpinner::reposition() {
    if (!parentWidget()) {
        return;
    }
    move(parentWidget()->width() - width() - kMargin, kMargin);
}

void BusySpinner::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(width() / 2.0, height() / 2.0);

    const QColor accent = ThemeManager::instance().currentTheme().accent;
    for (int i = 0; i < kTickCount; ++i) {
        QColor tick = accent;
        tick.setAlphaF(0.12f + 0.88f * (float(i) / float(kTickCount)));
        painter.save();
        painter.rotate(m_angle + i * (360.0 / kTickCount));
        painter.setPen(QPen(tick, 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(0, -kOuterRadius), QPointF(0, -kInnerRadius));
        painter.restore();
    }
}

}  // namespace traceview
