#include "brandcornermark.h"

#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QSvgRenderer>

namespace traceview {

namespace {

constexpr char kWordmarkResource[] = ":/branding/wordmark-corner.svg";
// The SVG's own light color -- swapped for the theme's text color.
constexpr char kWordmarkLightColor[] = "#F5F7FA";
// y of the middle (white) trace, in the SVG's viewBox units.
constexpr qreal kWordmarkMidlineY = 116.0;
// The group holding the outlined "TraceView" letters, dropped when
// setWordmarkText() replaces them, and the box those letters fill (viewBox
// units: left edge, baseline, cap height, width).
constexpr char kLetteringTitle[] = "<title>TraceView</title>";
constexpr char kGroupStart[] = "<g>";
constexpr char kGroupEnd[] = "</g>";
constexpr qreal kLetteringLeft = 105.8;
constexpr qreal kLetteringBaseline = 99.0;
constexpr qreal kLetteringCapHeight = 26.6;
constexpr qreal kLetteringWidth = 182.7;

}  // namespace

BrandCornerMark::BrandCornerMark(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);

    // Soft shadow so the mark lifts off whatever dashboard content it
    // overhangs. Follows the drawn shape (lines, dots, letters), not the
    // widget's rectangle.
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(8);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 110));
    setGraphicsEffect(shadow);

    QFile file(kWordmarkResource);
    if (file.open(QIODevice::ReadOnly)) {
        m_svgTemplate = file.readAll();
    }
    m_renderer = new QSvgRenderer(m_svgTemplate, this);
}

void BrandCornerMark::setForegroundColor(const QColor& color) {
    m_foregroundColor = color;
    reloadSvg();
}

void BrandCornerMark::setWordmarkText(const QString& text) {
    m_wordmarkText = text;
    reloadSvg();
}

void BrandCornerMark::reloadSvg() {
    QByteArray svg = m_svgTemplate;
    if (!m_wordmarkText.isEmpty()) {
        const qsizetype title = svg.indexOf(kLetteringTitle);
        const qsizetype start = title < 0 ? -1 : svg.lastIndexOf(kGroupStart, title);
        const qsizetype end = start < 0 ? -1 : svg.indexOf(kGroupEnd, title);
        if (end >= 0) {
            svg.remove(start, end + qsizetype(qstrlen(kGroupEnd)) - start);
        }
    }
    if (m_foregroundColor.isValid()) {
        svg.replace(kWordmarkLightColor, m_foregroundColor.name(QColor::HexRgb).toUtf8());
    }
    m_renderer->load(svg);
    update();
}

void BrandCornerMark::reposition(int height, int maxWidth, int midlineY) {
    if (!parentWidget() || !m_renderer->isValid()) {
        return;
    }
    const QRectF viewBox = m_renderer->viewBoxF();
    // Share of the mark's height that sits above the middle trace.
    const qreal midlineRatio = (kWordmarkMidlineY - viewBox.top()) / viewBox.height();
    qreal h = qMin<qreal>(height, midlineY / midlineRatio);
    h = qMin<qreal>(h, maxWidth * viewBox.height() / viewBox.width());
    const int width = qRound(h * viewBox.width() / viewBox.height());
    setGeometry(parentWidget()->width() - width, qRound(midlineY - h * midlineRatio), width,
                qRound(h));
}

void BrandCornerMark::paintEvent(QPaintEvent*) {
    if (!m_renderer->isValid()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    m_renderer->render(&painter, QRectF(rect()));
    if (m_wordmarkText.isEmpty()) {
        return;
    }

    // Bold text sized to the original letters' cap height, shrunk if it would
    // run wider than they did.
    const QRectF viewBox = m_renderer->viewBoxF();
    const qreal scale = width() / viewBox.width();
    QFont font = this->font();
    font.setBold(true);
    font.setPixelSize(100);
    const qreal capHeightRatio = QFontMetricsF(font).capHeight() / 100.0;
    qreal pixelSize = kLetteringCapHeight * scale / capHeightRatio;
    font.setPixelSize(qMax(1, qRound(pixelSize)));
    const qreal textWidth = QFontMetricsF(font).horizontalAdvance(m_wordmarkText);
    if (textWidth > kLetteringWidth * scale) {
        pixelSize *= kLetteringWidth * scale / textWidth;
        font.setPixelSize(qMax(1, qRound(pixelSize)));
    }
    painter.setFont(font);
    painter.setPen(m_foregroundColor.isValid() ? m_foregroundColor : QColor(kWordmarkLightColor));
    painter.drawText(QPointF((kLetteringLeft - viewBox.left()) * scale,
                             (kLetteringBaseline - viewBox.top()) * scale),
                     m_wordmarkText);
}

}  // namespace traceview
