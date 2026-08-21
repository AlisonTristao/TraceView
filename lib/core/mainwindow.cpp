#include "mainwindow.h"

#include <QActionGroup>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QToolButton>
#include <QUndoGroup>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "donatedialog.h"
#include "backend/backend.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgetregistry.h"
#include "dashboard/widgets/chartwidgets.h"
#include "debugchartswindow.h"
#include "deviceconnection.h"
#include "devices/devicesgrid.h"
#include "usbhidmanager.h"
#include "layerspanel.h"
#include "logs/logviewer.h"
#include "paneldockcontroller.h"
#include "project/projectstore.h"
#include "project/workspacemanager.h"
#include "propertiespanel.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "serialwidgetbridge.h"
#include "workspaceswitcher.h"
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

// The (device, source_id, topic_id, requested rate) one dashboard widget
// implies. deviceId empty and the rest zero for a widget that is not a
// telemetry consumer, one whose source/topic has not been configured yet, or
// one with no device picked in its config editor's Device combo (see
// dashboard/widgetconfigeditor.h's DeviceOption) -- MainWindow treats that as
// "no consumer" and puts nothing on any wire.
struct WidgetTopicRequest {
    QString deviceId;
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint32 rateMillihz = 0;
};

WidgetTopicRequest widgetTopicRequest(DashboardWidget* widget, DashboardGrid* grid) {
    const QString deviceId = grid->configForWidget(widget).value("deviceId").toString();
    if (auto* chart = dynamic_cast<ChartWidgetBase*>(widget)) {
        const ChartConfig& config = chart->config();
        return {deviceId, config.sourceId, config.topicId, requestedRateMillihzFor(config.sampleTimeMs)};
    }
    if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(widget)) {
        const GaugeConfig& config = gauge->config();
        return {deviceId, config.sourceId, config.topicId, kGaugeRequestedRateMillihz};
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

    m_dashboardGrid = new DashboardGrid(this);

    // Wires control-widget commands and the serial monitor/terminal to
    // whichever device each widget's own config currently targets -- see
    // core/serialwidgetbridge.h. Resolved lazily via m_deviceConnections
    // (empty right now; devices are added later via the Devices tab), so
    // construction order relative to onDeviceAdded() doesn't matter.
    m_serialWidgetBridge =
        new SerialWidgetBridge(m_dashboardGrid, [this](const QString& id) { return m_deviceConnections.value(id); },
                                this);

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

    // The Devices tab swaps this whole area for m_devicesGrid instead of
    // sharing the row with the canvas -- unlike the layers/properties
    // panels above, DevicesGrid isn't subject to DashboardGrid's
    // fraction-of-canvas geometry, so a plain QStackedWidget (real layout
    // space, not a float) is fine here.
    m_devicesGrid = new DevicesGrid(this);
    m_undoGroup->addStack(m_devicesGrid->undoStack());
    connect(m_removeDeviceAction, &QAction::triggered, m_devicesGrid, &DevicesGrid::removeSelected);
    connect(m_devicesGrid, &DevicesGrid::selectionChanged, this, &MainWindow::updateDeviceSelectionActions);
    // Keeps m_deviceConnections (one DeviceConnection per Device -- see
    // core/deviceconnection.h) in lockstep with m_devicesGrid's own list.
    connect(m_devicesGrid, &DevicesGrid::deviceAdded, this, &MainWindow::onDeviceAdded);
    connect(m_devicesGrid, &DevicesGrid::deviceRemoved, this, &MainWindow::onDeviceRemoved);
    connect(m_devicesGrid, &DevicesGrid::deviceUpdated, this, &MainWindow::onDeviceUpdated);
    connect(m_devicesGrid, &DevicesGrid::connectToggleRequested, this,
            &MainWindow::onDeviceConnectToggleRequested);
    // DevicesGrid can't enumerate ports itself (traceview_devices doesn't
    // depend on QSerialPort, see lib/CMakeLists.txt) -- MainWindow supplies
    // the live list DeviceConfigDialog's port combo offers.
    m_devicesGrid->setPortListProvider([]() -> QStringList {
        QStringList names;
        const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
        names.reserve(infos.size());
        for (const QSerialPortInfo& info : infos) {
            names.append(info.portName());
        }
        return names;
    });
    // Same reasoning as setPortListProvider() above, for the USB device
    // combo -- DevicesGrid can't enumerate HID devices itself
    // (traceview_devices doesn't depend on hidapi, see lib/CMakeLists.txt).
    m_devicesGrid->setUsbDeviceListProvider([]() -> QVector<UsbDeviceOption> {
        QVector<UsbDeviceOption> options;
        const QVector<UsbHidManager::DeviceInfo> devices = UsbHidManager::availableDevices();
        options.reserve(devices.size());
        for (const UsbHidManager::DeviceInfo& device : devices) {
            options.append({device.path, device.label});
        }
        return options;
    });
    // Same reasoning as setPortListProvider() above: DevicesGrid can't reach
    // a Backend itself (traceview_devices doesn't depend on
    // traceview_protocol), so MainWindow supplies the gear icon's "Reported
    // catalog" list from whichever DeviceConnection is live for that id.
    m_devicesGrid->setTopicCatalogProvider([this](const QString& deviceId) -> QVector<CatalogTopicInfo> {
        DeviceConnection* connection = m_deviceConnections.value(deviceId);
        return connection ? connection->backend()->catalogTopics() : QVector<CatalogTopicInfo>();
    });
    refreshDeviceStatusLabel(); // starts empty ("No devices configured...")
    refreshPropertiesPanelDevices();

    // Logs tab's content -- opened on demand via m_openLogFileAction, no
    // state to wire up front (contrast m_devicesGrid above).
    m_logViewer = new LogViewer(this);

    m_contentStack = new QStackedWidget(m_contentRow);
    m_contentStack->addWidget(m_dashboardGrid);
    m_contentStack->addWidget(m_devicesGrid);
    m_contentStack->addWidget(m_logViewer);
    contentLayout->addWidget(m_contentStack);

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

    // Added last so it lands to the right of m_telemetryStatusLabel above --
    // each addPermanentWidget() call appends further right.
    buildWorkspaceSwitcher();
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
    if (!dynamic_cast<ChartWidgetBase*>(widget) && !dynamic_cast<DummyGaugeWidget*>(widget)) {
        return;
    }

    // PASSO 5 (topico 17): closing a widget only drops its reference; an
    // unsubscribe is sent only when it was the last consumer of that topic
    // -- against whichever device's Backend it was actually registered
    // with, which is why WidgetSubscription remembers deviceId alongside
    // the handle now.
    connect(widget, &QObject::destroyed, this, [this, widget] {
        const WidgetSubscription sub = m_widgetSubscriptions.take(widget);
        if (DeviceConnection* connection = m_deviceConnections.value(sub.deviceId)) {
            connection->backend()->removeSubscriber(sub.handle);
        }
    });

    refreshWidgetSubscription(widget);
}

void MainWindow::refreshWidgetSubscription(DashboardWidget* widget) {
    const WidgetTopicRequest request = widgetTopicRequest(widget, m_dashboardGrid);
    WidgetSubscription& sub = m_widgetSubscriptions[widget];

    DeviceConnection* oldConnection = m_deviceConnections.value(sub.deviceId);
    DeviceConnection* newConnection = m_deviceConnections.value(request.deviceId);

    // A device change (or the old one going away) invalidates both the old
    // subscription handle (it belongs to a different Backend/
    // SubscriptionManager instance) and the fieldSample connection feeding
    // this widget.
    if (oldConnection && oldConnection != newConnection && sub.handle != 0) {
        oldConnection->backend()->removeSubscriber(sub.handle);
        disconnect(oldConnection->backend(), &Backend::fieldSample, widget, nullptr);
    }

    if (!newConnection) {
        sub.deviceId = request.deviceId;
        sub.handle = 0;
        return;
    }

    if (oldConnection != newConnection) {
        if (auto* chart = dynamic_cast<ChartWidgetBase*>(widget)) {
            connect(newConnection->backend(), &Backend::fieldSample, chart, &ChartWidgetBase::onFieldSample);
        } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(widget)) {
            connect(newConnection->backend(), &Backend::fieldSample, gauge, &DummyGaugeWidget::onFieldSample);
        }
    }

    // topico 17 PASSO 2: this widget is one *reference* to its topic, not a
    // subscription of its own -- Backend collapses however many widgets
    // read (source, topic) into a single subscription. Reusing the existing
    // handle only makes sense while staying on the same Backend instance.
    const quint64 handleToReuse = oldConnection == newConnection ? sub.handle : 0;
    sub.handle = newConnection->backend()->updateSubscriber(handleToReuse, request.sourceId, request.topicId,
                                                              request.rateMillihz);
    sub.deviceId = request.deviceId;
}

void MainWindow::refreshWidgetSubscriptions() {
    const QList<DashboardWidget*> widgets = m_widgetSubscriptions.keys();
    for (DashboardWidget* widget : widgets) {
        refreshWidgetSubscription(widget);
    }
    // A config edit can just as well have re-pointed a terminal widget at a
    // different device -- its inbound wiring needs the same kind of refresh
    // subscriptions just got, see SerialWidgetBridge::refreshTerminalWiring().
    if (m_serialWidgetBridge) {
        m_serialWidgetBridge->refreshTerminalWiring();
    }
}

void MainWindow::updateTelemetryStatusLabel() {
    if (!m_telemetryStatusLabel) {
        return;
    }

    // Aggregated across every connected device's Backend -- (sourceId,
    // topicId) is only unique *within* one device's session, so two
    // different devices reporting the same pair would collide in
    // statusByTopic; harmless here since this is a human-readable summary,
    // not anything routing decisions depend on.
    QVector<TopicSubscriptionState> states;
    QHash<quint64, StatusTopicRecord> statusByTopic;
    // Resolved from each device's catalog (MANIFEST_DATA) so the summary
    // below can show "motor_state" instead of a bare "0x11223344/0x0101" --
    // falls back to the hex pair for a topic whose schema hasn't arrived
    // (or isn't known) yet.
    QHash<quint64, QString> topicNames;
    const QList<DeviceConnection*> connections = m_deviceConnections.values();
    for (DeviceConnection* connection : connections) {
        Backend* backend = connection->backend();
        states += backend->subscriptions();
        for (const StatusTopicRecord& record : backend->topicStatuses()) {
            statusByTopic.insert(topicStatusKey(record.sourceId, record.topicId), record);
        }
        for (const CatalogTopicInfo& topic : backend->catalogTopics()) {
            topicNames.insert(topicStatusKey(topic.sourceId, topic.topicId), topic.name);
        }
    }

    if (states.isEmpty()) {
        m_telemetryStatusLabel->clear();
        m_telemetryStatusLabel->setToolTip(QString());
        return;
    }

    QStringList summary;
    QStringList detail;
    for (const TopicSubscriptionState& state : states) {
        const QString rawLabel = QString("0x%1/0x%2")
                                      .arg(state.sourceId, 8, 16, QChar('0'))
                                      .arg(state.topicId, 4, 16, QChar('0'));
        const QString resolvedName = topicNames.value(topicStatusKey(state.sourceId, state.topicId));
        const QString topicLabel = resolvedName.isEmpty() ? rawLabel : resolvedName;
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

        QString line = entry;
        if (!resolvedName.isEmpty()) {
            line += QString(" (%1)").arg(rawLabel);
        }
        line += QString(" | %1 widget(s) here").arg(state.subscriberCount);
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
    debugAction->setVisible(false);

    auto* aboutAction = menuBar()->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    auto* donateAction = menuBar()->addAction(tr("Dona&te"));
    connect(donateAction, &QAction::triggered, this, &MainWindow::onDonate);
}

Ribbon* MainWindow::buildRibbon() {
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

    m_groupAction = new QAction(tr("Group"), this);
    m_groupAction->setEnabled(false);
    connect(m_groupAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::groupSelected);

    m_ungroupAction = new QAction(tr("Ungroup"), this);
    m_ungroupAction->setEnabled(false);
    connect(m_ungroupAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::ungroupSelected);

    // Two independent QUndoStacks (dashboard widgets, devices) share one
    // Undo/Redo pair via QUndoGroup: m_undoGroup tracks which stack is
    // "active" (flipped in onRibbonTabChanged() to match the visible tab),
    // and createUndoAction()/createRedoAction() on the *group* wire up
    // triggered/enabled state (and a dynamic "Undo <command text>" label)
    // from whichever stack that is -- no manual canUndo()/canRedo() syncing,
    // and Ctrl+Z always acts on what's actually on screen instead of always
    // hitting the dashboard regardless of tab. m_devicesGrid doesn't exist
    // yet at this point (built after buildRibbon() returns, see the
    // constructor) -- its stack joins the group there, same reasoning as
    // m_removeDeviceAction's own connect() right after that construction.
    m_undoGroup = new QUndoGroup(this);
    m_undoGroup->addStack(m_dashboardGrid->undoStack());
    m_undoAction = m_undoGroup->createUndoAction(this, tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_undoGroup->createRedoAction(this, tr("Redo"));
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

    m_addDeviceAction = new QAction(tr("Add Device"), this);
    connect(m_addDeviceAction, &QAction::triggered, this, &MainWindow::onAddDevice);

    // Wired to m_devicesGrid once it exists (right after its own
    // construction in MainWindow::MainWindow()) -- it isn't built yet at
    // this point, since buildRibbon() runs before it, same reason
    // m_removeAction above is wired straight to m_dashboardGrid but this one
    // can't be wired to m_devicesGrid here.
    m_removeDeviceAction = new QAction(tr("Remove Device"), this);
    m_removeDeviceAction->setEnabled(false);
    m_removeDeviceAction->setShortcut(QKeySequence::Delete);

    m_openLogFileAction = new QAction(tr("Open Log File..."), this);
    connect(m_openLogFileAction, &QAction::triggered, this, &MainWindow::onOpenLogFile);

    auto* runPage = new QWidget(this);
    runPage->setObjectName("ribbonPage");
    runPage->setFixedHeight(kRibbonPageHeight);
    auto* runLayout = new QHBoxLayout(runPage);
    runLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    runLayout->setSpacing(kRibbonGroupSpacing);

    // Port/baud/connect used to live here as one global bar (see git history
    // pre-multi-device-refactor) -- each device now owns its own connection
    // config, set in the Devices tab (DeviceConfigDialog). This read-only
    // strip is what's left for Run: an at-a-glance glance at every
    // configured device's live connection state, for when you're looking at
    // the dashboard and not at any cell bound to a disconnected device.
    m_deviceStatusLabel = new QLabel(runPage);
    m_deviceStatusLabel->setTextFormat(Qt::RichText);

    // Lives in the status bar (bottom-left, see below) rather than on this
    // page: the ribbon's tab strip hides during fullscreen
    // (onFullscreenToggled -- m_ribbon->setTabBarVisible(false)), which
    // used to be harmless for this button since the Run page itself stayed
    // visible, but placing the workspace switcher in the ribbon's tab row
    // (topico's earlier design) broke under that same hide. The status bar
    // is never touched by fullscreen, so anything anchored there survives
    // it for free.
    m_fullscreenButton = new QToolButton(this);
    m_fullscreenButton->setCheckable(true);
    m_fullscreenButton->setAutoRaise(true);
    m_fullscreenButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_fullscreenButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_fullscreenButton->setToolTip(tr("Fullscreen dashboard (F11)"));
    connect(m_fullscreenButton, &QToolButton::toggled, this, &MainWindow::onFullscreenToggled);
    statusBar()->addWidget(m_fullscreenButton);

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

    // Window-level, same reasoning as fullscreen/exitFullscreen above --
    // browser/editor-style tab cycling (Ctrl+Tab forward, Ctrl+Shift+Tab
    // back) through WorkspaceManager's own list order. m_workspaceSwitcher
    // doesn't exist yet at this point (built last in the constructor) but
    // these only fire on a later keypress, well after construction finishes.
    auto* nextWorkspaceAction = new QAction(this);
    nextWorkspaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
    addAction(nextWorkspaceAction);
    connect(nextWorkspaceAction, &QAction::triggered, this, [this]() { cycleWorkspace(1); });

    auto* previousWorkspaceAction = new QAction(this);
    previousWorkspaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
    addAction(previousWorkspaceAction);
    connect(previousWorkspaceAction, &QAction::triggered, this, [this]() { cycleWorkspace(-1); });

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    runLayout->addWidget(m_deviceStatusLabel);
    runLayout->addStretch();

    // Initial refreshDeviceStatusLabel() call happens once m_devicesGrid
    // exists (buildRibbon() runs before it -- see the constructor); this
    // just keeps it live across a later theme change (dot colors).
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { refreshDeviceStatusLabel(); });

    auto* configurePage = new QWidget(this);
    configurePage->setObjectName("ribbonPage");
    configurePage->setFixedHeight(kRibbonPageHeight);
    auto* configureLayout = new QHBoxLayout(configurePage);
    configureLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    configureLayout->setSpacing(kRibbonGroupSpacing);

    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_addWidgetAction, m_removeAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(
        configurePage, {m_bringToFrontAction, m_bringForwardAction, m_sendBackwardAction, m_sendToBackAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_copyAction, m_pasteAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_groupAction, m_ungroupAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_undoAction, m_redoAction}));
    configureLayout->addStretch();

    auto* devicesPage = new QWidget(this);
    devicesPage->setObjectName("ribbonPage");
    devicesPage->setFixedHeight(kRibbonPageHeight);
    auto* devicesLayout = new QHBoxLayout(devicesPage);
    devicesLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    devicesLayout->setSpacing(kRibbonGroupSpacing);

    devicesLayout->addWidget(Ribbon::createButtonGroup(devicesPage, {m_addDeviceAction, m_removeDeviceAction}));
    devicesLayout->addStretch();

    auto* logsPage = new QWidget(this);
    logsPage->setObjectName("ribbonPage");
    logsPage->setFixedHeight(kRibbonPageHeight);
    auto* logsLayout = new QHBoxLayout(logsPage);
    logsLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    logsLayout->setSpacing(kRibbonGroupSpacing);

    logsLayout->addWidget(Ribbon::createButtonGroup(logsPage, {m_openLogFileAction}));
    logsLayout->addStretch();

    auto* ribbon = new Ribbon(this);
    m_runTabIndex = ribbon->addTab(tr("Run"), runPage);
    m_configureTabIndex = ribbon->addTab(tr("Layout"), configurePage);
    m_devicesTabIndex = ribbon->addTab(tr("Devices"), devicesPage);
    m_logsTabIndex = ribbon->addTab(tr("Logs"), logsPage);

    connect(ribbon, &Ribbon::currentTabChanged, this, &MainWindow::onRibbonTabChanged);

    m_ribbon = ribbon;
    return ribbon;
}

void MainWindow::buildWorkspaceSwitcher() {
    // Status bar (bottom-right), not the ribbon: see the comment above
    // m_fullscreenButton's construction in buildRibbon() for why -- the
    // ribbon's tab row is hidden while fullscreen, which this widget used
    // to live inside via Ribbon::setCornerWidget().
    m_workspaceSwitcher = new WorkspaceSwitcher(this);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::workspaceSelected, this, &MainWindow::onWorkspaceSelected);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::workspaceDeleteRequested, this,
            &MainWindow::onWorkspaceDeleteRequested);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::newWorkspaceRequested, this,
            &MainWindow::onNewWorkspaceRequested);
    m_workspaceSwitcher->updateIcons(ThemeManager::instance().currentTheme().textPrimary);
    refreshWorkspaceSwitcher();
    statusBar()->addPermanentWidget(m_workspaceSwitcher);
}

void MainWindow::refreshWorkspaceSwitcher() {
    QVector<WorkspaceSwitcher::Entry> entries;
    for (const Workspace& workspace : WorkspaceManager::instance().workspaces()) {
        entries.append({workspace.id, workspace.name});
    }
    m_workspaceSwitcher->setWorkspaces(entries, WorkspaceManager::instance().activeId());
}

void MainWindow::switchToWorkspace(const QString& id) {
    WorkspaceManager& workspaces = WorkspaceManager::instance();
    if (id == workspaces.activeId()) {
        return;
    }

    workspaces.setDashboardFor(workspaces.activeId(), m_dashboardGrid->toJson());
    workspaces.setActiveId(id);
    m_dashboardGrid->fromJson(workspaces.dashboardFor(id));
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
}

void MainWindow::cycleWorkspace(int direction) {
    const QVector<Workspace>& workspaces = WorkspaceManager::instance().workspaces();
    if (workspaces.size() < 2) {
        return;
    }

    const QString activeId = WorkspaceManager::instance().activeId();
    int index = 0;
    for (int i = 0; i < workspaces.size(); ++i) {
        if (workspaces[i].id == activeId) {
            index = i;
            break;
        }
    }

    const int nextIndex = (index + direction + workspaces.size()) % workspaces.size();
    switchToWorkspace(workspaces[nextIndex].id);
}

void MainWindow::onWorkspaceSelected(const QString& id) {
    switchToWorkspace(id);
}

void MainWindow::onNewWorkspaceRequested() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Workspace"), tr("Name:"), QLineEdit::Normal,
                                                 tr("Workspace"), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    WorkspaceManager& workspaces = WorkspaceManager::instance();
    workspaces.setDashboardFor(workspaces.activeId(), m_dashboardGrid->toJson());
    workspaces.createWorkspace(name.trimmed());
    m_dashboardGrid->fromJson(QJsonObject());
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
    statusBar()->showMessage(tr("Created workspace \"%1\".").arg(name.trimmed()), 3000);
}

void MainWindow::onWorkspaceDeleteRequested(const QString& id) {
    WorkspaceManager& workspaces = WorkspaceManager::instance();
    if (workspaces.workspaces().size() <= 1) {
        return;
    }

    const QString name = workspaces.nameFor(id);
    if (QMessageBox::question(this, tr("Delete Workspace"),
                               tr("Delete workspace \"%1\"? This cannot be undone.").arg(name),
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const bool wasActive = id == workspaces.activeId();
    workspaces.removeWorkspace(id);
    if (wasActive) {
        m_dashboardGrid->fromJson(workspaces.dashboardFor(workspaces.activeId()));
        m_dashboardGrid->undoStack()->clear();
        refreshPropertiesPanel();
        refreshLayersPanel();
    }
    refreshWorkspaceSwitcher();
    statusBar()->showMessage(tr("Deleted workspace \"%1\".").arg(name), 3000);
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

    // Only relevant while editing the layout — matches m_addWidgetAction,
    // which also starts disabled until the Layout tab is active (see
    // onRibbonTabChanged).
    m_propertiesPanel->hide();
}

void MainWindow::updateRibbonIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_addWidgetAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addWidgetAction->setToolTip(tr("Add widget"));
    m_removeAction->setIcon(makeMinusIcon(palette.danger));
    m_removeAction->setToolTip(tr("Remove selected widget (%1)")
                                    .arg(m_removeAction->shortcut().toString(QKeySequence::NativeText)));
    m_addDeviceAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addDeviceAction->setToolTip(tr("Add device"));
    m_removeDeviceAction->setIcon(makeMinusIcon(palette.danger));
    m_removeDeviceAction->setToolTip(tr("Remove selected device (%1)")
                                          .arg(m_removeDeviceAction->shortcut().toString(QKeySequence::NativeText)));
    m_openLogFileAction->setIcon(makeFolderIcon(palette.textPrimary));
    m_openLogFileAction->setToolTip(tr("Open a .blog log file"));
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
    m_groupAction->setIcon(makeGroupIcon(palette.textPrimary));
    m_groupAction->setToolTip(tr("Group — lock the selected widgets' positions together"));
    m_ungroupAction->setIcon(makeUngroupIcon(palette.textPrimary));
    m_ungroupAction->setToolTip(tr("Ungroup — let the selected widgets move independently again"));
    // No explicit setToolTip(): QAction falls back to text(), which
    // QUndoStack keeps updated with the pending command's description
    // (e.g. "Undo Move Widget"); the shortcut still shows up in the menu/
    // button via QAction::shortcut(), it's just not spelled out in the text.
    m_undoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/true));
    m_redoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/false));
    m_fullscreenButton->setIcon(makeFullscreenIcon(palette.textPrimary, m_fullscreenButton->isChecked()));
    if (m_workspaceSwitcher) {
        m_workspaceSwitcher->updateIcons(palette.textPrimary);
    }
}

void MainWindow::onRibbonTabChanged(int index) {
    m_configureTabActive = index == m_configureTabIndex;
    m_devicesTabActive = index == m_devicesTabIndex;
    m_dashboardGrid->setEditMode(m_configureTabActive);
    m_addWidgetAction->setEnabled(m_configureTabActive);
    // Ctrl+Z/Ctrl+Y (m_undoAction/m_redoAction) are created from m_undoGroup,
    // not either stack directly -- flip which one is "active" here so they
    // always undo/redo whatever the visible tab actually shows. Run and
    // Layout both display the dashboard, so both fall through to its stack.
    m_undoGroup->setActiveStack(m_devicesTabActive ? m_devicesGrid->undoStack() : m_dashboardGrid->undoStack());
    updatePanelVisibility();
    updateSelectionActions();
    updateDeviceSelectionActions();

    if (index == m_runTabIndex) {
        refreshDeviceStatusLabel();
    }

    // Swaps the whole content area between the canvas, the Devices grid and
    // the Logs viewer; Run/Layout both keep showing the canvas, only
    // Devices/Logs swap away from it (m_configureTabActive above already
    // goes false here on its own, so edit mode/panels don't need any extra
    // handling for either tab).
    QWidget* activeContent = m_dashboardGrid;
    if (index == m_devicesTabIndex) {
        activeContent = m_devicesGrid;
    } else if (index == m_logsTabIndex) {
        activeContent = m_logViewer;
    }
    m_contentStack->setCurrentWidget(activeContent);
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
    const bool hasSelection = m_dashboardGrid->selectedCount() > 0;
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
    const bool hasAnySelection = m_configureTabActive && m_dashboardGrid->selectedCount() > 0;
    const bool hasSingleSelection = m_configureTabActive && !m_dashboardGrid->selectedItemId().isEmpty();
    m_removeAction->setEnabled(hasAnySelection);
    m_copyAction->setEnabled(hasSingleSelection);
    m_pasteAction->setEnabled(m_configureTabActive && m_dashboardGrid->canPaste());
    m_bringToFrontAction->setEnabled(hasSingleSelection);
    m_bringForwardAction->setEnabled(hasSingleSelection);
    m_sendBackwardAction->setEnabled(hasSingleSelection);
    m_sendToBackAction->setEnabled(hasSingleSelection);
    m_groupAction->setEnabled(m_configureTabActive && m_dashboardGrid->selectedCount() >= 2);
    m_ungroupAction->setEnabled(m_configureTabActive && m_dashboardGrid->selectionHasGroup());
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

void MainWindow::onAddDevice() {
    // Drops in a placeholder mock device -- no picker dialog, same shape as
    // onAddWidget() above. DevicesGrid owns opening DeviceConfigDialog
    // itself (gear click on the new card), so renaming just happens there;
    // this slot doesn't reach into that flow.
    Device device;
    device.name = tr("New Device");
    m_devicesGrid->addDevice(device);
}

void MainWindow::updateDeviceSelectionActions() {
    m_removeDeviceAction->setEnabled(m_devicesTabActive && m_devicesGrid->selectedCount() > 0);
}

void MainWindow::refreshPropertiesPanelDevices() {
    const QVector<Device> devices = m_devicesGrid->devices();
    QVector<DeviceOption> options;
    options.reserve(devices.size());
    for (const Device& device : devices) {
        DeviceOption option{device.id, device.name, {}};
        if (DeviceConnection* connection = m_deviceConnections.value(device.id)) {
            option.catalogTopics = connection->backend()->catalogTopics();
        }
        options.append(option);
    }
    m_propertiesPanel->setAvailableDevices(options);
}

void MainWindow::refreshDeviceStatusLabel() {
    if (!m_deviceStatusLabel) {
        return;
    }
    const QVector<Device> devices = m_devicesGrid->devices();
    if (devices.isEmpty()) {
        m_deviceStatusLabel->setText(tr("No devices configured — add one in the Devices tab."));
        return;
    }

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QStringList parts;
    for (const Device& device : devices) {
        const QColor dotColor = device.connected ? palette.success : palette.danger;
        const QString name = device.name.isEmpty() ? tr("(unnamed)") : device.name;
        parts.append(
            QString("<span style='color:%1;'>&#9679;</span> %2").arg(dotColor.name(), name.toHtmlEscaped()));
    }
    m_deviceStatusLabel->setText(parts.join("&nbsp;&nbsp;&nbsp;&nbsp;"));
}

DeviceConnection* MainWindow::createDeviceConnection(const Device& device) {
    auto* connection = new DeviceConnection(device.commType, device.transportType, this);
    connect(connection, &DeviceConnection::connectionStateChanged, this,
            [this, id = device.id](bool connected) { onDeviceConnectionStateChanged(id, connected); });

    // Everything that gives this device's transport bytes meaning lives
    // behind the Backend interface (backend/backend.h) -- concretely a
    // BtpBackend today, owned internally by DeviceConnection. Hooked here,
    // once per device, the same way MainWindow used to hook the app's one
    // Backend before the multi-device refactor.
    Backend* backend = connection->backend();
    connect(backend, &Backend::statusMessage, this,
            [this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
    connect(backend, &Backend::subscriptionsChanged, this, &MainWindow::updateTelemetryStatusLabel);
    connect(backend, &Backend::statusReceived, this, &MainWindow::updateTelemetryStatusLabel);
    // A manifest exchange completing/updating is when catalogTopics() first
    // has (or changes) the readable names chart/gauge config editors resolve
    // sourceId/topicId against -- refresh their cached DeviceOption list
    // rather than only doing so on device add/remove/rename.
    connect(backend, &Backend::catalogChanged, this, &MainWindow::refreshPropertiesPanelDevices);
    // setDeviceIdentity(), not updateDevice() -- same reasoning as
    // onDeviceConnectionStateChanged()'s setDeviceConnected() call below: this
    // is the handshake reporting live state, not a user edit, so it must not
    // land on m_devicesGrid's undo stack.
    connect(connection, &DeviceConnection::deviceIdentified, this,
            [this, id = device.id](const QString& btpVersion, const QString& btpId) {
                m_devicesGrid->setDeviceIdentity(id, btpVersion, btpId);
            });
    return connection;
}

void MainWindow::onDeviceAdded(const Device& device) {
    DeviceConnection* connection = createDeviceConnection(device);
    m_deviceConnections.insert(device.id, connection);
    connection->setLineTerminator(device.lineTerminator);
    // A no-op if `device` has no target yet (a freshly added placeholder,
    // see onAddDevice()) -- otherwise opens now, or starts the ambient retry
    // loop, e.g. right after loading a saved project whose devices already
    // have one configured. Target is portName for Serial, usbPath for
    // UsbHid (baudRate is ignored by DeviceConnection in that case).
    const QString target = device.transportType == TransportType::UsbHid ? device.usbPath : device.portName;
    connection->connectTo(target, device.baudRate);
    refreshDeviceStatusLabel();
    refreshPropertiesPanelDevices();
}

void MainWindow::onDeviceRemoved(const QString& id) {
    DeviceConnection* connection = m_deviceConnections.take(id);
    if (!connection) {
        return;
    }
    connection->disconnectFrom();
    connection->deleteLater();
    refreshDeviceStatusLabel();
    refreshPropertiesPanelDevices();
}

void MainWindow::onDeviceUpdated(const Device& device) {
    DeviceConnection* connection = m_deviceConnections.value(device.id);
    if (!connection) {
        return;
    }
    if (connection->transportType() != device.transportType) {
        // DeviceConnection's Transport/Backend pair is fixed at construction
        // (deviceconnection.h) -- can't repoint a live SerialManager-backed
        // connection at a HID path or vice versa. Rebuild it in place, same
        // teardown/build steps onDeviceRemoved()/onDeviceAdded() use, so
        // every signal connection keyed by this device's id (properties
        // panel, control widgets, ...) keeps resolving through
        // m_deviceConnections without a remove/re-add round trip through
        // the undo stack.
        connection->disconnectFrom();
        connection->deleteLater();
        connection = createDeviceConnection(device);
        m_deviceConnections.insert(device.id, connection);
    }
    connection->setLineTerminator(device.lineTerminator);
    const QString target = device.transportType == TransportType::UsbHid ? device.usbPath : device.portName;
    connection->connectTo(target, device.baudRate);
    refreshDeviceStatusLabel();
    // Renaming/reconfiguring a device changes what every widget's own
    // Device combo should show as its selected entry's label.
    refreshPropertiesPanelDevices();
}

void MainWindow::onDeviceConnectToggleRequested(const QString& deviceId) {
    DeviceConnection* connection = m_deviceConnections.value(deviceId);
    if (!connection) {
        return;
    }
    if (connection->wantsConnection()) {
        connection->disconnectFrom();
        return;
    }
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.id == deviceId) {
            connection->connectTo(device.portName, device.baudRate);
            break;
        }
    }
}

void MainWindow::onDeviceConnectionStateChanged(const QString& deviceId, bool connected) {
    // Every chart/gauge/control/terminal cell currently configured for this
    // device (config()["deviceId"]) -- not just the Devices tab's own card.
    m_dashboardGrid->setDeviceConnected(deviceId, connected);
    refreshDeviceStatusLabel();

    // setDeviceConnected(), not updateDevice() -- this fires from live
    // connection state (including DeviceConnection's own ambient retry
    // loop), not a user edit, so it must not land on m_devicesGrid's undo
    // stack (a connection blinking would otherwise show up as an undoable
    // "Edit Device" step, and Ctrl+Z on the Devices tab would undo a status
    // dot instead of an actual edit).
    m_devicesGrid->setDeviceConnected(deviceId, connected);
    if (!connected) {
        // A dropped connection invalidates whatever the last session's
        // HELLO_RESULT reported -- a fresh reconnect re-identifies via
        // deviceIdentified() (see onDeviceAdded()) once its own handshake
        // completes, but nothing should show a stale version/id meanwhile.
        m_devicesGrid->setDeviceIdentity(deviceId, QString(), QString());
    }
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
    WorkspaceManager::instance().reset();
    m_dashboardGrid->fromJson(QJsonObject());
    m_dashboardGrid->undoStack()->clear();
    m_devicesGrid->fromJson(QJsonObject());
    m_devicesGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
    statusBar()->showMessage(tr("Started a new project."), 3000);
}

void MainWindow::onSaveProject() {
    WorkspaceManager::instance().setDashboardFor(WorkspaceManager::instance().activeId(), m_dashboardGrid->toJson());
    ProjectStore::instance().setSection("workspaces", WorkspaceManager::instance().toJson());
    ProjectStore::instance().setSection("devices", m_devicesGrid->toJson());

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
    WorkspaceManager::instance().setDashboardFor(WorkspaceManager::instance().activeId(), m_dashboardGrid->toJson());
    ProjectStore::instance().setSection("workspaces", WorkspaceManager::instance().toJson());
    ProjectStore::instance().setSection("devices", m_devicesGrid->toJson());

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

void MainWindow::onOpenLogFile() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open Log File"), QString(), tr("BTP Log (*.blog)"));
    if (path.isEmpty()) {
        return;
    }
    m_logViewer->openFile(path);
}

void MainWindow::openRecentFile(const QString& path) {
    if (!ProjectStore::instance().load(path)) {
        QMessageBox::warning(this, tr("Open Project"), ProjectStore::instance().lastError());
        return;
    }

    const QJsonObject workspacesSection = ProjectStore::instance().section("workspaces");
    if (workspacesSection.isEmpty()) {
        // Older project file, predating workspaces -- migrate its single
        // "dashboard" section into a lone Default workspace.
        WorkspaceManager::instance().reset();
        WorkspaceManager::instance().setDashboardFor(WorkspaceManager::instance().activeId(),
                                                       ProjectStore::instance().section("dashboard"));
    } else {
        WorkspaceManager::instance().fromJson(workspacesSection);
    }

    // Devices load first: each chart/gauge widget resolves its "Device"
    // config against m_deviceConnections the moment it's created below, and
    // that resolution never gets retried once a device connects later (see
    // MainWindow::refreshWidgetSubscription) -- so a widget created before
    // its device exists is permanently stuck with no subscription.
    // Absent in projects saved before device persistence existed --
    // fromJson(QJsonObject()) on an empty section just clears the list.
    m_devicesGrid->fromJson(ProjectStore::instance().section("devices"));
    m_devicesGrid->undoStack()->clear();
    m_dashboardGrid->fromJson(WorkspaceManager::instance().dashboardFor(WorkspaceManager::instance().activeId()));
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
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
