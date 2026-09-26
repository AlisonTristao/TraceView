#include "startuploadingoverlay.h"

#include <QEvent>
#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr char kWordmarkResource[] = ":/branding/wordmark.svg";
// The SVG's own light color -- swapped for the theme's text color.
constexpr char kWordmarkLightColor[] = "#F5F7FA";

}  // namespace

StartupLoadingOverlay::StartupLoadingOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::NoFocus);

    QFile file(kWordmarkResource);
    if (file.open(QIODevice::ReadOnly)) {
        m_svgTemplate = file.readAll();
    }
    m_renderer = new QSvgRenderer(this);
    applyTheme();

    parent->installEventFilter(this);
    setGeometry(parent->rect());
    raise();
}

void StartupLoadingOverlay::applyTheme() {
    QByteArray svg = m_svgTemplate;
    svg.replace(kWordmarkLightColor,
                ThemeManager::instance().currentTheme().textPrimary.name(QColor::HexRgb).toUtf8());
    m_renderer->load(svg);
}

bool StartupLoadingOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

void StartupLoadingOverlay::paintEvent(QPaintEvent* /*event*/) {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette.background);

    // Wordmark at up to 60% of the width, centered a bit above the middle,
    // with the caption right below it.
    QRectF markRect;
    if (m_renderer->isValid()) {
        const QRectF viewBox = m_renderer->viewBoxF();
        const qreal w = qMin<qreal>(width() * 0.6, 320);
        const qreal h = w * viewBox.height() / viewBox.width();
        markRect = QRectF((width() - w) / 2, height() * 0.45 - h / 2, w, h);
        m_renderer->render(&painter, markRect);
    }

    QFont font = this->font();
    font.setPointSizeF(font.pointSizeF() * 1.1);
    painter.setFont(font);
    painter.setPen(palette.textSecondary);
    const qreal top = markRect.isValid() ? markRect.bottom() + 12 : height() / 2.0;
    painter.drawText(QRectF(0, top, width(), painter.fontMetrics().height() * 2),
                     Qt::AlignHCenter | Qt::AlignTop, tr("Loading dashboard..."));

    if (!m_painted) {
        m_painted = true;
        emit firstPainted();
    }
}

}  // namespace traceview
