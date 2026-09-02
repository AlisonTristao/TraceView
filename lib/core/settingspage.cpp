#include "core/settingspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "preferences/appsettings.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
QWidget* createCategoryPage(QWidget* parent, const QString& title, const QString& description) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);

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
    section->setLayout(new QFormLayout(section));
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
    categories->setFixedWidth(170);
    categories->addItems({tr("General"), tr("Appearance"), tr("Dashboard"), tr("Terminal"),
                          tr("Connections"), tr("Diagnostics")});
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
