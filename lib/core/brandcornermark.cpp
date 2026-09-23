#include "brandcornermark.h"

#include <QFile>
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
    QByteArray svg = m_svgTemplate;
    svg.replace(kWordmarkLightColor, color.name(QColor::HexRgb).toUtf8());
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
}

}  // namespace traceview
