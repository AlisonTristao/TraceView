#pragma once

#include <QMainWindow>
#include <QString>

class QAction;

namespace traceview {

class DashboardGrid;
class Ribbon;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildMenus();
    Ribbon* buildRibbon();
    void updateRibbonIcons();

    // Shows the "pick a widget type" dialog, preselecting `preselectedTypeId`
    // (pass an empty string for none). Returns false if cancelled.
    bool pickWidgetType(const QString& title, const QString& preselectedTypeId, QString* outTypeId);

    void onRibbonTabChanged(int index);
    void onSelectionChanged(const QString& itemId);
    void updateSelectionActions();
    void onAddWidget();
    void onEditSelectedType();
    void onSaveProject();
    void onOpenProject();
    void onAbout();

    DashboardGrid* m_dashboardGrid = nullptr;
    QAction* m_positionAction = nullptr;
    QAction* m_addWidgetAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_editTypeAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    int m_configureTabIndex = -1;
    bool m_configureTabActive = false;
};

} // namespace traceview
