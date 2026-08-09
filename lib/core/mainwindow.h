#pragma once

#include <QJsonObject>
#include <QMainWindow>
#include <QString>

class QAction;

namespace traceview {

class DashboardGrid;
class PropertiesPanel;
class Ribbon;

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

    DashboardGrid* m_dashboardGrid = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    QAction* m_positionAction = nullptr;
    QAction* m_addWidgetAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    int m_configureTabIndex = -1;
    bool m_configureTabActive = false;
};

} // namespace traceview
