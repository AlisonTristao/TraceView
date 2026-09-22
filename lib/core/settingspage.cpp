#include "core/settingspage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

#include "core/applog.h"
#include "preferences/appsettings.h"
#include "theme/iconutils.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// The category glyphs down the left of the page, loaded from
// resources/icons/settings/ (same flat style as core/ribbonicons.cpp) and
// tinted to the active theme's colour at load time -- see theme/iconutils.h.
constexpr int kCategoryIconSize = 18;
constexpr int kCategoryCount = 7;
constexpr int kFullCategoryListWidth = 186;
// Icon (18px) + the item padding below (10px each side) + a little room so
// a touch target isn't flush against the icon -- comfortable to tap without
// needing the label text compact mode hides.
constexpr int kCompactCategoryListWidth = 56;
// Same threshold as MainWindow's own kSmallBreakpointMaxViewportWidth
// (mainwindow.cpp) -- this page has no access to that constant or to
// DashboardGrid's breakpoint machinery (it's a standalone widget, not part
// of the Dashboard tab), so it re-derives "phone-narrow" from its own width
// instead. Kept in sync deliberately: below this, the fixed-width category
// sidebar plus a side-by-side QFormLayout leaves the actual settings
// values too cramped to read or tap accurately.
constexpr int kCompactLayoutMaxWidth = 700;
// Outer/page margins and sidebar gap, desktop vs compact. On a phone every
// pixel of horizontal margin is taken straight out of the combo/spin box
// width, so compact mode trims them hard.
constexpr int kFullRootMargin = 18;
constexpr int kCompactRootMargin = 8;
constexpr int kFullBodySpacing = 18;
constexpr int kCompactBodySpacing = 8;
constexpr int kFullPageMarginH = 28;
constexpr int kFullPageMarginV = 24;
constexpr int kCompactPageMarginH = 10;
constexpr int kCompactPageMarginV = 12;
// Every combo/spin box shares one fixed width, taken from the Appearance
// page's combos (clamped to this range) so fields don't vary page to page.
constexpr int kMinFieldWidth = 180;
constexpr int kMaxFieldWidth = 260;
// Group box frame + inner margins + a vertical scrollbar's width -- the
// horizontal space a field row loses beyond the page's own margins.
constexpr int kFieldRowChrome = 40;

QPixmap categoryPixmapFor(int index, const QColor& color) {
    static const char* const kCategorySvgs[kCategoryCount] = {
        ":/icons/settings/general.svg",     ":/icons/settings/appearance.svg",
        ":/icons/settings/dashboard.svg",   ":/icons/settings/terminal.svg",
        ":/icons/settings/connections.svg", ":/icons/settings/diagnostics.svg",
        ":/icons/settings/updates.svg",
    };
    const int clampedIndex = qBound(0, index, kCategoryCount - 1);
    return loadTintedIcon(kCategorySvgs[clampedIndex], color, kCategoryIconSize)
        .pixmap(kCategoryIconSize, kCategoryIconSize);
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
    layout->setContentsMargins(kFullPageMarginH, kFullPageMarginV, kFullPageMarginH,
                               kFullPageMarginV);
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

    auto* rootLayout = new QVBoxLayout(this);
    m_rootLayout = rootLayout;
    rootLayout->setContentsMargins(kFullRootMargin, kFullRootMargin, kFullRootMargin,
                                   kFullRootMargin);
    rootLayout->setSpacing(12);

    auto* header = new QLabel(tr("Settings"), this);
    header->setObjectName("settingsTitle");
    QFont headerFont = header->font();
    headerFont.setPointSize(headerFont.pointSize() + 6);
    headerFont.setBold(true);
    header->setFont(headerFont);
    rootLayout->addWidget(header);

    auto* body = new QHBoxLayout;
    m_bodyLayout = body;
    body->setSpacing(kFullBodySpacing);
    auto* categories = new QListWidget(this);
    categories->setFixedWidth(kFullCategoryListWidth);
    m_categoryList = categories;
    // This is a fixed-width nav strip, never meant to scroll sideways --
    // but Qt's own content-width calculation for icon-only rows (compact
    // mode, applyCompactLayout() below) can land a hair over
    // kCompactCategoryListWidth, which otherwise shows a needless
    // horizontal scrollbar under it.
    categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories->setIconSize(QSize(kCategoryIconSize, kCategoryIconSize));
    // The app-wide QListWidget::item rule only reserves 4px around the label;
    // with an icon in front of it that leaves the glyph hard against the
    // frame and the text crowding it. Widen the padding (and space the rows)
    // just for this navigation list.
    categories->setStyleSheet(QStringLiteral("QListWidget::item { padding: 7px 10px; }"));
    categories->setSpacing(3);
    const QStringList categoryNames = {tr("General"),     tr("Appearance"),  tr("Dashboard"),
                                       tr("Terminal"),    tr("Connections"), tr("Diagnostics"),
                                       tr("Updates")};
    m_categoryNames = categoryNames;
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
    addPage(appearancePage);

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
    qobject_cast<QVBoxLayout*>(connectionsPage->layout())->addStretch();
    addPage(connectionsPage);

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
    for (const QComboBox* combo : {themeCombo, fontCombo, languageCombo}) {
        fieldWidth = qMax(fieldWidth, combo->sizeHint().width());
    }
    m_fieldWidth = qMin(fieldWidth, kMaxFieldWidth);
    const int fieldHeight = themeCombo->sizeHint().height();
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

    auto* restartRow = new QHBoxLayout;
    m_restartNotice = new QLabel(this);
    m_restartNotice->setWordWrap(true);
    restartRow->addWidget(m_restartNotice, 1);
    m_restartButton = new QPushButton(tr("Restart now"), this);
    restartRow->addWidget(m_restartButton);
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
        const int available = this->width() - 2 * kCompactRootMargin - kCompactCategoryListWidth -
                              kCompactBodySpacing - 2 * kCompactPageMarginH - kFieldRowChrome;
        width = qBound(1, available, m_fieldWidth);
    }
    for (QWidget* field : std::as_const(m_fieldWidgets)) {
        field->setFixedWidth(width);
    }
}

void SettingsPage::applyCompactLayout(bool compact) {
    m_compactLayout = compact;
    if (m_categoryList != nullptr) {
        m_categoryList->setFixedWidth(compact ? kCompactCategoryListWidth : kFullCategoryListWidth);
        // Icon-only when compact -- the full label doesn't fit a sidebar
        // this narrow. Tooltip always carries the name either way, so it's
        // still discoverable on a long-press/hover.
        const int rows = qMin(m_categoryList->count(), m_categoryNames.size());
        for (int i = 0; i < rows; ++i) {
            QListWidgetItem* item = m_categoryList->item(i);
            item->setText(compact ? QString() : m_categoryNames.at(i));
            item->setToolTip(m_categoryNames.at(i));
        }
    }
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

    const int rootMargin = compact ? kCompactRootMargin : kFullRootMargin;
    if (m_rootLayout != nullptr) {
        m_rootLayout->setContentsMargins(rootMargin, rootMargin, rootMargin, rootMargin);
    }
    if (m_bodyLayout != nullptr) {
        m_bodyLayout->setSpacing(compact ? kCompactBodySpacing : kFullBodySpacing);
    }
    const int pageMarginH = compact ? kCompactPageMarginH : kFullPageMarginH;
    const int pageMarginV = compact ? kCompactPageMarginV : kFullPageMarginV;
    for (QVBoxLayout* pageLayout : std::as_const(m_pageLayouts)) {
        if (pageLayout != nullptr) {
            pageLayout->setContentsMargins(pageMarginH, pageMarginV, pageMarginH, pageMarginV);
            pageLayout->setSpacing(compact ? 12 : 20);
        }
    }
    applyFieldWidth();
}

void SettingsPage::setUpdateStatusText(const QString& text) {
    if (m_updateStatusLabel != nullptr) {
        m_updateStatusLabel->setText(text);
    }
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
