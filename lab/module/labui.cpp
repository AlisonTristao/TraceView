#include "labui.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>

#include "traceview/thememanager.h"

namespace tracelab {

namespace ui {

namespace {

// The margins of a Settings category page (lib/core/settingspage.cpp).
constexpr int kPageMarginH = 28;
constexpr int kPageMarginV = 24;
constexpr int kPageSpacing = 20;

// With the alpha: the theme's borders are translucent.
QString css(const QColor& color) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

}  // namespace

QString styleSheet(const traceview::ThemePalette& palette) {
    QString sheet = QStringLiteral(
        "QLabel#labFieldLabel, QLabel#labNote, QLabel#labArrow, QLabel#labKind,"
        " QLabel#labStatCaption { color: @textSecondary@; }"
        // Inside a section: one frame, no boxes within boxes.
        "QGroupBox#labSection QListWidget, QGroupBox#labSection QPlainTextEdit,"
        " QGroupBox#labSection QScrollArea { background: transparent; border: none; }"
        "QGroupBox#labSection QScrollArea > QWidget > QWidget,"
        " QGroupBox#labSection QTabWidget > QStackedWidget,"
        " QGroupBox#labSection QTabWidget > QStackedWidget > QWidget { background: transparent; }"
        "QGroupBox#labSection QTabBar { background: transparent; }"
        "QGroupBox#labSection QTabWidget::pane { border: none; border-top: 1px solid @border@; }"
        "QFrame#labStat { background-color: @surfaceAlt@; border-radius: 6px; }"
        "QToolButton#labAddBlock { border: 1px dashed @borderStrong@; border-radius: 6px;"
        " padding: 10px 14px; color: @textSecondary@; background: transparent; }"
        "QToolButton#labAddBlock:hover { border-color: @accent@; color: @textPrimary@; }"
        "QToolButton#labAddBlock::menu-indicator { image: none; }"
        "QPushButton[labPrimary=\"true\"] { background-color: @accent@;"
        " border: 1px solid @accent@; color: #FFFFFF; font-weight: 600; padding: 6px 16px; }"
        "QPushButton[labPrimary=\"true\"]:hover { background-color: @accentHover@;"
        " border-color: @accentHover@; color: #FFFFFF; }"
        "QPushButton[labPrimary=\"true\"]:pressed { background-color: @accentPressed@;"
        " border-color: @accentPressed@; color: #FFFFFF; }"
        "QPushButton[labPrimary=\"true\"]:disabled { background-color: @surfaceAlt@;"
        " border-color: @border@; color: @textDisabled@; }"
        "QPushButton[labPrimary=\"true\"][labMenu=\"true\"] { padding-right: 28px; }"
        "QPushButton[labPrimary=\"true\"]::menu-indicator { subcontrol-origin: padding;"
        " subcontrol-position: right center; right: 8px; }");
    const QPair<const char*, QColor> colors[] = {
        {"@textSecondary@", palette.textSecondary}, {"@textPrimary@", palette.textPrimary},
        {"@textDisabled@", palette.textDisabled},   {"@surfaceAlt@", palette.surfaceAlt},
        {"@surface@", palette.surface},             {"@borderStrong@", palette.borderStrong},
        {"@border@", palette.border},               {"@accentHover@", palette.accentHover},
        {"@accentPressed@", palette.accentPressed}, {"@accent@", palette.accent},
    };
    for (const auto& [key, color] : colors) {
        sheet.replace(QLatin1String(key), css(color));
    }
    return sheet;
}

QLabel* note(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("labNote"));
    label->setWordWrap(true);
    return label;
}

QLabel* fieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("labFieldLabel"));
    return label;
}

void makePrimary(QWidget* button) {
    button->setProperty("labPrimary", true);
}

QVBoxLayout* categoryPage(QWidget* page, const QString& title, const QString& description,
                          QLabel** descriptionLabel) {
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kPageMarginH, kPageMarginV, kPageMarginH, kPageMarginV);
    layout->setSpacing(kPageSpacing);
    auto* titleLabel = new QLabel(QStringLiteral("<h2>%1</h2>").arg(title.toHtmlEscaped()), page);
    titleLabel->setTextFormat(Qt::RichText);
    layout->addWidget(titleLabel);
    auto* descriptionText = new QLabel(description, page);
    descriptionText->setObjectName(QStringLiteral("settingsDescription"));
    descriptionText->setWordWrap(true);
    layout->addWidget(descriptionText);
    if (descriptionLabel) {
        *descriptionLabel = descriptionText;
    }
    return layout;
}

}  // namespace ui

LabSection::LabSection(const QString& title, QWidget* parent) : QGroupBox(title, parent) {
    setObjectName(QStringLiteral("labSection"));
    m_body = new QVBoxLayout(this);
    m_body->setContentsMargins(14, 12, 14, 12);
    m_body->setSpacing(10);
}

LabStatusLine::LabStatusLine(QWidget* parent) : QLabel(parent) {
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &LabStatusLine::applyColor);
    applyColor();
}

void LabStatusLine::setStatus(const QString& text, int tone) {
    const QString shown = text.isEmpty() ? QString() : QStringLiteral("●  ") + text;
    if (shown != this->text()) {
        setText(shown);
    }
    if (qBound(0, tone, 3) != m_tone) {
        m_tone = qBound(0, tone, 3);
        applyColor();
    }
}

void LabStatusLine::applyColor() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    const QColor tones[] = {palette.textSecondary, palette.success, palette.warning, palette.danger};
    setStyleSheet(QStringLiteral("color: %1;").arg(tones[m_tone].name()));
}

}  // namespace tracelab
