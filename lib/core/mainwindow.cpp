#include "mainwindow.h"

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "donatedialog.h"
#include "backend/backend.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "dashboard/widgets/chartwidgets.h"
#include "debugchartswindow.h"
#include "layerspanel.h"
#include "paneldockcontroller.h"
#include "project/projectstore.h"
#include "protocol/btpbackend.h"
#include "propertiespanel.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "serialmanager.h"
#include "serialwidgetbridge.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace traceview {

namespace {
// Namespace-scope, outside MainWindow -- plain tr() isn't available here, so
// this uses QCoreApplication::translate() directly, same as the non-QObject
// fixes elsewhere in the codebase.
const QString kProjectFileFilter = QCoreApplication::translate("MainWindow", "TraceView Project (*.tvproj)");
constexpr const char* kRecentFilesSettingsKey = "recentFiles/paths";
constexpr int kMaxRecentFiles = 10;

// What a gauge asks for: it has no sample-time setting of its own (a gauge
// only ever shows the newest value), so it requests a modest fixed rate
// instead of inventing a config field for it.
constexpr quint32 kGaugeRequestedRateMillihz = 5000;  // 5 Hz
// Fallback for a chart whose configured sample time is unusable (<= 0).
constexpr quint32 kDefaultRequestedRateMillihz = 10000;  // 10 Hz

// Converts a chart's configured sample period into the rate its SUBSCRIBE
// asks for. This is only a *request*: the source clamps it to its schema's
// min/max and reports the effective rate in SUBSCRIBE_RESULT, which is what
// the status bar then shows (topico 17).
quint32 requestedRateMillihzFor(double sampleTimeMs) {
    if (!(sampleTimeMs > 0.0)) {
        return kDefaultRequestedRateMillihz;
    }
    const double millihz = 1'000'000.0 / sampleTimeMs;  // 1000 Hz-per-ms * 1000 milli
    if (millihz < 1.0) {
        return 1;
    }
    if (millihz > 4'000'000'000.0) {
        return 4'000'000'000u;
    }
    return quint32(millihz);
}

QString formatRateMillihz(quint32 millihz) {
    return QString::number(millihz / 1000.0, 'g', 4) + " Hz";
}

// The (source_id, topic_id, requested rate) one dashboard widget implies.
// All-zero for a widget that is not a telemetry consumer, or one whose
// source/topic has not been configured yet -- SubscriptionManager treats that
// as "no consumer" and puts nothing on the wire.
struct WidgetTopicRequest {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint32 rateMillihz = 0;
};

WidgetTopicRequest widgetTopicRequest(DashboardWidget* widget) {
    if (auto* chart = dynamic_cast<ChartWidgetBase*>(widget)) {
        const ChartConfig& config = chart->config();
        return {config.sourceId, config.topicId, requestedRateMillihzFor(config.sampleTimeMs)};
    }
    if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(widget)) {
        const GaugeConfig& config = gauge->config();
        return {config.sourceId, config.topicId, kGaugeRequestedRateMillihz};
    }
    return {};
}

quint64 topicStatusKey(quint32 sourceId, quint16 topicId) {
    return (quint64(sourceId) << 16) | quint64(topicId);
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("TraceView v%1").arg(kVersion));
    resize(1024, 640);

    buildMenus();

    m_serialManager = new SerialManager(this);
    m_dashboardGrid = new DashboardGrid(this);
    // Drives every chart cell's header status dot (DashboardCell::
    // setConnected()) off the app's one physical serial connection -- set
    // the initial (disconnected) state up front so a fresh layout doesn't
    // start with a stale/default dot before the first signal fires.
    connect(m_serialManager, &SerialManager::connectionStateChanged, m_dashboardGrid,
            &DashboardGrid::setDeviceConnected);
    m_dashboardGrid->setDeviceConnected(m_serialManager->isConnected());

    // Everything that gives the transport's raw bytes meaning lives behind
    // the Backend interface (backend/backend.h) -- concretely a BtpBackend
    // today, wiring BtpSession/ProtocolRouter/TelemetryFieldRouter/
    // BtpHandshake/ManifestClient/SubscriptionManager internally (see
    // protocol/btpbackend.cpp). MainWindow only ever talks to it through the
    // abstract interface, so plugging in a different protocol means
    // constructing a different Backend here -- nothing else in this file
    // changes.
    m_backend = new BtpBackend(this);
    connect(m_serialManager, &SerialManager::dataReceived, m_backend, &Backend::feedBytes);
    connect(m_backend, &Backend::bytesToWrite, m_serialManager, &SerialManager::write);
    connect(m_serialManager, &SerialManager::connectionStateChanged, m_backend,
            &Backend::onTransportConnectionChanged);
    connect(m_backend, &Backend::statusMessage, this,
            [this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
    connect(m_backend, &Backend::subscriptionsChanged, this, &MainWindow::updateTelemetryStatusLabel);
    connect(m_backend, &Backend::statusReceived, this, &MainWindow::updateTelemetryStatusLabel);

    // Wires control-widget commands and the serial monitor/terminal to the
    // same connection (BACKEND_TODO.txt Tasks 9/10; terminal rewired onto
    // BTP TERMINAL_IN/OUT in topico 19); no further interaction needed here
    // once wired. Outbound control-widget commands still go straight to
    // SerialManager as raw literal text (docs/PROTOCOL.md "Outbound: control
    // commands") -- that path bypasses Backend entirely, see
    // core/serialwidgetbridge.cpp.
    new SerialWidgetBridge(m_serialManager, m_backend, m_dashboardGrid, this);

    Ribbon* ribbon = buildRibbon();
    buildLayersPanel();
    buildPropertiesPanel();

    // Canvas fills the whole row below the ribbon; the layers/properties
    // panels float on top of it instead of sharing the row via layout, so
    // docking one never shrinks the canvas -- and, since DashboardGrid
    // stores every item's position/size as a fraction of the canvas area
    // (see dashboardgrid.h), a shrink would otherwise reflow/resize every
    // widget on the dashboard along with it. Positioned directly by
    // m_dockController (see paneldockcontroller.h) rather than a
    // QDockWidget, which always reserves real layout space for a docked
    // widget and spans the full window height (menu bar to status bar),
    // not just the canvas.
    m_contentRow = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(m_contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_dashboardGrid);

    // Reparented rather than added to contentLayout: their geometry is set
    // directly by m_dockController (wired below via an event filter on
    // m_contentRow) so they overlay the canvas instead of squeezing it, and
    // can be dragged by their header to any edge or off into a floating
    // window. The layers panel is 1/3 the width of the properties panel by
    // default, since it only needs to fit short layer names -- see each
    // panel's preferredThickness().
    m_layersPanel->setParent(m_contentRow);
    m_propertiesPanel->setParent(m_contentRow);
    m_contentRow->installEventFilter(this);

    m_dockController = new PanelDockController(m_contentRow, this, this);
    m_dockController->registerPanel(m_layersPanel, "layers", DockEdge::Left);
    m_dockController->registerPanel(m_propertiesPanel, "properties", DockEdge::Right);
    m_dockController->restoreState();
    connect(m_dockController, &PanelDockController::dragFinished, this, &MainWindow::updatePanelVisibility);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, kRibbonTopMargin, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(ribbon);
    centralLayout->addWidget(m_contentRow, /*stretch=*/1);
    setCentralWidget(central);

    // Permanent readout of what telemetry is actually flowing: the effective
    // rate each subscribed topic was granted (never the rate that was asked
    // for), plus per-topic bytes/drops when the peer publishes
    // status_version=2 (COMMANDS_AND_ACTIONS.md section 8.1). The transient
    // showMessage() calls above stay for one-off events; this is the standing
    // state.
    m_telemetryStatusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_telemetryStatusLabel);
}

MainWindow::~MainWindow() {
    // The dashboard's widgets are deleted later, as QObject children, i.e.
    // after this class's own members are gone -- so their destroyed()
    // handlers (which drop a subscription reference, see
    // wireChartWidgetToTelemetry) must be taken down here, while
    // m_widgetSubscriptions still exists. Only signals *from* each widget are
    // disconnected; the fieldSample connections into them are unaffected.
    for (auto it = m_widgetSubscriptions.constBegin(); it != m_widgetSubscriptions.constEnd(); ++it) {
        disconnect(it.key(), nullptr, this, nullptr);
    }
    m_widgetSubscriptions.clear();
}

void MainWindow::wireChartWidgetToTelemetry(DashboardWidget* widget) {
    bool consumesTelemetry = false;
    if (auto* chart = dynamic_cast<ChartWidgetBase*>(widget)) {
        connect(m_backend, &Backend::fieldSample, chart, &ChartWidgetBase::onFieldSample);
        consumesTelemetry = true;
    } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(widget)) {
        connect(m_backend, &Backend::fieldSample, gauge, &DummyGaugeWidget::onFieldSample);
        consumesTelemetry = true;
    }
    if (!consumesTelemetry) {
        return;
    }

    // topico 17 PASSO 2: this widget is one *reference* to its topic, not a
    // subscription of its own -- Backend collapses however many widgets
    // read (source, topic) into a single subscription.
    const WidgetTopicRequest request = widgetTopicRequest(widget);
    m_widgetSubscriptions.insert(
        widget, m_backend->addSubscriber(request.sourceId, request.topicId, request.rateMillihz));

    // PASSO 5: closing a widget only drops its reference; an unsubscribe is
    // sent only when it was the last consumer of that topic.
    connect(widget, &QObject::destroyed, this, [this, widget] {
        m_backend->removeSubscriber(m_widgetSubscriptions.take(widget));
    });
}

void MainWindow::refreshWidgetSubscriptions() {
    for (auto it = m_widgetSubscriptions.begin(); it != m_widgetSubscriptions.end(); ++it) {
        const WidgetTopicRequest request = widgetTopicRequest(it.key());
        it.value() =
            m_backend->updateSubscriber(it.value(), request.sourceId, request.topicId, request.rateMillihz);
    }
}

void MainWindow::updateTelemetryStatusLabel() {
    if (!m_telemetryStatusLabel) {
        return;
    }
    const QVector<TopicSubscriptionState> states = m_backend->subscriptions();
    if (states.isEmpty()) {
        m_telemetryStatusLabel->clear();
        m_telemetryStatusLabel->setToolTip(QString());
        return;
    }

    QHash<quint64, StatusTopicRecord> statusByTopic;
    for (const StatusTopicRecord& record : m_backend->topicStatuses()) {
        statusByTopic.insert(topicStatusKey(record.sourceId, record.topicId), record);
    }

    QStringList summary;
    QStringList detail;
    for (const TopicSubscriptionState& state : states) {
        const QString topicLabel = QString("0x%1/0x%2")
                                       .arg(state.sourceId, 8, 16, QChar('0'))
                                       .arg(state.topicId, 4, 16, QChar('0'));
        QString rate;
        if (state.effectiveRateMillihz != 0) {
            rate = formatRateMillihz(state.effectiveRateMillihz);
        } else if (state.awaitingResult) {
            rate = QStringLiteral("pending");
        } else {
            rate = QStringLiteral("not granted");
        }
        QString entry = topicLabel + ' ' + rate;
        if (state.rateLimited()) {
            entry += QString(" (limited, asked %1)").arg(formatRateMillihz(state.requestedRateMillihz));
        }
        summary.append(entry);

        QString line = entry + QString(" | %1 widget(s) here").arg(state.subscriberCount);
        const auto record = statusByTopic.constFind(topicStatusKey(state.sourceId, state.topicId));
        if (record != statusByTopic.constEnd()) {
            // status_version=2 per-topic metrics (section 8.1).
            line += QString(" | %1 subscriber(s) at source | %2 B | %3 drops")
                        .arg(record->subscriberCount)
                        .arg(record->bytesTotal)
                        .arg(record->samplesDroppedTotal);
        }
        detail.append(line);
    }
    m_telemetryStatusLabel->setText(summary.join(QStringLiteral("   ")));
    m_telemetryStatusLabel->setToolTip(detail.join(QStringLiteral("\n")));
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newAction = fileMenu->addAction(tr("&New Project"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewProject);

    auto* openAction = fileMenu->addAction(tr("&Open Project..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenProject);

    m_recentFilesMenu = fileMenu->addMenu(tr("Open &Recent"));
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction(tr("&Save Project"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);

    auto* saveAsAction = fileMenu->addAction(tr("Save Project &As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveProjectAs);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* themeMenu = viewMenu->addMenu(tr("&Theme"));

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    const QString currentId = ThemeManager::instance().currentTheme().id;
    for (const ThemePalette& palette : ThemeManager::instance().availableThemes()) {
        auto* action = themeMenu->addAction(palette.displayName);
        action->setCheckable(true);
        action->setChecked(palette.id == currentId);
        action->setData(palette.id);
        group->addAction(action);

        connect(action, &QAction::triggered, this, [id = palette.id]() {
            ThemeManager::instance().setTheme(id);
        });
    }

    auto* fontMenu = viewMenu->addMenu(tr("&Font"));

    auto* fontGroup = new QActionGroup(this);
    fontGroup->setExclusive(true);

    const QString currentFontId = FontManager::instance().currentFont().id;
    for (const FontOption& font : FontManager::instance().availableFonts()) {
        auto* action = fontMenu->addAction(font.displayName);
        action->setCheckable(true);
        action->setChecked(font.id == currentFontId);
        action->setData(font.id);
        fontGroup->addAction(action);

        connect(action, &QAction::triggered, this, [id = font.id]() {
            FontManager::instance().setFont(id);
        });
    }

    // Restart-to-apply: switching languages does not attempt to live-
    // retranslate every open widget (that spans the whole UI/dashboard
    // layer), so the choice is persisted and the app offers to relaunch.
    auto* languageMenu = viewMenu->addMenu(tr("&Language"));

    auto* languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);

    const QString currentLanguageId = LanguageManager::instance().currentLanguage().id;
    for (const LanguageInfo& language : LanguageManager::instance().availableLanguages()) {
        auto* action = languageMenu->addAction(language.displayName);
        action->setCheckable(true);
        action->setChecked(language.id == currentLanguageId);
        action->setData(language.id);
        languageGroup->addAction(action);

        connect(action, &QAction::triggered, this, [this, id = language.id]() {
            if (id == LanguageManager::instance().currentLanguage().id) {
                return;
            }
            LanguageManager::instance().setLanguage(id);

            QMessageBox restartBox(this);
            restartBox.setWindowTitle(tr("Restart Required"));
            restartBox.setText(tr("The application needs to restart to apply the new language. Restart now?"));
            auto* restartNowButton = restartBox.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
            restartBox.addButton(tr("Later"), QMessageBox::RejectRole);
            restartBox.exec();

            if (restartBox.clickedButton() == restartNowButton) {
                QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
                QCoreApplication::quit();
            }
        });
    }

    auto* debugAction = menuBar()->addAction(tr("&Debug"));
    connect(debugAction, &QAction::triggered, this, &MainWindow::onDebug);

    auto* aboutAction = menuBar()->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    auto* donateAction = menuBar()->addAction(tr("Dona&te"));
    connect(donateAction, &QAction::triggered, this, &MainWindow::onDonate);
}

Ribbon* MainWindow::buildRibbon() {
    m_positionAction = new QAction(tr("Position"), this);
    m_positionAction->setCheckable(true);
    m_positionAction->setChecked(true);
    m_positionAction->setEnabled(false);
    connect(m_positionAction, &QAction::toggled, this, [this](bool checked) {
        if (!checked) {
            m_positionAction->setChecked(true); // selection is always the active tool while editing
        }
    });

    m_addWidgetAction = new QAction(tr("Add"), this);
    m_addWidgetAction->setEnabled(false);
    connect(m_addWidgetAction, &QAction::triggered, this, &MainWindow::onAddWidget);

    m_removeAction = new QAction(tr("Remove"), this);
    m_removeAction->setEnabled(false);
    m_removeAction->setShortcut(QKeySequence::Delete);
    connect(m_removeAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::removeSelected);

    m_copyAction = new QAction(tr("Copy"), this);
    m_copyAction->setEnabled(false);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::copySelected);

    m_pasteAction = new QAction(tr("Paste"), this);
    m_pasteAction->setEnabled(false);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::pasteItem);

    m_bringToFrontAction = new QAction(tr("To Front"), this);
    m_bringToFrontAction->setEnabled(false);
    connect(m_bringToFrontAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::bringSelectedToFront);

    m_bringForwardAction = new QAction(tr("Forward"), this);
    m_bringForwardAction->setEnabled(false);
    connect(m_bringForwardAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::bringSelectedForward);

    m_sendBackwardAction = new QAction(tr("Backward"), this);
    m_sendBackwardAction->setEnabled(false);
    connect(m_sendBackwardAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::sendSelectedBackward);

    m_sendToBackAction = new QAction(tr("To Back"), this);
    m_sendToBackAction->setEnabled(false);
    connect(m_sendToBackAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::sendSelectedToBack);

    // createUndoAction()/createRedoAction() wire up triggered/enabled state
    // (and a dynamic "Undo <command text>" label) directly from the stack —
    // no manual canUndo()/canRedo() syncing needed.
    m_undoAction = m_dashboardGrid->undoStack()->createUndoAction(this, tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_dashboardGrid->undoStack()->createRedoAction(this, tr("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);

    connect(m_dashboardGrid, &DashboardGrid::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::refreshPropertiesPanel);
    // The layers panel's row list needs to resync on anything that could add/
    // remove/rename/reorder an item -- itemsChanged() covers add/remove/type-
    // change/load, indexChanged() covers everything else that goes through
    // the undo stack (rename, z-order, and their own undo/redo), same
    // reasoning as the refreshPropertiesPanel hook right above.
    connect(m_dashboardGrid, &DashboardGrid::itemsChanged, this, &MainWindow::refreshLayersPanel);
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::refreshLayersPanel);
    // A config edit (or its undo/redo) can repoint a widget at another
    // source/topic or change its sample time, which changes what this client
    // must have subscribed -- every path that edits a config goes through the
    // undo stack, so this one hook covers them all (topico 17 PASSO 2/5).
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this,
            &MainWindow::refreshWidgetSubscriptions);
    // Chart/gauge widgets subscribe to live telemetry the moment they're
    // created -- topico 15's "varios assinantes por campo" wiring, covering
    // both a fresh Add Widget and a project load (DashboardGrid::createCell
    // is the single factory path for both, see dashboardgrid.cpp).
    connect(m_dashboardGrid, &DashboardGrid::widgetCreated, this, &MainWindow::wireChartWidgetToTelemetry);
    // The clipboard can change from a copySelected() call here, or from
    // another window/app entirely — either way, m_pasteAction's enabled
    // state needs to stay in sync with whether it's currently pasteable.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::updateSelectionActions);

    auto* runPage = new QWidget(this);
    runPage->setObjectName("ribbonPage");
    runPage->setFixedHeight(kRibbonPageHeight);
    auto* runLayout = new QHBoxLayout(runPage);
    runLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    runLayout->setSpacing(kRibbonGroupSpacing);

    m_portCombo = new QComboBox(runPage);
    m_portCombo->setToolTip(tr("Serial port"));
    m_portCombo->setMinimumWidth(110);

    m_refreshPortsButton = new QToolButton(runPage);
    m_refreshPortsButton->setText(QString::fromUtf8("\xE2\x9F\xB3")); // ⟳
    m_refreshPortsButton->setToolTip(tr("Refresh port list"));
    m_refreshPortsButton->setAutoRaise(true);
    m_refreshPortsButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    connect(m_refreshPortsButton, &QToolButton::clicked, this, &MainWindow::refreshSerialPorts);

    // Reuses the same baud list SerialMonitorWidget used to offer for its
    // now-removed per-widget connect bar (Tarefa 3) -- one global connection
    // means one place to pick the baud rate. Extended with the higher rates
    // a BTP v1 dongle actually uses (TRANSPORT_SERIAL.md section 8;
    // t_dongle_develop's own monitor_speed/BAUDRATE) and made editable so
    // any board-specific value can be typed directly, since USB CDC line
    // coding is informative only (same section) and real UART boards vary.
    m_baudCombo = new QComboBox(runPage);
    m_baudCombo->setEditable(true);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600",
                            "1000000", "2000000", "3000000", "5000000"});
    m_baudCombo->setCurrentText("921600");
    m_baudCombo->setToolTip(tr("Baud rate (type a custom value if yours isn't listed)"));
    m_baudCombo->setValidator(new QIntValidator(1, 10000000, m_baudCombo));

    // Terminator appended to control-widget commands only (docs/PROTOCOL.md
    // "Outbound: control commands", BACKEND_TODO.txt Task 9) -- unlike
    // port/baud this isn't a QSerialPort property, so it stays editable
    // while connected instead of being locked alongside them below.
    m_lineTerminatorCombo = new QComboBox(runPage);
    m_lineTerminatorCombo->addItem(tr("None"), int(LineTerminator::None));
    m_lineTerminatorCombo->addItem(tr("LF (\\n)"), int(LineTerminator::Lf));
    m_lineTerminatorCombo->addItem(tr("CR (\\r)"), int(LineTerminator::Cr));
    m_lineTerminatorCombo->addItem(tr("CRLF (\\r\\n)"), int(LineTerminator::CrLf));
    m_lineTerminatorCombo->setCurrentIndex(1); // Lf, matching SerialManager's default
    m_lineTerminatorCombo->setToolTip(tr("Line terminator appended to control-widget commands (push button/toggle/"
                                          "slider). Doesn't affect the serial terminal's raw keystrokes."));
    connect(m_lineTerminatorCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onLineTerminatorChanged);

    m_connectButton = new QPushButton(tr("Connect"), runPage);
    m_connectButton->setCheckable(true);
    connect(m_connectButton, &QPushButton::toggled, this, &MainWindow::onSerialConnectToggled);

    m_fullscreenButton = new QToolButton(runPage);
    m_fullscreenButton->setCheckable(true);
    m_fullscreenButton->setAutoRaise(true);
    m_fullscreenButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_fullscreenButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_fullscreenButton->setToolTip(tr("Fullscreen dashboard (F11)"));
    connect(m_fullscreenButton, &QToolButton::toggled, this, &MainWindow::onFullscreenToggled);

    // Window-level shortcuts (not menu items) so they keep working once the
    // menu bar is hidden while fullscreen (see onFullscreenToggled). Routed
    // through the button itself rather than duplicating onFullscreenToggled's
    // logic here.
    auto* fullscreenAction = new QAction(this);
    fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    addAction(fullscreenAction);
    connect(fullscreenAction, &QAction::triggered, m_fullscreenButton, &QToolButton::toggle);

    auto* exitFullscreenAction = new QAction(this);
    exitFullscreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(exitFullscreenAction);
    connect(exitFullscreenAction, &QAction::triggered, this, [this]() {
        if (m_fullscreenButton->isChecked()) {
            m_fullscreenButton->setChecked(false);
        }
    });

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    runLayout->addWidget(m_portCombo);
    runLayout->addWidget(m_refreshPortsButton);
    runLayout->addWidget(m_baudCombo);
    runLayout->addWidget(m_lineTerminatorCombo);
    runLayout->addWidget(m_connectButton);
    runLayout->addStretch();
    runLayout->addWidget(m_fullscreenButton);

    refreshSerialPorts();
    connect(m_serialManager, &SerialManager::connectionStateChanged, this,
            &MainWindow::onSerialConnectionStateChanged);
    connect(m_serialManager, &SerialManager::errorOccurred, this, &MainWindow::onSerialErrorOccurred);

    auto* configurePage = new QWidget(this);
    configurePage->setObjectName("ribbonPage");
    configurePage->setFixedHeight(kRibbonPageHeight);
    auto* configureLayout = new QHBoxLayout(configurePage);
    configureLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    configureLayout->setSpacing(kRibbonGroupSpacing);

    configureLayout->addWidget(
        Ribbon::createButtonGroup(configurePage, {m_positionAction, m_addWidgetAction, m_removeAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(
        configurePage, {m_bringToFrontAction, m_bringForwardAction, m_sendBackwardAction, m_sendToBackAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_copyAction, m_pasteAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_undoAction, m_redoAction}));
    configureLayout->addStretch();

    auto* ribbon = new Ribbon(this);
    m_runTabIndex = ribbon->addTab(tr("Run"), runPage);
    m_configureTabIndex = ribbon->addTab(tr("Layout"), configurePage);

    connect(ribbon, &Ribbon::currentTabChanged, this, &MainWindow::onRibbonTabChanged);

    m_ribbon = ribbon;
    return ribbon;
}

void MainWindow::buildLayersPanel() {
    m_layersPanel = new LayersPanel(this);
    connect(m_layersPanel, &LayersPanel::itemSelected, m_dashboardGrid, &DashboardGrid::selectItem);
    connect(m_layersPanel, &LayersPanel::pinnedChanged, this, &MainWindow::updatePanelVisibility);

    // Only relevant while editing the layout -- matches m_propertiesPanel
    // (see buildPropertiesPanel() below).
    m_layersPanel->hide();
}

void MainWindow::buildPropertiesPanel() {
    m_propertiesPanel = new PropertiesPanel(this);
    m_propertiesPanel->setAvailableTypes(WidgetRegistry::instance().availableTypes());

    connect(m_propertiesPanel, &PropertiesPanel::typeChangeRequested, this,
            &MainWindow::onPanelTypeChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::nameChangeRequested, this,
            &MainWindow::onPanelNameChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::keyChangeRequested, this, &MainWindow::onPanelKeyChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::configChangeRequested, this,
            &MainWindow::onPanelConfigChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::pinnedChanged, this, &MainWindow::updatePanelVisibility);

    // Only relevant while editing the layout — matches m_addWidgetAction/
    // m_positionAction, which also start disabled until the Layout tab is
    // active (see onRibbonTabChanged).
    m_propertiesPanel->hide();
}

void MainWindow::updateRibbonIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_positionAction->setIcon(makeSelectIcon(palette.textPrimary));
    m_positionAction->setToolTip(tr("Position — select a widget to move/resize it"));
    m_addWidgetAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addWidgetAction->setToolTip(tr("Add widget"));
    m_removeAction->setIcon(makeMinusIcon(palette.danger));
    m_removeAction->setToolTip(tr("Remove selected widget (%1)")
                                    .arg(m_removeAction->shortcut().toString(QKeySequence::NativeText)));
    m_copyAction->setIcon(makeCopyIcon(palette.textPrimary));
    m_copyAction->setToolTip(
        tr("Copy selected widget (%1)").arg(m_copyAction->shortcut().toString(QKeySequence::NativeText)));
    m_pasteAction->setIcon(makePasteIcon(palette.textPrimary));
    m_pasteAction->setToolTip(
        tr("Paste as a new widget (%1)").arg(m_pasteAction->shortcut().toString(QKeySequence::NativeText)));
    m_bringToFrontAction->setIcon(makeBringToFrontIcon(palette.textPrimary));
    m_bringToFrontAction->setToolTip(tr("Bring to front"));
    m_bringForwardAction->setIcon(makeBringForwardIcon(palette.textPrimary));
    m_bringForwardAction->setToolTip(tr("Bring forward"));
    m_sendBackwardAction->setIcon(makeSendBackwardIcon(palette.textPrimary));
    m_sendBackwardAction->setToolTip(tr("Send backward"));
    m_sendToBackAction->setIcon(makeSendToBackIcon(palette.textPrimary));
    m_sendToBackAction->setToolTip(tr("Send to back"));
    // No explicit setToolTip(): QAction falls back to text(), which
    // QUndoStack keeps updated with the pending command's description
    // (e.g. "Undo Move Widget"); the shortcut still shows up in the menu/
    // button via QAction::shortcut(), it's just not spelled out in the text.
    m_undoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/true));
    m_redoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/false));
    m_fullscreenButton->setIcon(makeFullscreenIcon(palette.textPrimary, m_fullscreenButton->isChecked()));
}

void MainWindow::onRibbonTabChanged(int index) {
    m_configureTabActive = index == m_configureTabIndex;
    m_dashboardGrid->setEditMode(m_configureTabActive);
    m_addWidgetAction->setEnabled(m_configureTabActive);
    m_positionAction->setEnabled(m_configureTabActive);
    updatePanelVisibility();
    updateSelectionActions();

    if (index == m_runTabIndex) {
        refreshSerialPorts();
    }
}

void MainWindow::refreshSerialPorts() {
    // Preserve the current pick across a refresh when it's still present --
    // rebuilding the list would otherwise silently reset it to whatever
    // QSerialPortInfo happens to return first.
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    m_portCombo->addItems(m_serialManager->availablePorts());
    const int index = m_portCombo->findText(current);
    if (index >= 0) {
        m_portCombo->setCurrentIndex(index);
    }
}

void MainWindow::onSerialConnectToggled(bool checked) {
    if (checked) {
        if (!m_serialManager->open(m_portCombo->currentText(), m_baudCombo->currentText().toInt())) {
            // open() already reported the failure via errorOccurred(); just
            // snap the button back to reflect that the connection didn't
            // happen (re-enters this slot with checked=false, which calls
            // close() on an already-closed port -- a harmless no-op).
            m_connectButton->setChecked(false);
        }
        return;
    }
    m_serialManager->close();
}

void MainWindow::onSerialConnectionStateChanged(bool connected) {
    m_connectButton->setText(connected ? tr("Disconnect") : tr("Connect"));
    {
        const QSignalBlocker blocker(m_connectButton);
        m_connectButton->setChecked(connected);
    }
    m_portCombo->setEnabled(!connected);
    m_refreshPortsButton->setEnabled(!connected);
    m_baudCombo->setEnabled(!connected);
}

void MainWindow::onSerialErrorOccurred(const QString& message) {
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onLineTerminatorChanged(int) {
    m_serialManager->setLineTerminator(LineTerminator(m_lineTerminatorCombo->currentData().toInt()));
}

void MainWindow::onSelectionChanged(const QString&) {
    updateSelectionActions();
    refreshPropertiesPanel();
    refreshLayersPanel();
    updatePanelVisibility();
}

void MainWindow::updatePanelVisibility() {
    // Hiding a panel mid-drag would yank it out from under the cursor --
    // m_dockController re-triggers this itself (via dragFinished()) once the
    // gesture ends, so any visibility change that landed during it isn't
    // lost, just deferred.
    if (m_dockController->isDragging()) {
        return;
    }
    const bool hasSelection = !m_dashboardGrid->selectedItemId().isEmpty();
    const bool showProperties = m_configureTabActive && (hasSelection || m_propertiesPanel->isPinned());
    const bool showLayers = m_configureTabActive && (hasSelection || m_layersPanel->isPinned());
    m_propertiesPanel->setVisible(showProperties);
    m_layersPanel->setVisible(showLayers);
    // Both panels are floated over m_dashboardGrid rather than laid out
    // beside it (see positionOverlayPanels()), so re-showing one has to
    // explicitly reclaim the top of the stack -- a plain setVisible(true)
    // doesn't change sibling stacking order.
    if (showProperties) {
        m_propertiesPanel->raise();
    }
    if (showLayers) {
        m_layersPanel->raise();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_contentRow && event->type() == QEvent::Resize) {
        positionOverlayPanels();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::positionOverlayPanels() {
    m_dockController->relayout();
}

void MainWindow::updateSelectionActions() {
    const bool hasSelection = m_configureTabActive && !m_dashboardGrid->selectedItemId().isEmpty();
    m_removeAction->setEnabled(hasSelection);
    m_copyAction->setEnabled(hasSelection);
    m_pasteAction->setEnabled(m_configureTabActive && m_dashboardGrid->canPaste());
    m_bringToFrontAction->setEnabled(hasSelection);
    m_bringForwardAction->setEnabled(hasSelection);
    m_sendBackwardAction->setEnabled(hasSelection);
    m_sendToBackAction->setEnabled(hasSelection);
}

void MainWindow::refreshPropertiesPanel() {
    const bool hasSelection = !m_dashboardGrid->selectedItemId().isEmpty();
    m_propertiesPanel->setSelection(hasSelection, m_dashboardGrid->selectedItemTypeId(),
                                     m_dashboardGrid->selectedItemDisplayName(), m_dashboardGrid->selectedItemKey(),
                                     m_dashboardGrid->selectedItemConfig());
}

void MainWindow::refreshLayersPanel() {
    m_layersPanel->setItems(m_dashboardGrid->layerEntries(), m_dashboardGrid->selectedItemId());
}

void MainWindow::onAddWidget() {
    // Drops in the first registered type; DashboardGrid::addItem() selects
    // it immediately, so the properties panel comes up already showing it
    // — the type (and name/key) is picked there, not in a dialog upfront.
    const QVector<WidgetTypeInfo> types = WidgetRegistry::instance().availableTypes();
    if (types.isEmpty()) {
        return;
    }
    m_dashboardGrid->addItem(types.first().typeId);
}

void MainWindow::onPanelTypeChangeRequested(const QString& typeId) {
    m_dashboardGrid->changeSelectedType(typeId);
}

void MainWindow::onPanelNameChangeRequested(const QString& name) {
    m_dashboardGrid->renameSelected(name);
}

void MainWindow::onPanelKeyChangeRequested(const QString& key) {
    if (!m_dashboardGrid->setSelectedKey(key)) {
        statusBar()->showMessage(tr("Key \"%1\" is already used by another widget.").arg(key), 4000);
    }
    // Resyncs the field either way: on success to the committed value (a
    // no-op visually), on rejection to snap the text back to what's
    // actually stored instead of leaving the rejected input showing.
    refreshPropertiesPanel();
}

void MainWindow::onPanelConfigChangeRequested(const QJsonObject& config) {
    m_dashboardGrid->changeSelectedConfig(config);
}

void MainWindow::onNewProject() {
    if (QMessageBox::question(this, tr("New Project"),
                               tr("Discard the current dashboard and start a new, empty project?"),
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    ProjectStore::instance().reset();
    m_dashboardGrid->fromJson(QJsonObject());
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    statusBar()->showMessage(tr("Started a new project."), 3000);
}

void MainWindow::onSaveProject() {
    ProjectStore::instance().setSection("dashboard", m_dashboardGrid->toJson());

    QString path = ProjectStore::instance().currentPath();
    if (path.isEmpty()) {
        onSaveProjectAs();
        return;
    }

    if (!ProjectStore::instance().save()) {
        QMessageBox::warning(this, tr("Save Project"), ProjectStore::instance().lastError());
        return;
    }
    addRecentFile(path);
}

void MainWindow::onSaveProjectAs() {
    ProjectStore::instance().setSection("dashboard", m_dashboardGrid->toJson());

    const QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"), QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }

    if (!ProjectStore::instance().saveAs(path)) {
        QMessageBox::warning(this, tr("Save Project"), ProjectStore::instance().lastError());
        return;
    }
    addRecentFile(path);
}

void MainWindow::onOpenProject() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }
    openRecentFile(path);
}

void MainWindow::openRecentFile(const QString& path) {
    if (!ProjectStore::instance().load(path)) {
        QMessageBox::warning(this, tr("Open Project"), ProjectStore::instance().lastError());
        return;
    }

    m_dashboardGrid->fromJson(ProjectStore::instance().section("dashboard"));
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    addRecentFile(path);
}

void MainWindow::addRecentFile(const QString& path) {
    QSettings settings;
    QStringList files = settings.value(kRecentFilesSettingsKey).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecentFiles) {
        files.removeLast();
    }
    settings.setValue(kRecentFilesSettingsKey, files);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu() {
    m_recentFilesMenu->clear();

    const QSettings settings;
    const QStringList files = settings.value(kRecentFilesSettingsKey).toStringList();
    if (files.isEmpty()) {
        QAction* emptyAction = m_recentFilesMenu->addAction(tr("(No Recent Projects)"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& path : files) {
        QAction* action = m_recentFilesMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path]() { openRecentFile(path); });
    }

    m_recentFilesMenu->addSeparator();
    connect(m_recentFilesMenu->addAction(tr("Clear Recent Projects")), &QAction::triggered, this,
            &MainWindow::onClearRecentFiles);
}

void MainWindow::onClearRecentFiles() {
    QSettings settings;
    settings.remove(kRecentFilesSettingsKey);
    updateRecentFilesMenu();
}

void MainWindow::onAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::onDonate() {
    DonateDialog dialog(this);
    dialog.exec();
}

void MainWindow::onDebug() {
    // Reuses the existing window (raised to the front) instead of stacking
    // up a new synthetic-data feed/timer every click -- m_debugChartsWindow
    // resets to null on its own once the user closes it (WA_DeleteOnClose +
    // QPointer, see mainwindow.h).
    if (!m_debugChartsWindow) {
        m_debugChartsWindow = new DebugChartsWindow(this);
    }
    m_debugChartsWindow->show();
    m_debugChartsWindow->raise();
    m_debugChartsWindow->activateWindow();
}

void MainWindow::onFullscreenToggled(bool checked) {
    // On Windows this deliberately never touches Qt::WindowFullScreen /
    // showFullScreen() at all - confirmed via an A/B test (temporarily
    // routing through vanilla Qt showFullScreen()/showNormal()/
    // showMaximized() with none of the WinAPI code below) that the flicker
    // this function exists to avoid reproduces identically with plain Qt,
    // with zero custom code involved. That matches a long-standing,
    // still-open category of Qt-on-Windows bug (QTBUG-51093, QTBUG-47247,
    // years of forum reports, still reproducing on Qt6/Windows 11) in the
    // QPA windows backend's fullscreen transition - not something fixable
    // by reordering native calls layered on top of it, only by avoiding
    // that codepath entirely.
    //
    // Instead, "fullscreen" here just means: a borderless window resized to
    // exactly cover the monitor. That's the same "borderless windowed
    // fullscreen" idiom games and other native Windows apps use for the
    // same reason - to Windows it's an ordinary SetWindowPos, no different
    // from a user dragging the window's edge, so none of Qt's dedicated (and
    // apparently fragile) fullscreen state machine is ever engaged.
    if (checked) {
        // Brackets the native resize below AND the chrome hide() calls
        // further down in one suspended-repaint block: the native resize
        // alone already paints once (a borderless window at monitor size,
        // chrome still visible), and menuBar()->hide()/setTabBarVisible()
        // each schedule their own relayout/repaint on top of that - three
        // independent paints landing as three visible steps unless nothing
        // is allowed to paint until all of it is done.
        //
        // setUpdatesEnabled(false) alone doesn't achieve that on Windows: it
        // only suppresses Qt's own paint-event scheduling, not the WM_PAINT
        // that SetWindowPos(..., SWP_FRAMECHANGED) below forces synchronously
        // as part of the frame-style change, which lands (and is visible)
        // before menuBar()->hide()/setTabBarVisible() even run - that's the
        // "goes fullscreen with the menu still there, then the ribbon tabs
        // disappear a moment later" two-step. WM_SETREDRAW is the native
        // counterpart that actually blocks painting for this HWND at the
        // Win32 level regardless of source, so it's what suspends that
        // in-between frame; the RedrawWindow() call once chrome is hidden
        // forces the single final repaint everything was waiting for.
        setUpdatesEnabled(false);
#ifdef Q_OS_WIN
        // GetWindowPlacement(), not isMaximized()/GetWindowRect(): it hands
        // back Windows' own canonical "restore to" rectangle
        // (rcNormalPosition) together with the current show command in one
        // atomic snapshot. Recomputing that rectangle ourselves (e.g. from
        // GetWindowRect while maximized) doesn't work - it's the maximized
        // rect, not the rect the window should return to - and deriving our
        // own "was maximized" via a separate call/Qt's cached isMaximized()
        // can race or drift from what SetWindowPlacement() will restore.
        // Capturing the whole placement and replaying it verbatim on exit
        // keeps Windows' own restore bookkeeping intact, so a later manual
        // un-maximize (double-click title bar, Win+Down) still lands on the
        // right size instead of on a placement we half-guessed.
        HWND hwnd = reinterpret_cast<HWND>(winId());
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &placement);
        m_wasMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
        const RECT& normalRect = placement.rcNormalPosition;
        m_preFullscreenGeometry = QRect(normalRect.left, normalRect.top, normalRect.right - normalRect.left,
                                         normalRect.bottom - normalRect.top);

        // Strip the frame and cover whichever monitor the window is
        // currently on, in one atomic SetWindowPos. MonitorFromWindow()/
        // GetMonitorInfoW() report physical pixel bounds - the same
        // coordinate space winId()'s HWND already operates in as a
        // per-monitor-DPI-aware window - so this lines up exactly with no
        // manual DPI math, unlike going through QScreen::geometry() (Qt's
        // logical/scaled coordinate space) would require. Plain
        // SetWindowPos never invokes Windows' min/max show animation (that
        // only triggers via ShowWindow's SW_MAXIMIZE/MINIMIZE/RESTORE
        // codes) so there's nothing to suppress here either - it's a single
        // ordinary resize.
        //
        // Deliberately NOT animated: an earlier version of this code
        // animated the grow/shrink via ~60 real SetWindowPos calls over
        // 200ms. That reintroduced two new problems the instant version
        // didn't have - visible stutter/stepping (each tick forces a full
        // relayout+repaint of the live dashboard, which doesn't reliably
        // fit inside one frame's budget) and, more seriously, a corrupted
        // taskbar after exiting (rapid resizes through the taskbar's screen
        // region appear to confuse Explorer's own fullscreen-window
        // detection). Both were absent with a single atomic resize, so this
        // stays a snap rather than a tween.
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        style &= ~WS_OVERLAPPEDWINDOW;
        SetWindowLongW(hwnd, GWL_STYLE, style);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        const RECT& mr = monitorInfo.rcMonitor;
        SetWindowPos(hwnd, nullptr, mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
#else
        m_wasMaximized = isMaximized();
        m_preFullscreenGeometry = frameGeometry();
        showFullScreen();
#endif
        m_fullscreenButton->setToolTip(tr("Exit fullscreen (F11 / Esc)"));
        menuBar()->hide();
        m_ribbon->setTabBarVisible(false);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        setUpdatesEnabled(true);
        updateRibbonIcons();
        return;
    }

    m_fullscreenButton->setToolTip(tr("Fullscreen dashboard (F11)"));
    // Same reasoning as the entry path: brackets the frame/placement
    // restore below and the chrome show() calls further down so nothing
    // paints until the window is at its final size AND its chrome is back,
    // instead of those landing as separate visible steps.
    setUpdatesEnabled(false);

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style |= WS_OVERLAPPEDWINDOW;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    // SetWindowLongW's own docs: changing frame-related styles
    // (WS_OVERLAPPEDWINDOW includes WS_CAPTION/WS_THICKFRAME) only takes
    // effect once SetWindowPos is called with SWP_FRAMECHANGED. Without
    // this, Windows keeps computing the non-client area from the stale
    // borderless style, so the SW_SHOWMAXIMIZED placement below sizes the
    // window against the wrong frame metrics and its bottom edge ends up
    // extending under the taskbar. SWP_NOMOVE/SWP_NOSIZE keep this call from
    // moving/resizing anything itself - it exists purely to make the style
    // change above take effect before SetWindowPlacement runs.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // Both the maximized and plain-restore cases go through
    // SetWindowPlacement rather than a plain SetWindowPos: rcNormalPosition
    // (what m_preFullscreenGeometry was captured from, on entry) is
    // documented to be in *workspace* coordinates - relative to the
    // monitor's work area, which excludes the taskbar - not screen
    // coordinates. GetWindowPlacement/SetWindowPlacement agree on that
    // convention between themselves, but a plain SetWindowPos expects
    // screen coordinates; feeding it workspace coordinates silently shifted
    // the restored window by the taskbar's thickness, leaving its bottom
    // edge hidden behind it. Going through SetWindowPlacement both ways
    // keeps the coordinate space consistent regardless of where the
    // taskbar is docked.
    //
    // Genuine maximize is additionally a distinct OS-tracked state (affects
    // Aero Snap, what double-click-titlebar toggles back to, etc.), not
    // just "resized to look like the maximized rect", so SW_SHOWMAXIMIZED
    // here isn't only about coordinates - it's the only way into that state
    // at all. Either showCmd goes through ShowWindow's codepath, which
    // re-invokes Windows' min/max show animation - suspend it for just this
    // call and restore the user's setting right after. There's no
    // multi-step sequence left for it to mask a gap in: the frame style
    // change above paints nothing by itself (see the entry path's
    // SetWindowLongW comment for why), so this placement call is the only
    // paint in the sequence.
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    placement.showCmd = m_wasMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    placement.rcNormalPosition = RECT{m_preFullscreenGeometry.left(), m_preFullscreenGeometry.top(),
                                       m_preFullscreenGeometry.left() + m_preFullscreenGeometry.width(),
                                       m_preFullscreenGeometry.top() + m_preFullscreenGeometry.height()};

    ANIMATIONINFO animationInfo{};
    animationInfo.cbSize = sizeof(animationInfo);
    SystemParametersInfoW(SPI_GETANIMATION, sizeof(animationInfo), &animationInfo, 0);
    const int previousMinAnimate = animationInfo.iMinAnimate;
    animationInfo.iMinAnimate = 0;
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(animationInfo), &animationInfo, 0);

    SetWindowPlacement(hwnd, &placement);

    animationInfo.iMinAnimate = previousMinAnimate;
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(animationInfo), &animationInfo, 0);
#else
    if (m_wasMaximized) {
        showMaximized();
    } else {
        showNormal();
        // m_preFullscreenGeometry is captured via frameGeometry() above, so
        // restore it the same way (frame included), not via setGeometry()
        // which would place just the client area at that rect.
        setFrameGeometry(m_preFullscreenGeometry);
    }
#endif
    menuBar()->show();
    m_ribbon->setTabBarVisible(true);
#ifdef Q_OS_WIN
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
#endif
    setUpdatesEnabled(true);
    updateRibbonIcons();
}

} // namespace traceview
