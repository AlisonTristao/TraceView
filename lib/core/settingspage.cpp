#include "core/settingspage.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

#include "agent/agentbridgeserver.h"
#include "appearancecatalog.h"
#include "core/applog.h"
#include "dashboard/widgets/chartstyle.h"
#include "preferences/appsettings.h"
#include "protocol/manifeststore.h"
#include "theme/dialogchrome.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// The category list is a NavSidebar (theme/dialogchrome.h), same as the
// TraceKit panel's: Lucide glyphs, tinted from the palette, icon-only in
// compact mode.
constexpr int kCategoryCount = 7;
// Same threshold as MainWindow's own kSmallBreakpointMaxViewportWidth
// (mainwindow.cpp) -- this page has no access to that constant or to
// DashboardGrid's breakpoint machinery (it's a standalone widget, not part
// of the Dashboard tab), so it re-derives "phone-narrow" from its own width
// instead. Kept in sync deliberately: below this, the fixed-width category
// sidebar plus a side-by-side QFormLayout leaves the actual settings
// values too cramped to read or tap accurately.
constexpr int kCompactLayoutMaxWidth = 700;
// Root margins come from DialogChrome::kMargins on desktop; on a phone every
// pixel of horizontal margin is taken straight out of the combo/spin box
// width, so compact mode trims them hard.
constexpr int kCompactRootMargin = 8;
constexpr int kCompactBodySpacing = 8;
// Inside each page's scroll area: a little room from the side list and the
// scrollbar, not a second full dialog margin.
constexpr int kFullPageMarginH = 8;
constexpr int kFullPageMarginV = 4;
constexpr int kCompactPageMarginH = 4;
constexpr int kCompactPageMarginV = 4;
// Every combo/spin box shares one fixed width, taken from the Appearance
// page's combos (clamped to this range) so fields don't vary page to page.
constexpr int kMinFieldWidth = 180;
constexpr int kMaxFieldWidth = 260;
// Group box frame + inner margins + a vertical scrollbar's width -- the
// horizontal space a field row loses beyond the page's own margins.
constexpr int kFieldRowChrome = 40;

QWidget* createCategoryPage(QWidget* parent, const QString& title, const QString& description) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kFullPageMarginH, kFullPageMarginV, kFullPageMarginH,
                               kFullPageMarginV);
    layout->setSpacing(DialogChrome::kSectionSpacing);
    layout->addWidget(new DialogHeader(title, description, page, DialogHeader::Level::Page));
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

// A word-wrapping label that toggles its checkbox when clicked, standing in
// for QCheckBox's own text (which never wraps and so pushes long options
// like "Connect configured devices when a project opens" off a phone screen).
class CheckBoxLabel final : public QLabel {
public:
    CheckBoxLabel(const QString& text, QCheckBox* target, QWidget* parent)
        : QLabel(text, parent), m_target(target) {
        setWordWrap(true);
        setToolTip(target->toolTip());
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) &&
            m_target->isEnabled()) {
            m_target->toggle();
        }
        QLabel::mouseReleaseEvent(event);
    }

private:
    QCheckBox* m_target;
};

void addCheckBoxRow(QFormLayout* form, QCheckBox* checkBox) {
    auto* row = new QWidget(checkBox->parentWidget());
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* label = new CheckBoxLabel(checkBox->text(), checkBox, row);
    checkBox->setText(QString());
    checkBox->setParent(row);
    layout->addWidget(checkBox, 0, Qt::AlignTop);
    layout->addWidget(label, 1);
    form->addRow(row);
}

// A file path has no spaces to break at, so a word-wrapping QLabel can't
// wrap it and it overflows narrow screens. Zero-width spaces after each
// separator give it break points without changing how it looks.
QString breakablePath(const QString& path) {
    QString result;
    result.reserve(path.size() * 2);
    for (const QChar ch : path) {
        result.append(ch);
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\')) {
            result.append(QChar(0x200B));
        }
    }
    return result;
}
}  // namespace

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    AppSettings& settings = AppSettings::instance();
    m_initialLanguageId = LanguageManager::instance().currentLanguage().id;
    m_initialFrameLogCapacity = settings.frameLogCapacity();
    m_initialNotificationHistoryCapacity = settings.notificationHistoryCapacity();

    QVBoxLayout* rootLayout = DialogChrome::setupRootLayout(this);
    m_rootLayout = rootLayout;

    m_header = new DialogHeader(tr("Settings"), QString(), this);
    rootLayout->addWidget(m_header);

    auto* body = new QHBoxLayout;
    m_bodyLayout = body;
    body->setSpacing(DialogChrome::kBodySpacing);
    auto* categories = new NavSidebar(this);
    m_categoryList = categories;
    // Row order matches the QStackedWidget page order below.
    categories->addPage(tr("General"), QStringLiteral("lucide:settings"));
    categories->addPage(tr("Appearance"), QStringLiteral("lucide:palette"));
    categories->addPage(tr("Dashboard"), QStringLiteral("lucide:layout-dashboard"));
    categories->addPage(tr("Terminal"), QStringLiteral("lucide:terminal"));
    categories->addPage(tr("Connections"), QStringLiteral("lucide:cable"));
    categories->addPage(tr("Diagnostics"), QStringLiteral("lucide:activity"));
    categories->addPage(tr("Updates"), QStringLiteral("lucide:refresh-cw"));
    Q_ASSERT(categories->count() == kCategoryCount);
    body->addWidget(categories);

    auto* pages = new QStackedWidget(this);
    body->addWidget(pages, 1);
    // Each category page goes into its own vertical-only scroll area. In
    // compact mode WrapAllRows roughly doubles every form's height, and on a
    // phone-height screen a non-scrolling page would otherwise have its
    // combos/spin boxes squeezed below a readable height to make it fit.
    const auto addPage = [this, pages](QWidget* page) {
        auto* scroll = new QScrollArea(pages);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Let the page's own themed background show through instead of the
        // viewport's default palette fill.
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setWidget(page);
        m_pageLayouts.append(qobject_cast<QVBoxLayout*>(page->layout()));
        pages->addWidget(scroll);
    };
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
    addCheckBoxRow(formFor(startupSection), autoConnect);
    connect(autoConnect, &QCheckBox::toggled, &settings, &AppSettings::setAutoConnectOnProjectOpen);
    qobject_cast<QVBoxLayout*>(generalPage->layout())->addStretch();
    addPage(generalPage);

    QWidget* appearancePage =
        createCategoryPage(pages, tr("Appearance"),
                           tr("Appearance changes apply immediately. Changing the language is "
                              "saved for the next start."));
    QGroupBox* appearanceSection = addSection(appearancePage, tr("Application appearance"));

    // A preset applies a whole look at once; the combo shows the preset the
    // current appearance matches, if any.
    auto* presetCombo = new QComboBox(appearanceSection);
    auto syncPresetCombo = [presetCombo]() {
        const QSignalBlocker blocker(presetCombo);
        presetCombo->clear();
        presetCombo->addItem(tr("Custom"), QString());
        const AppearanceSnapshot current = currentAppearance();
        for (const AppearancePreset& preset : appearancePresets()) {
            presetCombo->addItem(preset.name, preset.id);
            if (preset.values == current) {
                presetCombo->setCurrentIndex(presetCombo->count() - 1);
            }
        }
    };
    syncPresetCombo();
    formFor(appearanceSection)->addRow(tr("Preset"), presetCombo);
    connect(presetCombo, &QComboBox::currentIndexChanged, this, [presetCombo](int index) {
        const QString id = presetCombo->itemData(index).toString();
        for (const AppearancePreset& preset : appearancePresets()) {
            if (preset.id == id) {
                applyAppearance(preset.values);
            }
        }
    });
    connectAppearanceChanged(presetCombo, syncPresetCombo);

    // One row per appearance option (core/appearancecatalog.h), the same
    // list the View menu is built from.
    for (const AppearanceOption& option : appearanceOptions()) {
        auto* combo = new QComboBox(appearanceSection);
        const QString key = option.key;
        auto sync = [combo, key]() {
            for (const AppearanceOption& current : appearanceOptions()) {
                if (current.key != key) {
                    continue;
                }
                const QSignalBlocker blocker(combo);
                combo->clear();
                for (const AppearanceChoice& choice : current.choices()) {
                    combo->addItem(choice.label, choice.id);
                }
                combo->setCurrentIndex(qMax(0, combo->findData(current.current())));
            }
        };
        sync();
        formFor(appearanceSection)->addRow(option.label, combo);
        const auto apply = option.apply;
        connect(combo, &QComboBox::currentIndexChanged, this,
                [combo, apply](int index) { apply(combo->itemData(index).toString()); });
        connectAppearanceChanged(combo, sync);
    }

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
    addPage(appearancePage);

    QWidget* dashboardPage =
        createCategoryPage(pages, tr("Dashboard"),
                           tr("Rendering caps redraws only; telemetry samples continue to be "
                              "recorded at their requested rate."));
    QGroupBox* renderingSection = addSection(dashboardPage, tr("Rendering quality"));
    auto* profileCombo = new QComboBox(renderingSection);
    profileCombo->addItem(tr("Low (30 FPS)"), int(AppSettings::RenderProfile::Low));
    profileCombo->addItem(tr("Medium (60 FPS)"), int(AppSettings::RenderProfile::Medium));
    profileCombo->addItem(tr("High (120 FPS)"), int(AppSettings::RenderProfile::High));
    profileCombo->addItem(tr("Extra High (240 FPS)"), int(AppSettings::RenderProfile::ExtraHigh));
    profileCombo->addItem(tr("Custom"), int(AppSettings::RenderProfile::Custom));
    profileCombo->setCurrentIndex(profileCombo->findData(int(settings.renderProfile())));
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

    QGroupBox* subscribeRateSection = addSection(dashboardPage, tr("Telemetry subscribe rate"));
    auto* subscribeRateOverride =
        new QCheckBox(tr("Override every widget's requested rate"), subscribeRateSection);
    subscribeRateOverride->setChecked(settings.subscribeRateOverrideEnabled());
    addCheckBoxRow(formFor(subscribeRateSection), subscribeRateOverride);
    auto* subscribeRateHz = new QSpinBox(subscribeRateSection);
    subscribeRateHz->setRange(1, 1000);
    subscribeRateHz->setValue(settings.subscribeRateOverrideHz());
    subscribeRateHz->setSuffix(tr(" Hz"));
    subscribeRateHz->setEnabled(settings.subscribeRateOverrideEnabled());
    subscribeRateHz->setToolTip(
        tr("Applied to every chart, gauge and text board on this dashboard, in place of each "
           "widget's own sample time. Each topic still clamps it to its own max/min rate."));
    formFor(subscribeRateSection)->addRow(tr("Rate"), subscribeRateHz);
    connect(subscribeRateOverride, &QCheckBox::toggled, &settings,
            &AppSettings::setSubscribeRateOverrideEnabled);
    connect(subscribeRateOverride, &QCheckBox::toggled, subscribeRateHz, &QSpinBox::setEnabled);
    connect(subscribeRateHz, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setSubscribeRateOverrideHz);

    qobject_cast<QVBoxLayout*>(dashboardPage->layout())->addStretch();
    addPage(dashboardPage);

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
    addCheckBoxRow(formFor(terminalSection), wordWrap);
    auto* autoScroll = new QCheckBox(tr("Follow new output"), terminalSection);
    autoScroll->setChecked(settings.terminalAutoScroll());
    addCheckBoxRow(formFor(terminalSection), autoScroll);
    auto* cursorBlink = new QCheckBox(tr("Blink remote cursor"), terminalSection);
    cursorBlink->setChecked(settings.terminalCursorBlink());
    addCheckBoxRow(formFor(terminalSection), cursorBlink);
    connect(scrollback, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setTerminalScrollbackLines);
    connect(wordWrap, &QCheckBox::toggled, &settings, &AppSettings::setTerminalWordWrap);
    connect(autoScroll, &QCheckBox::toggled, &settings, &AppSettings::setTerminalAutoScroll);
    connect(cursorBlink, &QCheckBox::toggled, &settings, &AppSettings::setTerminalCursorBlink);
    qobject_cast<QVBoxLayout*>(terminalPage->layout())->addStretch();
    addPage(terminalPage);

    QWidget* connectionsPage =
        createCategoryPage(pages, tr("Connections"),
                           tr("Controls how TraceView restores and retries device connections."));
    QGroupBox* reconnectSection = addSection(connectionsPage, tr("Reconnect"));
    auto* autoReconnect =
        new QCheckBox(tr("Retry disconnected devices automatically"), reconnectSection);
    autoReconnect->setChecked(settings.autoReconnect());
    addCheckBoxRow(formFor(reconnectSection), autoReconnect);
    auto* reconnectDelay = new QSpinBox(reconnectSection);
    reconnectDelay->setRange(1, 60);
    reconnectDelay->setValue(settings.reconnectIntervalSeconds());
    reconnectDelay->setSuffix(tr(" seconds"));
    formFor(reconnectSection)->addRow(tr("Retry interval"), reconnectDelay);
    connect(autoReconnect, &QCheckBox::toggled, &settings, &AppSettings::setAutoReconnect);
    connect(reconnectDelay, qOverload<int>(&QSpinBox::valueChanged), &settings,
            &AppSettings::setReconnectIntervalSeconds);

    QGroupBox* manifestSection = addSection(connectionsPage, tr("Manifest cache"));
    auto* manifestCache =
        new QCheckBox(tr("Remember each device's manifest between sessions"), manifestSection);
    manifestCache->setChecked(settings.manifestCacheEnabled());
    manifestCache->setToolTip(
        tr("A reconnect then only asks whether the device's catalog changed, and the topics "
           "of a known device are listed even while it is offline."));
    addCheckBoxRow(formFor(manifestSection), manifestCache);
    auto* skipOnHello = new QCheckBox(
        tr("Trust the cache without asking when the device reports the same revision "
           "(TCP/BLE)"),
        manifestSection);
    skipOnHello->setChecked(settings.manifestCacheSkipOnHello());
    skipOnHello->setEnabled(manifestCache->isChecked());
    addCheckBoxRow(formFor(manifestSection), skipOnHello);
    auto* skipOnHelloNote = new QLabel(
        tr("Not recommended: the question it skips is tiny, and it is what keeps the device's "
           "reported information (firmware version and the like) up to date."),
        manifestSection);
    skipOnHelloNote->setWordWrap(true);
    skipOnHelloNote->setEnabled(manifestCache->isChecked());
    formFor(manifestSection)->addRow(skipOnHelloNote);
    auto* clearManifests = new QPushButton(manifestSection);
    const auto refreshClearManifests = [clearManifests] {
        const int stored = ManifestStore::instance().count();
        clearManifests->setText(tr("Clear cache (%n device(s))", "", stored));
        clearManifests->setEnabled(stored > 0);
    };
    refreshClearManifests();
    formFor(manifestSection)->addRow(QString(), clearManifests);
    connect(manifestCache, &QCheckBox::toggled, &settings, &AppSettings::setManifestCacheEnabled);
    connect(manifestCache, &QCheckBox::toggled, skipOnHello, &QCheckBox::setEnabled);
    connect(manifestCache, &QCheckBox::toggled, skipOnHelloNote, &QLabel::setEnabled);
    connect(skipOnHello, &QCheckBox::toggled, &settings,
            &AppSettings::setManifestCacheSkipOnHello);
    connect(clearManifests, &QPushButton::clicked, this, [refreshClearManifests] {
        ManifestStore::instance().clear();
        refreshClearManifests();
    });

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // MCP clients launch `TraceView --mcp` as a local process, which only
    // exists on desktop.
    QGroupBox* agentSection = addSection(connectionsPage, tr("AI assistants"));
    auto* agentBridge =
        new QCheckBox(tr("Let AI assistants such as Claude control TraceView"), agentSection);
    agentBridge->setChecked(settings.agentBridgeEnabled());
    agentBridge->setToolTip(
        tr("An assistant connected over MCP can then create workspaces and widgets, read "
           "telemetry and take screenshots of this window. Its dashboard edits can be undone."));
    addCheckBoxRow(formFor(agentSection), agentBridge);
    connect(agentBridge, &QCheckBox::toggled, &settings, &AppSettings::setAgentBridgeEnabled);

    auto* agentAutoApprove =
        new QCheckBox(tr("Allow everything without asking"), agentSection);
    agentAutoApprove->setChecked(settings.agentAutoApprove());
    agentAutoApprove->setToolTip(
        tr("The assistant can then connect devices, send them commands, flash firmware, "
           "delete things and overwrite files without a confirmation each time. Only for "
           "assistants you trust."));
    agentAutoApprove->setEnabled(agentBridge->isChecked());
    addCheckBoxRow(formFor(agentSection), agentAutoApprove);
    connect(agentAutoApprove, &QCheckBox::toggled, &settings,
            &AppSettings::setAgentAutoApprove);
    connect(agentBridge, &QCheckBox::toggled, agentAutoApprove, &QWidget::setEnabled);
    // "Yes to All" in a confirmation turns it on from outside this page.
    connect(&settings, &AppSettings::agentPreferencesChanged, agentAutoApprove,
            [agentAutoApprove, &settings] {
                const QSignalBlocker block(agentAutoApprove);
                agentAutoApprove->setChecked(settings.agentAutoApprove());
            });

    // Two registrations cover every Claude on the computer: Claude Code's
    // user scope (~/.claude.json) is shared by the terminal, the IDE
    // extensions and the desktop app's Code tab; the desktop app's chat reads
    // its own claude_desktop_config.json, which no `claude mcp` command edits.
    const QStringList serverCommand = mcpServerCommand();
    QStringList commandParts;
    for (const QString& part : serverCommand) {
        commandParts.append(part.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(part)
                                                            : part);
    }
    const QString registerCommand = QStringLiteral("claude mcp add --scope user traceview -- ") +
                                    commandParts.join(QLatin1Char(' '));
    QJsonObject desktopServer;
    desktopServer.insert(QStringLiteral("command"), serverCommand.value(0));
    desktopServer.insert(QStringLiteral("args"), QJsonArray::fromStringList(serverCommand.mid(1)));
    const QString desktopEntry =
        QString::fromUtf8(
            QJsonDocument(QJsonObject{{QStringLiteral("traceview"), desktopServer}})
                .toJson(QJsonDocument::Indented))
            .trimmed();

    const auto addRegistration = [this, agentSection](const QString& title, const QString& text,
                                                      const QString& note) {
        auto* titleLabel = new QLabel(title, agentSection);
        titleLabel->setWordWrap(true);
        formFor(agentSection)->addRow(titleLabel);
        auto* textLabel = new QLabel(text, agentSection);
        textLabel->setTextFormat(Qt::PlainText);
        textLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        textLabel->setWordWrap(true);
        textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        formFor(agentSection)->addRow(textLabel);
        auto* copy = new QPushButton(tr("Copy"), agentSection);
        formFor(agentSection)->addRow(QString(), copy);
        auto* noteLabel = new QLabel(note, agentSection);
        noteLabel->setWordWrap(true);
        formFor(agentSection)->addRow(noteLabel);
        connect(copy, &QPushButton::clicked, this,
                [text] { QGuiApplication::clipboard()->setText(text); });
    };
    addRegistration(
        tr("Claude Code: run this once in a terminal."), registerCommand,
        tr("Registers TraceView for every folder (--scope user), so Claude Code in the terminal, "
           "the VS Code extension and the Code tab of the Claude desktop app all see it."));
    addRegistration(
        tr("Claude desktop app (chat): add this to claude_desktop_config.json."), desktopEntry,
        tr("Paste it inside the file's \"mcpServers\" object (Claude desktop app: Settings > "
           "Developer > Edit Config), then restart the app."));
#endif
    qobject_cast<QVBoxLayout*>(connectionsPage->layout())->addStretch();
    addPage(connectionsPage);

    QWidget* diagnosticsPage =
        createCategoryPage(pages, tr("Diagnostics"),
                           tr("Keep enough history for investigation without letting long-running "
                              "sessions use unbounded memory."));
    // The bell's options -- the same three its panel's menu changes, kept in
    // step with it both ways.
    QGroupBox* notificationSection = addSection(diagnosticsPage, tr("Notifications"));
    auto* popups = new QComboBox(notificationSection);
    popups->addItem(tr("Every notification"), int(AppSettings::NotificationPopups::All));
    popups->addItem(tr("Only the bell"), int(AppSettings::NotificationPopups::BellOnly));
    formFor(notificationSection)->addRow(tr("Pop-ups over the dashboard"), popups);
    auto* errorPopups = new QCheckBox(tr("Errors still pop up"), notificationSection);
    addCheckBoxRow(formFor(notificationSection), errorPopups);
    auto* pinStyle = new QComboBox(notificationSection);
    pinStyle->addItem(tr("Over the dashboard"), int(AppSettings::NotificationPinStyle::Overlay));
    pinStyle->addItem(tr("Beside the dashboard (shrinks it)"), int(AppSettings::NotificationPinStyle::Push));
    formFor(notificationSection)->addRow(tr("Pinned panel"), pinStyle);
    const auto syncNotificationFields = [popups, errorPopups, pinStyle, &settings] {
        const QSignalBlocker b1(popups);
        const QSignalBlocker b2(errorPopups);
        const QSignalBlocker b3(pinStyle);
        popups->setCurrentIndex(popups->findData(int(settings.notificationPopups())));
        errorPopups->setChecked(settings.notificationErrorPopups());
        errorPopups->setEnabled(settings.notificationPopups() == AppSettings::NotificationPopups::BellOnly);
        pinStyle->setCurrentIndex(pinStyle->findData(int(settings.notificationPinStyle())));
    };
    syncNotificationFields();
    connect(&settings, &AppSettings::notificationPreferencesChanged, this, syncNotificationFields);
    connect(popups, &QComboBox::currentIndexChanged, this, [popups, &settings](int) {
        settings.setNotificationPopups(AppSettings::NotificationPopups(popups->currentData().toInt()));
    });
    connect(errorPopups, &QCheckBox::toggled, &settings, &AppSettings::setNotificationErrorPopups);
    connect(pinStyle, &QComboBox::currentIndexChanged, this, [pinStyle, &settings](int) {
        settings.setNotificationPinStyle(AppSettings::NotificationPinStyle(pinStyle->currentData().toInt()));
    });

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
    QGroupBox* logFileSection = addSection(diagnosticsPage, tr("Log file"));
    auto* logFileLabel = new QLabel(breakablePath(AppLog::currentLogFilePath()), logFileSection);
    logFileLabel->setToolTip(AppLog::currentLogFilePath());
    logFileLabel->setWordWrap(true);
    logFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formFor(logFileSection)->addRow(tr("Current session"), logFileLabel);
    auto* verboseSerialLog =
        new QCheckBox(tr("Log raw serial bytes (verbose)"), logFileSection);
    verboseSerialLog->setChecked(settings.verboseSerialLogging());
    verboseSerialLog->setToolTip(
        tr("Every byte written to and read from a serial device is logged as hex. Leave off "
           "unless you're actively investigating a connection problem -- a device streaming "
           "telemetry fills the log fast."));
    addCheckBoxRow(formFor(logFileSection), verboseSerialLog);
    connect(verboseSerialLog, &QCheckBox::toggled, &settings,
            &AppSettings::setVerboseSerialLogging);
    auto* openLogFolderButton = new QPushButton(tr("Open log folder"), logFileSection);
    formFor(logFileSection)->addRow(QString(), openLogFolderButton);
    connect(openLogFolderButton, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppLog::logDirectory())); });

    qobject_cast<QVBoxLayout*>(diagnosticsPage->layout())->addStretch();
    addPage(diagnosticsPage);

#if defined(TRACEVIEW_FLATPAK_BUILD)
    QWidget* updatesPage = createCategoryPage(
        pages, tr("Updates"),
        tr("This installation is managed by Flatpak. Use your system's software "
           "manager or 'flatpak update' to update TraceView."));
#else
    QWidget* updatesPage = createCategoryPage(
        pages, tr("Updates"),
        tr("TraceView can check GitHub for a newer release. Nothing is downloaded or "
           "installed without your confirmation."));
    QGroupBox* updatesSection = addSection(updatesPage, tr("Automatic checks"));
    auto* autoCheckUpdates =
        new QCheckBox(tr("Check for updates on startup"), updatesSection);
    autoCheckUpdates->setChecked(settings.updateAutoCheckEnabled());
    addCheckBoxRow(formFor(updatesSection), autoCheckUpdates);
    connect(autoCheckUpdates, &QCheckBox::toggled, &settings,
            &AppSettings::setUpdateAutoCheckEnabled);
    m_updateStatusLabel = new QLabel(updatesPage);
    formFor(updatesSection)->addRow(tr("Last checked"), m_updateStatusLabel);
    const qint64 lastCheckEpochMs = settings.updateLastCheckEpochMs();
    m_updateStatusLabel->setText(
        lastCheckEpochMs > 0
            ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(lastCheckEpochMs),
                                 QLocale::ShortFormat)
            : tr("Never"));
    auto* checkNowButton = new QPushButton(tr("Check now"), updatesSection);
    formFor(updatesSection)->addRow(QString(), checkNowButton);
    connect(checkNowButton, &QPushButton::clicked, this,
            &SettingsPage::checkForUpdatesRequested);
#endif
    qobject_cast<QVBoxLayout*>(updatesPage->layout())->addStretch();
    addPage(updatesPage);

    // Every section's QFormLayout is a child of `pages` in the QObject tree
    // (addSection() parents it to the QGroupBox it creates, which is in
    // turn added to one of the pages above) -- one findChildren() sweep
    // instead of threading a QList through all eleven addSection() call
    // sites above. Used by applyCompactLayout()/resizeEvent() below.
    m_formLayouts = pages->findChildren<QFormLayout*>();

    // One fixed size for every combo/spin box, matching the Appearance
    // page's combos -- see kMinFieldWidth. Fixed, so they neither stretch
    // across a wide row nor get squeezed by the layout on a narrow one;
    // applyFieldWidth() only caps it when the screen is genuinely narrower.
    int fieldWidth = kMinFieldWidth;
    for (const QComboBox* combo : appearanceSection->findChildren<QComboBox*>()) {
        fieldWidth = qMax(fieldWidth, combo->sizeHint().width());
    }
    m_fieldWidth = qMin(fieldWidth, kMaxFieldWidth);
    const int fieldHeight = fontCombo->sizeHint().height();
    for (QComboBox* combo : pages->findChildren<QComboBox*>()) {
        m_fieldWidgets.append(combo);
    }
    for (QSpinBox* spin : pages->findChildren<QSpinBox*>()) {
        m_fieldWidgets.append(spin);
    }
    for (QWidget* field : std::as_const(m_fieldWidgets)) {
        field->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        field->setFixedHeight(fieldHeight);
    }

    connect(categories, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    categories->setCurrentRow(0);

    // Footer: the restart notice and its button, then Close.
    auto* restartRow = new QHBoxLayout;
    m_restartNotice = DialogChrome::hintLabel(QString(), this);
    restartRow->addWidget(m_restartNotice, 1);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Android/iOS can't relaunch the app (see MainWindow::restartApplication).
    m_restartButton = new QPushButton(tr("Close app"), this);
#else
    m_restartButton = new QPushButton(tr("Restart now"), this);
#endif
    restartRow->addWidget(m_restartButton);
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    restartRow->addWidget(m_buttonBox);
    rootLayout->addLayout(restartRow);
    connect(m_restartButton, &QPushButton::clicked, this, &SettingsPage::restartRequested);
    refreshRestartNotice();

    // Establishes the correct starting layout even if this page is first
    // shown already narrow (e.g. the app opened directly at a phone-sized
    // window) -- resizeEvent() alone would only react to a resize *after*
    // construction, not the size it's first laid out at.
    applyCompactLayout(width() <= kCompactLayoutMaxWidth);
}

void SettingsPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = width() <= kCompactLayoutMaxWidth;
    if (compact != m_compactLayout) {
        applyCompactLayout(compact);
    } else {
        applyFieldWidth();
    }
}

void SettingsPage::applyFieldWidth() {
    int width = m_fieldWidth;
    if (m_compactLayout) {
        const int available = this->width() - 2 * kCompactRootMargin - DialogChrome::kNavCompactWidth -
                              kCompactBodySpacing - 2 * kCompactPageMarginH - kFieldRowChrome;
        width = qBound(1, available, m_fieldWidth);
    }
    for (QWidget* field : std::as_const(m_fieldWidgets)) {
        field->setFixedWidth(width);
    }
}

void SettingsPage::applyCompactLayout(bool compact) {
    m_compactLayout = compact;
    // Icon-only when compact -- the full label doesn't fit a sidebar this
    // narrow. The tooltip carries the name either way.
    if (m_categoryList != nullptr) m_categoryList->setCompact(compact);
    // WrapAllRows stacks each row's label above its field instead of beside
    // it, the same fix deviceconfigdialog.cpp already applies unconditionally
    // for its own (always-narrow) form -- here it's conditional because this
    // page is full-width on desktop, where the side-by-side default reads
    // better.
    const QFormLayout::RowWrapPolicy policy =
        compact ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows;
    for (QFormLayout* form : std::as_const(m_formLayouts)) {
        form->setRowWrapPolicy(policy);
        form->setHorizontalSpacing(compact ? 8 : 18);
        form->setVerticalSpacing(compact ? 8 : 12);
    }

    if (m_rootLayout != nullptr) {
        m_rootLayout->setContentsMargins(compact ? QMargins(kCompactRootMargin, kCompactRootMargin,
                                                            kCompactRootMargin, kCompactRootMargin)
                                                 : DialogChrome::kMargins);
    }
    if (m_bodyLayout != nullptr) {
        m_bodyLayout->setSpacing(compact ? kCompactBodySpacing : DialogChrome::kBodySpacing);
    }
    const int pageMarginH = compact ? kCompactPageMarginH : kFullPageMarginH;
    const int pageMarginV = compact ? kCompactPageMarginV : kFullPageMarginV;
    for (QVBoxLayout* pageLayout : std::as_const(m_pageLayouts)) {
        if (pageLayout != nullptr) {
            pageLayout->setContentsMargins(pageMarginH, pageMarginV, pageMarginH, pageMarginV);
            pageLayout->setSpacing(compact ? 8 : DialogChrome::kSectionSpacing);
        }
    }
    applyFieldWidth();
}

void SettingsPage::setUpdateStatusText(const QString& text) {
    if (m_updateStatusLabel != nullptr) {
        m_updateStatusLabel->setText(text);
    }
}

void SettingsPage::setTitleVisible(bool visible) {
    m_header->setVisible(visible);
}

void SettingsPage::refreshRestartNotice() {
    const AppSettings& settings = AppSettings::instance();
    const bool restartRequired =
        LanguageManager::instance().currentLanguage().id != m_initialLanguageId ||
        settings.frameLogCapacity() != m_initialFrameLogCapacity ||
        settings.notificationHistoryCapacity() != m_initialNotificationHistoryCapacity;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    const QString restartText =
        tr("Close and reopen TraceView to apply language or diagnostics history changes, "
           "or they will apply the next time you open it.");
#else
    const QString restartText =
        tr("Restart TraceView to apply language or diagnostics history changes.");
#endif
    m_restartNotice->setText(restartRequired
                                 ? restartText
                                 : tr("Changes apply immediately unless noted otherwise."));
    m_restartButton->setEnabled(restartRequired);
}

}  // namespace traceview
