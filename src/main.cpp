#include <QApplication>

#include "core/mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("TraceView");
    QApplication::setOrganizationName("AlisonTristao");

    traceview::MainWindow window;
    window.show();

    return QApplication::exec();
}
