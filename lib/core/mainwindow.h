#pragma once

#include <QMainWindow>

namespace traceview {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace traceview
