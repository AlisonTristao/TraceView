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

// Same rationale as arrowImagePath above: QCheckBox::indicator:checked is a
// pixmap subcontrol, so the checkmark has to be handed to Qt as an actual
// image rather than drawn via QSS borders.
QString checkImagePath(const QColor& color) {
    constexpr int kSize = 10;
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawPolyline(QPolygon({QPoint(1, 5), QPoint(4, 8), QPoint(9, 2)}));
    painter.end();

    const QString path = QDir(QDir::tempPath()).filePath("traceview_check.png");
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

/* DashboardCell paints only its rounded silhouette itself. Leaving the
   global rectangular QWidget background active here creates square corner
   patches over the dotted layout canvas. */
QWidget[dashboardCell="true"] {
    background: transparent;
}

/* LayersPanel/PropertiesPanel float over the dashboard canvas (see
   MainWindow's positionOverlayPanels()) rather than sharing its row via
   layout. Both would otherwise inherit the same @background@ as the canvas
   from the generic QWidget rule above, so any part of the panel not
   covered by its own child widgets (e.g. the empty stretch next to the pin
   button in its top bar) reads as the canvas showing through rather than
   as part of the panel -- @surface@ (already used for the ribbon/status
   bar's chrome, elsewhere in this file) plus a border on the canvas-facing
   edge give the whole panel a fill and outline that's visibly its own. */
QWidget#layersPanel {
    background-color: @surface@;
    border-right: 1px solid @border@;
}
QWidget#propertiesPanel {
    background-color: @surface@;
    border-left: 1px solid @border@;
}

/* DockablePanelHeader is the strip a panel gets dragged/docked by (see
   dockablepanelheader.h) -- a plain bottom border on its own wouldn't read
   against the panel's own @surface@ fill, so it gets the same @surfaceAlt@
   tone already used for the ribbon/menu bar's chrome (see kRibbonTopMargin's
   comment) to read as a distinct, grabbable bar rather than blending into
   the rest of the panel. */
QWidget#dockablePanelHeader {
    background-color: @surfaceAlt@;
    border-bottom: 1px solid @border@;
}

/* PanelDockController's resize grips (edge strip while docked, corner
   square while floating, see dockresizegrip.h) are invisible until hovered
   -- a permanent visible handle would be one more line competing with the
   canvas/panel content for so little actual width. */
QWidget#dockResizeGrip {
    background-color: transparent;
}
QWidget#dockResizeGrip:hover {
    background-color: @accent@;
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

/* Panel pin toggles (PropertiesPanel/LayersPanel) already show their pinned
   state through the icon itself (hollow vs. filled pushpin) -- the generic
   solid-accent checked background above would double up on that and just
   look like a stray blue box, so keep this one flat and let hover be the
   only surface feedback. Opaque @surface@ (matching the panels' own fill,
   see "QWidget#layersPanel"/"QWidget#propertiesPanel" above) rather than
   the generic QToolButton's transparent, though, so the button doesn't
   stand out as a different-colored patch against its panel. */
QToolButton#pinButton {
    background-color: @surface@;
}
QToolButton#pinButton:checked {
    background-color: @surface@;
    color: @textPrimary@;
    border-color: transparent;
}
QToolButton#pinButton:checked:hover {
    background-color: @surfaceAlt@;
    border-color: @border@;
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
QPushButton:checked {
    background-color: @accent@;
    color: @background@;
    border-color: @accent@;
}
QPushButton:disabled {
    color: @textDisabled@;
    border-color: @border@;
}

QPushButton[variant="success"] {
    background-color: @success@;
    color: @background@;
    border-color: @success@;
}
QPushButton[variant="warning"] {
    background-color: @warning@;
    color: @background@;
    border-color: @warning@;
}
QPushButton[variant="danger"] {
    background-color: @danger@;
    color: @background@;
    border-color: @danger@;
}

/* Headerless dashboard controls (push button/toggle/slider, see
   controlwidgets.cpp) opt into this via a dynamic "dashboardControlPanel"
   property instead of DashboardWidget's default background-color (which
   matches the canvas behind it -- see dashboardcell paintEvent's rounded
   corner). Without a fill that actually contrasts with the canvas, their
   rounded cell border reads as a stray disconnected curve instead of a
   panel once another widget sits nearby. DashboardWidget::setEditModeHint()
   only turns this property on while arranging (Layout) -- in Run it's off,
   so the control blends into the canvas with no background box behind it. */
QWidget[dashboardControlPanel="true"] {
    background-color: @surface@;
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

QListWidget {
    background-color: @surface@;
    color: @textPrimary@;
    border: 1px solid @border@;
}
QListWidget::item {
    padding: 4px;
}
QListWidget::item:selected {
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

QSlider::groove:horizontal {
    background: @surfaceAlt@;
    border: 1px solid @border@;
    height: 6px;
    border-radius: 3px;
    /* Groove is inset 5px top/bottom so the style reserves the full 16px
       (handle diameter) of vertical space for the control -- the handle's
       matching -5px margin below then expands back out to fill exactly
       that reserved space instead of overflowing it and getting clipped
       by the widget's own bounds. */
    margin: 5px 0;
}
QSlider::sub-page:horizontal {
    background: @accent@;
    border-radius: 3px;
}
QSlider::add-page:horizontal {
    background: @surfaceAlt@;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background: @accent@;
    border: 1px solid @accentHover@;
    width: 16px;
    height: 16px;
    margin: -5px 0;
    border-radius: 8px;
}
QSlider::handle:horizontal:hover {
    background: @accentHover@;
}
QSlider::handle:horizontal:pressed {
    background: @accentPressed@;
}
QSlider::handle:horizontal:disabled {
    background: @textDisabled@;
    border-color: @border@;
}

QCheckBox {
    background: transparent;
    spacing: 6px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    border: 1px solid @borderStrong@;
    border-radius: 3px;
    background-color: @surface@;
}
QCheckBox::indicator:hover {
    border-color: @accentHover@;
}
QCheckBox::indicator:checked {
    background-color: @accent@;
    border-color: @accent@;
    image: url("@checkArrowUri@");
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
    qss.replace("@checkArrowUri@", checkImagePath(p.background));

    return qss;
}

} // namespace traceview
