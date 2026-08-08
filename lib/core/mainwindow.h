#pragma once

#include <QMainWindow>

class QAction;

namespace traceview {

class DashboardGrid;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildMenus();
    void buildToolbar();

    void onConfigureToggled(bool enabled);
    void onAddWidget();
    void onSaveProject();
    void onOpenProject();

    DashboardGrid* m_dashboardGrid = nullptr;
    QAction* m_addWidgetAction = nullptr;
};

} // namespace traceview
