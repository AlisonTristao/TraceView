#include "mainwindow.h"

#include <QLabel>

#include "traceview/version.h"

namespace traceview {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("TraceView v%1").arg(kVersion));
    resize(1024, 640);

    auto* placeholder = new QLabel(
        "TraceView\n\nTelemetry dashboard for line-following robots.\n"
        "Serial/ESP-NOW telemetry ingestion is not implemented yet.",
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

} // namespace traceview
