#include "paletteeditordialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "traceview/appearance.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// Tokens in the order the editor lists them, grouped. A series color is
// the token "series:<index>".
struct TokenGroup {
    const char* title;
    QVector<QPair<const char*, const char*>> tokens;  // (token, label)
};

QVector<TokenGroup> tokenGroups() {
    return {
        {QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Surfaces"),
         {{"background", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Background")},
          {"surface", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Cards and panels")},
          {"surfaceAlt",
           QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Headers and inputs")},
          {"border", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Borders")},
          {"borderStrong",
           QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Strong borders")}}},
        {QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Text"),
         {{"textPrimary", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Primary text")},
          {"textSecondary",
           QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Secondary text")},
          {"textDisabled",
           QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Disabled text")}}},
        {QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Accent"),
         {{"accent", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Accent")},
          {"accentHover", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Accent (hover)")},
          {"accentPressed",
           QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Accent (pressed)")}}},
        {QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Status"),
         {{"success", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Success")},
          {"warning", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Warning")},
          {"danger", QT_TRANSLATE_NOOP("traceview::PaletteEditorDialog", "Danger")}}},
    };
}

QColor ThemePalette::*memberFor(const QString& token) {
    static const QHash<QString, QColor ThemePalette::*> members = {
        {"background", &ThemePalette::background},
        {"surface", &ThemePalette::surface},
        {"surfaceAlt", &ThemePalette::surfaceAlt},
        {"border", &ThemePalette::border},
        {"borderStrong", &ThemePalette::borderStrong},
        {"textPrimary", &ThemePalette::textPrimary},
        {"textSecondary", &ThemePalette::textSecondary},
        {"textDisabled", &ThemePalette::textDisabled},
        {"accent", &ThemePalette::accent},
        {"accentHover", &ThemePalette::accentHover},
        {"accentPressed", &ThemePalette::accentPressed},
        {"success", &ThemePalette::success},
        {"warning", &ThemePalette::warning},
        {"danger", &ThemePalette::danger},
    };
    return members.value(token, nullptr);
}

constexpr int kSeriesCount = 6;
// WCAG AA for normal-size text.
constexpr double kMinTextContrast = 4.5;

QIcon swatchIcon(const QColor& color) {
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(128, 128, 128), 1));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 3, 3);
    return QIcon(pixmap);
}

}  // namespace

PaletteEditorDialog::PaletteEditorDialog(const ThemePalette& palette, bool isNew,
                                         QWidget* parent)
    : QDialog(parent),
      m_palette(palette),
      m_original(palette),
      m_previousThemeId(ThemeManager::instance().currentTheme().id),
      m_isNew(isNew) {
    setWindowTitle(isNew ? tr("New Palette") : tr("Edit Palette"));
    while (m_palette.series.size() < kSeriesCount) {
        m_palette.series.append(m_palette.accent);
    }

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);

    auto* nameForm = new QFormLayout;
    m_nameEdit = new QLineEdit(m_palette.displayName, content);
    nameForm->addRow(tr("Name"), m_nameEdit);
    contentLayout->addLayout(nameForm);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_palette.displayName = text.trimmed().isEmpty() ? m_original.displayName : text.trimmed();
    });

    auto addButton = [this](QFormLayout* form, const QString& token, const QString& label) {
        auto* button = new QPushButton(form->parentWidget());
        button->setFocusPolicy(Qt::StrongFocus);
        connect(button, &QPushButton::clicked, this, [this, token]() { pickColor(token); });
        m_buttons.insert(token, button);
        form->addRow(label, button);
    };

    for (const TokenGroup& group : tokenGroups()) {
        auto* box = new QGroupBox(tr(group.title), content);
        auto* form = new QFormLayout(box);
        for (const auto& [token, label] : group.tokens) {
            addButton(form, QString::fromLatin1(token), tr(label));
        }
        contentLayout->addWidget(box);
    }

    auto* seriesBox = new QGroupBox(tr("Data series"), content);
    auto* seriesForm = new QFormLayout(seriesBox);
    for (int i = 0; i < kSeriesCount; ++i) {
        addButton(seriesForm, QStringLiteral("series:%1").arg(i), tr("Series %1").arg(i + 1));
    }
    contentLayout->addWidget(seriesBox);

    m_contrastLabel = new QLabel(content);
    m_contrastLabel->setWordWrap(true);
    contentLayout->addWidget(m_contrastLabel);
    contentLayout->addStretch();

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PaletteEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PaletteEditorDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);
    resize(420, 640);

    refreshButtons();
    refreshContrast();
    preview();
}

QColor PaletteEditorDialog::colorOf(const QString& token) const {
    if (token.startsWith(QLatin1String("series:"))) {
        return m_palette.series.value(token.mid(7).toInt());
    }
    const auto member = memberFor(token);
    return member ? m_palette.*member : QColor();
}

void PaletteEditorDialog::setColorOf(const QString& token, const QColor& color) {
    if (token.startsWith(QLatin1String("series:"))) {
        const int index = token.mid(7).toInt();
        if (index >= 0 && index < m_palette.series.size()) {
            m_palette.series[index] = color;
        }
        return;
    }
    if (const auto member = memberFor(token)) {
        m_palette.*member = color;
    }
}

void PaletteEditorDialog::pickColor(const QString& token) {
    // Borders are usually translucent over the surface below them.
    const QColor picked = QColorDialog::getColor(colorOf(token), this, tr("Choose Color"),
                                                 QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) {
        return;
    }
    setColorOf(token, picked);
    refreshButtons();
    refreshContrast();
    preview();
}

void PaletteEditorDialog::refreshButtons() {
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it) {
        const QColor color = colorOf(it.key());
        it.value()->setIcon(swatchIcon(color));
        it.value()->setText(color.alpha() < 255 ? color.name(QColor::HexArgb).toUpper()
                                                : color.name().toUpper());
    }
}

void PaletteEditorDialog::refreshContrast() {
    struct Pair {
        QString what;
        QColor text;
        QColor background;
    };
    const QVector<Pair> pairs = {
        {tr("Primary text on background"), m_palette.textPrimary, m_palette.background},
        {tr("Primary text on cards"), m_palette.textPrimary, m_palette.surface},
        {tr("Secondary text on cards"), m_palette.textSecondary, m_palette.surface},
    };
    QStringList warnings;
    for (const Pair& pair : pairs) {
        const double ratio = contrastRatio(pair.text, pair.background);
        if (ratio < kMinTextContrast) {
            warnings << tr("%1: %2:1 (at least 4.5:1 recommended)")
                            .arg(pair.what)
                            .arg(ratio, 0, 'f', 1);
        }
    }
    m_contrastLabel->setText(warnings.isEmpty()
                                 ? tr("Text contrast is good.")
                                 : tr("Low contrast, text may be hard to read:\n%1")
                                       .arg(warnings.join(QLatin1Char('\n'))));
}

void PaletteEditorDialog::preview() {
    ThemeManager& theme = ThemeManager::instance();
    theme.saveCustomTheme(m_palette);
    if (theme.currentTheme().id != m_palette.id) {
        theme.setTheme(m_palette.id);
    }
}

void PaletteEditorDialog::accept() {
    m_done = true;
    preview();  // the name may have changed since the last color pick
    QDialog::accept();
}

void PaletteEditorDialog::reject() {
    if (!m_done) {
        m_done = true;
        ThemeManager& theme = ThemeManager::instance();
        if (m_isNew) {
            theme.removeCustomTheme(m_palette.id);
            theme.setTheme(m_previousThemeId);
        } else {
            theme.saveCustomTheme(m_original);
        }
    }
    QDialog::reject();
}

}  // namespace traceview
