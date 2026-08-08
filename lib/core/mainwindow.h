#pragma once

#include <QMainWindow>

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

    void onConfigureToggled(bool enabled);
    void onAddWidget();
    void onSaveProject();
    void onOpenProject();
    void onAbout();

    DashboardGrid* m_dashboardGrid = nullptr;
    QAction* m_configureAction = nullptr;
    QAction* m_addWidgetAction = nullptr;
};

} // namespace traceview
