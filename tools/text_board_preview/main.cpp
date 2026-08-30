// Throwaway visual harness for TextBoardWidget -- feeds it the real
// `sys -health` report and prints the fitted font size at a few cell sizes,
// so the "text gets cut off" report can be reproduced without a robot.

#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QWidget>
#include <cstdio>

#include "dashboard/widgets/textboardwidget.h"

using namespace traceview;

namespace {

const char* kHealthReport =
    "================= Monitor Stats =================\n"
    "uptime      : 00h 15m 00s\n"
    "---------------------- CPU ----------------------\n"
    "tick rate   :    1000 Hz\n"
    "core rate   :     240 MHz\n"
    "core temp   :   41.10 \xC2\xB0""C\n"
    "core PRO    :   91.81 %\n"
    "core APP    :   23.41 %\n"
    "-------------------- memory --------------------\n"
    "logger      :    1.71 %\n"
    "total ram   :  942.05 kb\n"
    "sram        :   91.72 kb\n"
    "psram       :  881.61 kb\n"
    "--------------------- tasks ---------------------\n"
    "task name        prio core      cpu     free heap\n"
    "-\n"
    "ipc1             24   app      0.00%     0.55 kb\n"
    "wifi             23   pro      1.42%     2.68 kb\n"
    "esp_timer        22   pro     11.29%     2.95 kb\n"
    "sys_evt          20   pro      0.00%     1.62 kb\n"
    "tcpip            18   any      0.03%     2.39 kb\n"
    "state_machine    10   app     23.41%     6.57 kb\n"
    "EKF_task         4    pro     39.27%     0.85 kb\n"
    "comms            4    pro     10.90%     4.54 kb\n"
    "routine          3    pro     26.00%     4.40 kb\n"
    "shell_task       2    pro      2.72%     4.27 kb\n"
    "system_monitor   1    pro      0.19%     1.70 kb\n"
    "Tmr Svc          1    any      0.00%     1.35 kb\n"
    "junkebox_task    1    pro      0.00%     3.13 kb\n"
    "mdns             1    pro      0.00%     3.28 kb\n"
    "ipc0             1    pro      0.00%     0.46 kb\n"
    "IDLE1            0    app     76.59%     0.75 kb\n"
    "IDLE0            0    pro      8.19%     0.74 kb\n"
    "=================================================\n";

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    std::printf("report bytes: %zu, lines: %d\n",
                int(std::string(kHealthReport).size()),
                QString::fromUtf8(kHealthReport).count('\n'));

    for (const QSize size : {QSize(560, 640), QSize(400, 320), QSize(240, 160),
                             QSize(900, 400)}) {
        TextBoardWidget probe;
        probe.setText(QString::fromUtf8(kHealthReport));
        probe.resize(size);
        std::printf("cell %dx%d -> fitted font px = %d\n", size.width(),
                    size.height(), probe.fittedFontPixelSize());
    }
    std::fflush(stdout);

    if (argc > 1 && QString(argv[1]) == "--headless") {
        return 0;
    }

    auto* window = new QWidget;
    window->setWindowTitle("TextBoardWidget preview -- sys -health");
    window->resize(600, 700);
    auto* layout = new QGridLayout(window);
    auto* board = new TextBoardWidget;
    board->setText(QString::fromUtf8(kHealthReport));
    layout->addWidget(board);
    window->show();

    return QApplication::exec();
}
