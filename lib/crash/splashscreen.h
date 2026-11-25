#pragma once

#include <QString>
#include <QWidget>

namespace traceview {

// Shown at the very start of main() and closed once MainWindow is up, so the
// app never looks gone while it loads -- in particular right after an update
// relaunched it. Qt only, same on every platform. Lives in traceview_crash
// because it reads the SessionMarker's "updated from" note.
class SplashScreen : public QWidget {
    Q_OBJECT
public:
    // `updatedFrom` non-empty adds "Updated from vX to vY".
    explicit SplashScreen(const QString& updatedFrom = QString(), QWidget* parent = nullptr);

    void setMessage(const QString& message);
    // Closes the splash once `window` has been shown.
    void finish(QWidget* window);
};

}  // namespace traceview
