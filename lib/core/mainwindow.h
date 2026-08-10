#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QRect>
#include <QString>

class QAction;
class QComboBox;
class QMenu;
class QPushButton;
class QToolButton;

namespace traceview {

class BtpSession;
class DashboardGrid;
class ProtocolRouter;
class PropertiesPanel;
class Ribbon;
class SerialManager;
class TelemetryCatalog;
class TelemetryFieldRouter;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Declared (rather than left implicit) because m_telemetryCatalog is a
    // plain (non-QObject) heap object this class owns and frees itself;
    // defined out-of-line in mainwindow.cpp, where TelemetryCatalog's full
    // definition is visible.
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
    // BTP v1 client stack (topico 14): BtpSession decodes the byte stream
    // SerialManager hands it into validated frames, ProtocolRouter
    // dispatches those by MessageType, and TelemetryFieldRouter decodes
    // TELEMETRY samples against m_telemetryCatalog into per-field values.
    // m_telemetryCatalog starts empty -- there is no manifest/discovery
    // exchange yet (that's topico 16), so nothing here assumes which
    // source_id is on the other end of the wire; populating it for a real
    // connection is left to a later topico.
    BtpSession* m_btpSession = nullptr;
    ProtocolRouter* m_protocolRouter = nullptr;
    TelemetryCatalog* m_telemetryCatalog = nullptr;
    TelemetryFieldRouter* m_telemetryFieldRouter = nullptr;
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
