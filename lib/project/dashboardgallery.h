#pragma once

#include <QString>
#include <QStringList>

namespace traceview {

// The dashboards TraceView keeps inside the app itself, as opposed to the
// .tvproj files a developer opens/saves wherever they like. Each entry is an
// ordinary .tvproj living in the app's data directory (so exporting one is
// just copying the file), named after the file's base name.
//
// One entry can be marked as the startup dashboard. With none marked -- or
// the marked one missing -- the app starts on the example compiled into the
// binary (kBuiltInExamplePath), which can never be edited or removed, so
// there is always something to open, even on a fresh install.
//
// No UI here; see core/dashboardgallerydialog.h for the Developer-mode
// management window and MainWindow::openStartupDashboard() for startup.
class DashboardGallery {
public:
    static const QString kBuiltInExamplePath;

    // Created on first use.
    static QString directory();

    // Sorted case-insensitively.
    static QStringList names();
    static QString pathFor(const QString& name);
    static bool contains(const QString& name);

    // Empty when path isn't a gallery entry (the built-in example included).
    static QString nameForPath(const QString& path);
    static bool isBuiltInExample(const QString& path) {
        return path == kBuiltInExamplePath;
    }

    // Empty means the built-in example.
    static QString defaultName();
    static void setDefaultName(const QString& name);

    // The gallery's default entry when it still exists, else the built-in
    // example.
    static QString startupPath();

    // A name has to work as a file name on every platform TraceView ships on.
    static bool isValidName(const QString& name, QString* error = nullptr);

    // Both keep the default pointer in step (renamed along / cleared).
    static bool rename(const QString& oldName, const QString& newName, QString* error = nullptr);
    static bool remove(const QString& name, QString* error = nullptr);
};

}  // namespace traceview
