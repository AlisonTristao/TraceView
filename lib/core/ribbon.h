#pragma once

#include <QString>
#include <QWidget>

class QTabBar;
class QStackedWidget;

namespace traceview {

// A small Word/Excel-style ribbon: a row of tabs, each swapping in its own
// page (typically a strip of icon buttons) below. Deliberately dumb — it
// owns no app logic, MainWindow builds the pages and wires their actions.
class Ribbon : public QWidget {
public:
    explicit Ribbon(QWidget* parent = nullptr);

    // Takes ownership of `page`. Returns the new tab's index.
    int addTab(const QString& label, QWidget* page, bool enabled = true, const QString& toolTip = QString());

private:
    QTabBar* m_tabBar;
    QStackedWidget* m_stack;
};

} // namespace traceview
