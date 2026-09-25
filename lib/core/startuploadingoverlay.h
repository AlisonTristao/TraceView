#pragma once

#include <QByteArray>
#include <QWidget>

class QSvgRenderer;

namespace traceview {

// Full-window cover shown from the first frame until the startup dashboard
// is built: theme background, the wordmark and a "Loading dashboard..."
// line. Building a dashboard blocks the event loop, so without it the
// window sat blank until the widgets appeared. Static on purpose -- nothing
// could animate while the load runs anyway.
//
// firstPainted() fires once, after the cover has been painted, so the load
// can be deferred until the cover is actually on screen.
class StartupLoadingOverlay : public QWidget {
    Q_OBJECT

public:
    explicit StartupLoadingOverlay(QWidget* parent);

signals:
    void firstPainted();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyTheme();

    QByteArray m_svgTemplate;
    QSvgRenderer* m_renderer;
    bool m_painted = false;
};

}  // namespace traceview
