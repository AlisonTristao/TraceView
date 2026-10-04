#include "labui.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>

#include "traceview/thememanager.h"

namespace tracelab {

namespace ui {

namespace {

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
        " QLabel#labSectionTitle, QLabel#labStatCaption { color: @textSecondary@; }"
        "QFrame#labSection { background-color: @surface@; border: 1px solid @border@;"
        " border-radius: 8px; }"
        // Inside a section: one frame, no boxes within boxes.
        "QFrame#labSection QListWidget, QFrame#labSection QPlainTextEdit,"
        " QFrame#labSection QScrollArea { background: transparent; border: none; }"
        "QFrame#labSection QScrollArea > QWidget > QWidget,"
        " QFrame#labSection QTabWidget > QStackedWidget,"
        " QFrame#labSection QTabWidget > QStackedWidget > QWidget { background: transparent; }"
        "QFrame#labSection QTabBar { background: transparent; }"
        "QFrame#labSection QTabWidget::pane { border: none; border-top: 1px solid @border@; }"
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

}  // namespace ui

namespace {

QFont scaled(QFont font, double factor, bool bold) {
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * factor));
    }
    font.setBold(bold);
    return font;
}

}  // namespace

LabSection::LabSection(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("labSection"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 12);
    layout->setSpacing(8);
    m_titleRow = new QHBoxLayout;
    m_titleRow->setSpacing(8);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("labSectionTitle"));
    QFont font = scaled(m_title->font(), 0.9, true);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 106);
    m_title->setFont(font);
    m_titleRow->addWidget(m_title);
    m_titleRow->addStretch();
    layout->addLayout(m_titleRow);
    m_body = new QVBoxLayout;
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(8);
    layout->addLayout(m_body, 1);
    setTitle(title);
}

void LabSection::setTitle(const QString& title) {
    m_title->setText(title);
}

LabPageHeader::LabPageHeader(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto* who = new QVBoxLayout;
    who->setSpacing(2);
    m_kind = new QLabel(this);
    m_kind->setObjectName(QStringLiteral("labKind"));
    QFont kindFont = scaled(m_kind->font(), 0.85, true);
    kindFont.setCapitalization(QFont::AllUppercase);
    kindFont.setLetterSpacing(QFont::PercentageSpacing, 108);
    m_kind->setFont(kindFont);
    who->addWidget(m_kind);
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(12);
    m_title = new QLabel(this);
    m_title->setFont(scaled(m_title->font(), 1.8, true));
    titleRow->addWidget(m_title);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    titleRow->addWidget(m_status, 0, Qt::AlignBottom);
    titleRow->addStretch();
    who->addLayout(titleRow);
    layout->addLayout(who, 1);
    m_extras = new QHBoxLayout;
    m_extras->setSpacing(8);
    layout->addLayout(m_extras);
    m_actions = new QHBoxLayout;
    m_actions->setSpacing(8);
    layout->addLayout(m_actions);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &LabPageHeader::applyStatusColor);
}

void LabPageHeader::setKind(const QString& text) {
    m_kind->setText(text);
}

void LabPageHeader::setTitle(const QString& text) {
    m_title->setText(text);
}

void LabPageHeader::setStatus(const QString& text, int tone) {
    m_status->setText(text.isEmpty() ? QString() : QStringLiteral("●  ") + text);
    m_tone = qBound(0, tone, 3);
    applyStatusColor();
}

void LabPageHeader::applyStatusColor() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    const QColor tones[] = {palette.textSecondary, palette.success, palette.warning, palette.danger};
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(tones[m_tone].name()));
}

void LabPageHeader::addExtra(QWidget* widget) {
    m_extras->addWidget(widget, 0, Qt::AlignVCenter);
}

void LabPageHeader::addAction(QWidget* widget) {
    m_actions->addWidget(widget, 0, Qt::AlignVCenter);
}

}  // namespace tracelab
