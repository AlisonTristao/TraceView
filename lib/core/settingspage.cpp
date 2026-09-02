#include "core/settingspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>

#include "preferences/appsettings.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// The category glyphs down the left of the page, hand-drawn (not font glyphs)
// in the same flat style as core/ribbonicons.cpp so they stay crisp at the
// list's small icon size and pick up the active theme's colour. Every glyph
// is laid out in an 18px design space.
constexpr int kCategoryIconSize = 18;
constexpr int kCategoryCount = 6;

// Draws a transparent 18px pixmap with an antialiased round-capped pen already
// set to `color`; `draw` fills in the glyph. Painter is torn down before the
// pixmap is returned so it is fully flushed.
QPixmap categoryPixmap(const QColor& color, const std::function<void(QPainter&)>& draw) {
    QPixmap pixmap(kCategoryIconSize, kCategoryIconSize);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, 1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        draw(painter);
    }
    return pixmap;
}

QPixmap categoryPixmapFor(int index, const QColor& color) {
    switch (index) {
        case 0:  // General -- three slider rails, each with a knob at a different offset.
            return categoryPixmap(color, [&](QPainter& p) {
                const double knobs[3] = {12.0, 6.0, 10.0};
                for (int i = 0; i < 3; ++i) {
                    const double y = 4.0 + i * 5.0;
                    p.drawLine(QPointF(2.5, y), QPointF(15.5, y));
                    p.setBrush(QBrush(color));
                    p.drawEllipse(QPointF(knobs[i], y), 2.1, 2.1);
                    p.setBrush(Qt::NoBrush);
                }
            });
        case 1:  // Appearance -- a circle with its right half filled.
            return categoryPixmap(color, [&](QPainter& p) {
                const QRectF disc(2.0, 2.0, 14.0, 14.0);
                p.setBrush(QBrush(color));
                p.drawPie(disc, -90 * 16, 180 * 16);
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(disc);
            });
        case 2:  // Dashboard -- a 2x2 grid, echoing a widget layout.
            return categoryPixmap(color, [](QPainter& p) {
                const double cell = 6.5;
                const double gap = 2.0;
                const double o = (kCategoryIconSize - (2 * cell + gap)) / 2.0;
                p.drawRoundedRect(QRectF(o, o, cell, cell), 1.0, 1.0);
                p.drawRoundedRect(QRectF(o + cell + gap, o, cell, cell), 1.0, 1.0);
                p.drawRoundedRect(QRectF(o, o + cell + gap, cell, cell), 1.0, 1.0);
                p.drawRoundedRect(QRectF(o + cell + gap, o + cell + gap, cell, cell), 1.0, 1.0);
            });
        case 3:  // Terminal -- a window outline with a prompt chevron and cursor underscore.
            return categoryPixmap(color, [](QPainter& p) {
                p.drawRoundedRect(QRectF(1.5, 3.0, 15.0, 12.0), 1.6, 1.6);
                QPolygonF chevron;
                chevron << QPointF(4.5, 7.5) << QPointF(6.7, 9.5) << QPointF(4.5, 11.5);
                p.drawPolyline(chevron);
                p.drawLine(QPointF(8.0, 11.5), QPointF(12.5, 11.5));
            });
        case 4:  // Connections -- two interlocking chain links.
            return categoryPixmap(color, [](QPainter& p) {
                p.drawRoundedRect(QRectF(1.5, 6.0, 10.0, 6.0), 3.0, 3.0);
                p.drawRoundedRect(QRectF(6.5, 6.0, 10.0, 6.0), 3.0, 3.0);
            });
        default:  // Diagnostics -- a heartbeat trace.
            return categoryPixmap(color, [](QPainter& p) {
                QPolygonF pulse;
                pulse << QPointF(1.5, 9.0) << QPointF(5.0, 9.0) << QPointF(7.0, 3.5)
                      << QPointF(9.0, 14.5) << QPointF(11.0, 9.0) << QPointF(16.5, 9.0);
                p.drawPolyline(pulse);
            });
    }
}

// Row order matches categoryNames / the QStackedWidget page order below. Each
// row carries two glyphs: `normal` for an idle row, `selected` for the one the
// accent fill sits behind (a QListWidget won't recolour a plain pixmap icon
// the way it does the row's text).
void applyCategoryIcons(QListWidget* list, const QColor& normal, const QColor& selected) {
    const int rows = qMin(list->count(), kCategoryCount);
    for (int i = 0; i < rows; ++i) {
        QIcon icon;
        icon.addPixmap(categoryPixmapFor(i, normal), QIcon::Normal);
        icon.addPixmap(categoryPixmapFor(i, selected), QIcon::Selected);
        list->item(i)->setIcon(icon);
    }
}

QWidget* createCategoryPage(QWidget* parent, const QString& title, const QString& description) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(20);

    auto* titleLabel = new QLabel(QString("<h2>%1</h2>").arg(title.toHtmlEscaped()), page);
    titleLabel->setTextFormat(Qt::RichText);
    layout->addWidget(titleLabel);

    auto* descriptionLabel = new QLabel(description, page);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setObjectName("settingsDescription");
    layout->addWidget(descriptionLabel);
    return page;
}

QGroupBox* addSection(QWidget* page, const QString& title) {
    auto* section = new QGroupBox(title, page);
    auto* form = new QFormLayout(section);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(section);
    return section;
}

QFormLayout* formFor(QGroupBox* section) {
    return qobject_cast<QFormLayout*>(section->layout());
}
}  // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    AppSettings& settings = AppSettings::instance();
    m_initialLanguageId = LanguageManager::instance().currentLanguage().id;
    m_initialFrameLogCapacity = settings.frameLogCapacity();
    m_initialNotificationHistoryCapacity = settings.notificationHistoryCapacity();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(12);

    auto* header = new QLabel(tr("Settings"), this);
    header->setObjectName("settingsTitle");
    QFont headerFont = header->font();
    headerFont.setPointSize(headerFont.pointSize() + 6);
    headerFont.setBold(true);
    header->setFont(headerFont);
    rootLayout->addWidget(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(18);
    auto* categories = new QListWidget(this);
    categories->setFixedWidth(186);
    categories->setIconSize(QSize(kCategoryIconSize, kCategoryIconSize));
    // The app-wide QListWidget::item rule only reserves 4px around the label;
    // with an icon in front of it that leaves the glyph hard against the
    // frame and the text crowding it. Widen the padding (and space the rows)
    // just for this navigation list.
    categories->setStyleSheet(QStringLiteral("QListWidget::item { padding: 7px 10px; }"));
    categories->setSpacing(3);
    const QStringList categoryNames = {tr("General"), tr("Appearance"), tr("Dashboard"),
                                       tr("Terminal"), tr("Connections"), tr("Diagnostics")};
    for (const QString& name : categoryNames) {
        auto* item = new QListWidgetItem(name, categories);
        item->setSizeHint(QSize(-1, 36));
    }

    const auto refreshCategoryIcons = [categories] {
        const ThemePalette& theme = ThemeManager::instance().currentTheme();
        applyCategoryIcons(categories, theme.textSecondary, theme.background);
    };
    refreshCategoryIcons();
    // The Appearance page's own theme combo can swap the palette while this
    // tab is open, so keep the glyphs in step (the switcher/ribbon do the same).
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [refreshCategoryIcons](const ThemePalette&) { refreshCategoryIcons(); });
    body->addWidget(categories);

    auto* pages = new QStackedWidget(this);
    body->addWidget(pages, 1);
    rootLayout->addLayout(body, 1);

    QWidget* generalPage = createCategoryPage(
        pages, tr("General"), tr("Project and startup preferences shared by the application."));
    QGroupBox* recentSection = addSection(generalPage, tr("Recent projects"));
    auto* recentLimit = new QSpinBox(recentSection);
    recentLimit->setRange(1, 50);
    recentLimit->setValue(settings.recentProjectsLimit());
    recentLimit->setSuffix(tr(" projects"));
    formFor(recentSection)->addRow(tr("Remember"), recentLimit);
    auto* clearRecentButton = new QPushButton(tr("Clear recent projects"), recentSection);
    formFor(recentSection)->addRow(QString(), clearRecentButton);
    connect(recentLimit, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setRecentProjectsLimit);
    connect(clearRecentButton, &QPushButton::clicked, this,
            &SettingsPage::clearRecentProjectsRequested);

    QGroupBox* startupSection = addSection(generalPage, tr("Project startup"));
    auto* autoConnect =
        new QCheckBox(tr("Connect configured devices when a project opens"), startupSection);
    autoConnect->setChecked(settings.autoConnectOnProjectOpen());
    formFor(startupSection)->addRow(autoConnect);
    connect(autoConnect, &QCheckBox::toggled, &settings, &AppSettings::setAutoConnectOnProjectOpen);
    qobject_cast<QVBoxLayout*>(generalPage->layout())->addStretch();
    pages->addWidget(generalPage);

    QWidget* appearancePage =
        createCategoryPage(pages, tr("Appearance"),
                           tr("Theme and typeface changes apply immediately. Changing the language "
                              "is saved for the next start."));
    QGroupBox* appearanceSection = addSection(appearancePage, tr("Application appearance"));
    auto* themeCombo = new QComboBox(appearanceSection);
    const QVector<ThemePalette> themes = ThemeManager::instance().availableThemes();
    for (const ThemePalette& theme : themes) {
        themeCombo->addItem(theme.displayName, theme.id);
        if (theme.id == ThemeManager::instance().currentTheme().id) {
            themeCombo->setCurrentIndex(themeCombo->count() - 1);
        }
    }
    formFor(appearanceSection)->addRow(tr("Theme"), themeCombo);
    connect(themeCombo, &QComboBox::currentIndexChanged, this, [themeCombo](int index) {
        ThemeManager::instance().setTheme(themeCombo->itemData(index).toString());
    });

    auto* fontCombo = new QComboBox(appearanceSection);
    const QVector<FontOption> fonts = FontManager::instance().availableFonts();
    for (const FontOption& font : fonts) {
        fontCombo->addItem(font.displayName, font.id);
        if (font.id == FontManager::instance().currentFont().id) {
            fontCombo->setCurrentIndex(fontCombo->count() - 1);
        }
    }
    formFor(appearanceSection)->addRow(tr("Interface font"), fontCombo);
    connect(fontCombo, &QComboBox::currentIndexChanged, this, [fontCombo](int index) {
        FontManager::instance().setFont(fontCombo->itemData(index).toString());
    });

    auto* languageCombo = new QComboBox(appearanceSection);
    const QVector<LanguageInfo> languages = LanguageManager::instance().availableLanguages();
    for (const LanguageInfo& language : languages) {
        languageCombo->addItem(language.displayName, language.id);
        if (language.id == m_initialLanguageId) {
            languageCombo->setCurrentIndex(languageCombo->count() - 1);
        }
    }
    formFor(appearanceSection)->addRow(tr("Language"), languageCombo);
    connect(languageCombo, &QComboBox::currentIndexChanged, this, [this, languageCombo](int index) {
        LanguageManager::instance().setLanguage(languageCombo->itemData(index).toString());
        refreshRestartNotice();
    });
    qobject_cast<QVBoxLayout*>(appearancePage->layout())->addStretch();
    pages->addWidget(appearancePage);

    QWidget* dashboardPage =
        createCategoryPage(pages, tr("Dashboard"),
                           tr("Rendering caps redraws only; telemetry samples continue to be "
                              "recorded at their requested rate."));
    QGroupBox* renderingSection = addSection(dashboardPage, tr("Rendering quality"));
    auto* profileCombo = new QComboBox(renderingSection);
    profileCombo->addItem(tr("Low (15 FPS)"), int(AppSettings::RenderProfile::Low));
    profileCombo->addItem(tr("Medium (30 FPS)"), int(AppSettings::RenderProfile::Medium));
    profileCombo->addItem(tr("High (60 FPS)"), int(AppSettings::RenderProfile::High));
    profileCombo->addItem(tr("Custom"), int(AppSettings::RenderProfile::Custom));
    profileCombo->setCurrentIndex(int(settings.renderProfile()));
    formFor(renderingSection)->addRow(tr("Profile"), profileCombo);
    auto* customFps = new QSpinBox(renderingSection);
    customFps->setRange(1, 240);
    customFps->setValue(settings.customRenderFps());
    customFps->setSuffix(tr(" FPS"));
    customFps->setEnabled(settings.renderProfile() == AppSettings::RenderProfile::Custom);
    formFor(renderingSection)->addRow(tr("Custom rate"), customFps);
    connect(profileCombo, &QComboBox::currentIndexChanged, this,
            [profileCombo, customFps, &settings](int index) {
                const auto profile =
                    static_cast<AppSettings::RenderProfile>(profileCombo->itemData(index).toInt());
                settings.setRenderProfile(profile);
                customFps->setEnabled(profile == AppSettings::RenderProfile::Custom);
            });
    connect(customFps, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setCustomRenderFps);
    qobject_cast<QVBoxLayout*>(dashboardPage->layout())->addStretch();
    pages->addWidget(dashboardPage);

    QWidget* terminalPage = createCategoryPage(
        pages, tr("Terminal"),
        tr("These controls are applied to all open serial terminal widgets immediately."));
    QGroupBox* terminalSection = addSection(terminalPage, tr("Terminal display"));
    auto* scrollback = new QSpinBox(terminalSection);
    scrollback->setRange(100, 100000);
    scrollback->setSingleStep(100);
    scrollback->setValue(settings.terminalScrollbackLines());
    scrollback->setSuffix(tr(" lines"));
    formFor(terminalSection)->addRow(tr("Scrollback limit"), scrollback);
    auto* wordWrap = new QCheckBox(tr("Wrap long lines"), terminalSection);
    wordWrap->setChecked(settings.terminalWordWrap());
    formFor(terminalSection)->addRow(wordWrap);
    auto* autoScroll = new QCheckBox(tr("Follow new output"), terminalSection);
    autoScroll->setChecked(settings.terminalAutoScroll());
    formFor(terminalSection)->addRow(autoScroll);
    auto* cursorBlink = new QCheckBox(tr("Blink remote cursor"), terminalSection);
    cursorBlink->setChecked(settings.terminalCursorBlink());
    formFor(terminalSection)->addRow(cursorBlink);
    connect(scrollback, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setTerminalScrollbackLines);
    connect(wordWrap, &QCheckBox::toggled, &settings, &AppSettings::setTerminalWordWrap);
    connect(autoScroll, &QCheckBox::toggled, &settings, &AppSettings::setTerminalAutoScroll);
    connect(cursorBlink, &QCheckBox::toggled, &settings, &AppSettings::setTerminalCursorBlink);
    qobject_cast<QVBoxLayout*>(terminalPage->layout())->addStretch();
    pages->addWidget(terminalPage);

    QWidget* connectionsPage =
        createCategoryPage(pages, tr("Connections"),
                           tr("Controls how TraceView restores and retries device connections."));
    QGroupBox* reconnectSection = addSection(connectionsPage, tr("Reconnect"));
    auto* autoReconnect =
        new QCheckBox(tr("Retry disconnected devices automatically"), reconnectSection);
    autoReconnect->setChecked(settings.autoReconnect());
    formFor(reconnectSection)->addRow(autoReconnect);
    auto* reconnectDelay = new QSpinBox(reconnectSection);
    reconnectDelay->setRange(1, 60);
    reconnectDelay->setValue(settings.reconnectIntervalSeconds());
    reconnectDelay->setSuffix(tr(" seconds"));
    formFor(reconnectSection)->addRow(tr("Retry interval"), reconnectDelay);
    connect(autoReconnect, &QCheckBox::toggled, &settings, &AppSettings::setAutoReconnect);
    connect(reconnectDelay, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setReconnectIntervalSeconds);
    qobject_cast<QVBoxLayout*>(connectionsPage->layout())->addStretch();
    pages->addWidget(connectionsPage);

    QWidget* diagnosticsPage =
        createCategoryPage(pages, tr("Diagnostics"),
                           tr("Keep enough history for investigation without letting long-running "
                              "sessions use unbounded memory."));
    QGroupBox* diagnosticsSection = addSection(diagnosticsPage, tr("In-memory history"));
    auto* frameHistory = new QSpinBox(diagnosticsSection);
    frameHistory->setRange(100, 50000);
    frameHistory->setSingleStep(100);
    frameHistory->setValue(settings.frameLogCapacity());
    frameHistory->setSuffix(tr(" frames"));
    formFor(diagnosticsSection)->addRow(tr("BTP traffic history"), frameHistory);
    auto* notificationHistory = new QSpinBox(diagnosticsSection);
    notificationHistory->setRange(100, 10000);
    notificationHistory->setSingleStep(100);
    notificationHistory->setValue(settings.notificationHistoryCapacity());
    notificationHistory->setSuffix(tr(" messages"));
    formFor(diagnosticsSection)->addRow(tr("Notification history"), notificationHistory);
    connect(frameHistory, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, &settings](int value) {
                settings.setFrameLogCapacity(value);
                refreshRestartNotice();
            });
    connect(notificationHistory, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, &settings](int value) {
                settings.setNotificationHistoryCapacity(value);
                refreshRestartNotice();
            });
    qobject_cast<QVBoxLayout*>(diagnosticsPage->layout())->addStretch();
    pages->addWidget(diagnosticsPage);

    connect(categories, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    categories->setCurrentRow(0);

    auto* restartRow = new QHBoxLayout;
    m_restartNotice = new QLabel(this);
    m_restartNotice->setWordWrap(true);
    restartRow->addWidget(m_restartNotice, 1);
    m_restartButton = new QPushButton(tr("Restart now"), this);
    restartRow->addWidget(m_restartButton);
    rootLayout->addLayout(restartRow);
    connect(m_restartButton, &QPushButton::clicked, this, &SettingsPage::restartRequested);
    refreshRestartNotice();
}

void SettingsPage::refreshRestartNotice() {
    const AppSettings& settings = AppSettings::instance();
    const bool restartRequired =
        LanguageManager::instance().currentLanguage().id != m_initialLanguageId ||
        settings.frameLogCapacity() != m_initialFrameLogCapacity ||
        settings.notificationHistoryCapacity() != m_initialNotificationHistoryCapacity;
    m_restartNotice->setText(
        restartRequired ? tr("Restart TraceView to apply language or diagnostics history changes.")
                        : tr("Changes apply immediately unless noted otherwise."));
    m_restartButton->setEnabled(restartRequired);
}

}  // namespace traceview
