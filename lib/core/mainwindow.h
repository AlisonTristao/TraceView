#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QRect>
#include <QString>

class QAction;
class QComboBox;
class QPushButton;
class QToolButton;

namespace traceview {

class DashboardGrid;
class PropertiesPanel;
class Ribbon;
class SerialManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

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
    void onSaveProject();
    void onOpenProject();
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
    int m_configureTabIndex = -1;
    bool m_configureTabActive = false;

    void onLineTerminatorChanged(int index);

    SerialManager* m_serialManager = nullptr;
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
