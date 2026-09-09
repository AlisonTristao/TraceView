#pragma once

#include <QWidget>

class QTimer;

namespace traceview {

// A small indeterminate "busy" spinner meant to float over one corner of a
// host widget while a load runs on a background thread -- the host keeps
// repainting/responding to input the whole time it's up, so a slow load
// never reads as a hang. Purely cosmetic: start()/stop() are the entire
// contract, nothing here tracks real progress of whatever triggered it.
//
// Positions itself in its parent's top-right corner and re-positions on
// every parent resize (installs an event filter on it, the same trick
// MainWindow uses for its own overlay panels -- see
// MainWindow::positionOverlayPanels()).
class BusySpinner : public QWidget {
    Q_OBJECT

public:
    explicit BusySpinner(QWidget* parent);

    // Shows the spinner and starts animating. Safe to call again while
    // already running.
    void start();
    // Hides the spinner and stops the repaint timer, so an idle spinner
    // costs nothing.
    void stop();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reposition();

    QTimer* m_timer;
    int m_angle = 0;
};

}  // namespace traceview
