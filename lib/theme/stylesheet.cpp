#include "stylesheet.h"

#include <QDir>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>

namespace traceview {

namespace {

QString cssColor(const QColor& c) {
    if (c.alpha() < 255) {
        return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
    return c.name(QColor::HexRgb);
}

// QComboBox::down-arrow (and, below, QSpinBox's up/down-arrow) are pixmap
// subcontrols — border-only CSS triangle tricks that work for normal
// widgets don't reliably render for them, so draw a small filled triangle
// instead (same "draw it, don't fake it with borders" approach as the
// ribbon icons in ribbonicons.cpp) and hand Qt a real image. Qt's style
// sheet url() doesn't understand data: URIs, only actual paths, so the
// themed triangle is written to a temp file each time the stylesheet is
// (re)built and referenced by that path.
QString arrowImagePath(const QColor& color, const QString& fileTag, bool pointingDown) {
    constexpr int kSize = 10;
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const QPolygon triangle = pointingDown
                                   ? QPolygon({QPoint(1, 3), QPoint(kSize - 1, 3), QPoint(kSize / 2, kSize - 2)})
                                   : QPolygon({QPoint(1, kSize - 3), QPoint(kSize - 1, kSize - 3), QPoint(kSize / 2, 2)});
    painter.drawPolygon(triangle);
    painter.end();

    const QString path = QDir(QDir::tempPath()).filePath(QString("traceview_%1_arrow.png").arg(fileTag));
    pixmap.save(path, "PNG");
    return path;
}

} // namespace

QString buildStyleSheet(const ThemePalette& p) {
    QString qss = R"(
QWidget {
    background-color: @background@;
    color: @textPrimary@;
    selection-background-color: @accent@;
    selection-color: @background@;
}

QMainWindow::separator {
    background-color: @border@;
    width: 1px;
    height: 1px;
}

QMenuBar {
    background-color: @surfaceAlt@;
    border-bottom: 1px solid @border@;
}
QMenuBar::item {
    padding: 4px 10px;
    background: transparent;
}
QMenuBar::item:selected {
    background-color: @surfaceAlt@;
}

QMenu {
    background-color: @surface@;
    border: 1px solid @border@;
}
QMenu::item {
    padding: 4px 24px 4px 12px;
}
QMenu::item:selected {
    background-color: @accent@;
    color: @background@;
}
QMenu::separator {
    height: 1px;
    background-color: @border@;
    margin: 4px 0;
}

QToolBar {
    background-color: @surface@;
    border-bottom: 1px solid @border@;
    spacing: 4px;
}

QToolButton {
    background-color: transparent;
    color: @textPrimary@;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 2px;
}
QToolButton:hover {
    background-color: @surfaceAlt@;
    border-color: @border@;
}
QToolButton:pressed {
    background-color: @accentPressed@;
    color: @background@;
}
QToolButton:checked {
    background-color: @accent@;
    color: @background@;
    border-color: @accent@;
}
QToolButton:disabled {
    color: @textDisabled@;
}

QStatusBar {
    background-color: @surface@;
    border-top: 1px solid @border@;
    color: @textSecondary@;
}

QPushButton {
    background-color: @surface@;
    color: @textPrimary@;
    border: 1px solid @borderStrong@;
    border-radius: 4px;
    padding: 5px 14px;
}
QPushButton:hover {
    background-color: @surfaceAlt@;
    border-color: @accentHover@;
}
QPushButton:pressed {
    background-color: @accentPressed@;
    color: @background@;
}
QPushButton:disabled {
    color: @textDisabled@;
    border-color: @border@;
}

QLabel {
    background: transparent;
}
QLabel:disabled {
    color: @textDisabled@;
}

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: @surface@;
    color: @textPrimary@;
    border: 1px solid @border@;
    border-radius: 4px;
    padding: 3px 6px;
}
QLineEdit:focus, QComboBox:focus {
    border: 1px solid @accent@;
}

QSpinBox, QDoubleSpinBox {
    padding-right: 20px;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 18px;
    border-left: 1px solid @border@;
    border-bottom: 1px solid @border@;
    border-top-right-radius: 4px;
    background-color: @surfaceAlt@;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 18px;
    border-left: 1px solid @border@;
    border-bottom-right-radius: 4px;
    background-color: @surfaceAlt@;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: @accent@;
}
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed,
QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed {
    background-color: @accentPressed@;
}
QSpinBox::up-button:disabled, QDoubleSpinBox::up-button:disabled,
QSpinBox::down-button:disabled, QDoubleSpinBox::down-button:disabled {
    background-color: @surface@;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: url("@spinUpArrowUri@");
    width: 8px;
    height: 8px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: url("@comboArrowUri@");
    width: 8px;
    height: 8px;
}

QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 18px;
    border-left: 1px solid @border@;
    border-top-right-radius: 4px;
    border-bottom-right-radius: 4px;
}
QComboBox::down-arrow {
    image: url("@comboArrowUri@");
    width: 10px;
    height: 10px;
}
QComboBox QAbstractItemView {
    background-color: @surface@;
    color: @textPrimary@;
    border: 1px solid @border@;
    outline: none;
    selection-background-color: @accent@;
    selection-color: @background@;
}

QGroupBox {
    border: 1px solid @border@;
    border-radius: 4px;
    margin-top: 10px;
    padding-top: 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    padding: 0 4px;
    color: @textSecondary@;
}

QWidget#ribbon {
    background-color: @surfaceAlt@;
    border-bottom: 1px solid @border@;
}
QWidget#ribbonPage {
    background-color: @surfaceAlt@;
}
QFrame#ribbonGroup {
    background-color: transparent;
    border: 1px solid @border@;
    border-radius: 4px;
}

QFrame#sectionDivider {
    background-color: @border@;
    border: none;
}

QTableWidget {
    background-color: @surface@;
    color: @textPrimary@;
    border: 1px solid @border@;
    gridline-color: @border@;
}
QTableWidget::item:selected {
    background-color: @accent@;
    color: @background@;
}
QHeaderView::section {
    background-color: @surfaceAlt@;
    color: @textSecondary@;
    border: none;
    border-bottom: 1px solid @border@;
    padding: 4px;
}

QScrollBar:vertical {
    background: @background@;
    width: 12px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: @surfaceAlt@;
    border: 1px solid @border@;
    min-height: 24px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover {
    background: @accent@;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar:horizontal {
    background: @background@;
    height: 12px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: @surfaceAlt@;
    border: 1px solid @border@;
    min-width: 24px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal:hover {
    background: @accent@;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}
)";

    qss.replace("@background@", cssColor(p.background));
    qss.replace("@surfaceAlt@", cssColor(p.surfaceAlt));
    qss.replace("@surface@", cssColor(p.surface));
    qss.replace("@borderStrong@", cssColor(p.borderStrong));
    qss.replace("@border@", cssColor(p.border));
    qss.replace("@textPrimary@", cssColor(p.textPrimary));
    qss.replace("@textSecondary@", cssColor(p.textSecondary));
    qss.replace("@textDisabled@", cssColor(p.textDisabled));
    qss.replace("@accentHover@", cssColor(p.accentHover));
    qss.replace("@accentPressed@", cssColor(p.accentPressed));
    qss.replace("@accent@", cssColor(p.accent));
    qss.replace("@success@", cssColor(p.success));
    qss.replace("@warning@", cssColor(p.warning));
    qss.replace("@danger@", cssColor(p.danger));
    qss.replace("@comboArrowUri@", arrowImagePath(p.textSecondary, "combo", /*pointingDown=*/true));
    qss.replace("@spinUpArrowUri@", arrowImagePath(p.textSecondary, "spin_up", /*pointingDown=*/false));

    return qss;
}

} // namespace traceview
