#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QRect>
#include <QString>

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;

namespace traceview {

class Backend;
class DashboardGrid;
class DashboardWidget;
class PropertiesPanel;
class Ribbon;
class SerialManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Declared (rather than left implicit): the destructor needs to
    // disconnect each dashboard widget's destroyed() handler (see
    // wireChartWidgetToTelemetry) while m_widgetSubscriptions still exists,
    // before Qt's own child-QObject teardown runs.
    ~MainWindow() override;

private:
    void buildMenus();
    Ribbon* buildRibbon();
    void buildPropertiesPanel();
    void updateRibbonIcons();

    void onRibbonTabChanged(int index);
    void refreshSerialPorts();
    void onSerialConnectToggled(bool checked);
    void onSerialConnectionStateChanged(bool connected);
    void onSerialErrorOccurred(const QString& message);
    void onSelectionChanged(const QString& itemId);
    void updateSelectionActions();
    // Pushes the current selection's type/name/key into m_propertiesPanel.
    // Called on selectionChanged and whenever the undo stack moves, since a
    // property edit (or its undo/redo) doesn't otherwise touch selection.
    void refreshPropertiesPanel();
    void onAddWidget();
    void onPanelTypeChangeRequested(const QString& typeId);
    void onPanelNameChangeRequested(const QString& name);
    void onPanelKeyChangeRequested(const QString& key);
    void onPanelConfigChangeRequested(const QJsonObject& config);
    void onNewProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProject();
    void openRecentFile(const QString& path);
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void onClearRecentFiles();
    void onAbout();
    void onFullscreenToggled(bool checked);

    DashboardGrid* m_dashboardGrid = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    Ribbon* m_ribbon = nullptr;
    QAction* m_positionAction = nullptr;
    QAction* m_addWidgetAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    int m_configureTabIndex = -1;
    bool m_configureTabActive = false;

    void onLineTerminatorChanged(int index);

    SerialManager* m_serialManager = nullptr;
    // Everything on the other end of the wire, behind the Backend interface
    // (backend/backend.h) -- decoded telemetry, topic subscriptions, and
    // terminal framing. Concretely a BtpBackend today; MainWindow only ever
    // talks to it through the abstract interface. SerialManager above stays
    // concrete: it just moves raw bytes in and out of the open port.
    Backend* m_backend = nullptr;
    void wireChartWidgetToTelemetry(DashboardWidget* widget);
    // Re-derives every open widget's subscription from its current config --
    // called whenever a config edit (or its undo/redo) could have changed a
    // widget's source/topic/sample time.
    void refreshWidgetSubscriptions();
    // Recomputes the status-bar telemetry summary (requested vs. effective
    // rate, plus bytes/drops from a status_version=2 STATUS).
    void updateTelemetryStatusLabel();
    // One entry per live chart/gauge widget: its SubscriptionManager handle
    // (0 when the widget has no source/topic configured yet). Cleaned up from
    // each widget's own destroyed() signal.
    QHash<DashboardWidget*, quint64> m_widgetSubscriptions;
    QLabel* m_telemetryStatusLabel = nullptr;
    int m_runTabIndex = -1;
    QComboBox* m_portCombo = nullptr;
    QToolButton* m_refreshPortsButton = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QComboBox* m_lineTerminatorCombo = nullptr;
    QPushButton* m_connectButton = nullptr;
    QToolButton* m_fullscreenButton = nullptr;
    bool m_wasMaximized = false;
    QRect m_preFullscreenGeometry;
};

} // namespace traceview
