#include "stylesheet.h"

namespace traceview {

namespace {

QString cssColor(const QColor& c) {
    if (c.alpha() < 255) {
        return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
    return c.name(QColor::HexRgb);
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

QTabWidget::pane {
    border: 1px solid @border@;
}
QTabBar {
    background-color: @surfaceAlt@;
}
QTabBar::tab {
    background-color: @surface@;
    color: @textSecondary@;
    border: 1px solid @border@;
    padding: 3px 12px;
}
QTabBar::tab:selected {
    background-color: @surfaceAlt@;
    color: @textPrimary@;
    border-bottom-color: @surfaceAlt@;
}
QTabBar::tab:disabled {
    color: @textDisabled@;
    background-color: @surface@;
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

    return qss;
}

} // namespace traceview
