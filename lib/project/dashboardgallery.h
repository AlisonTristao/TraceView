#pragma once

#include <QString>
#include <QStringList>

namespace traceview {

// What a gallery card shows of one dashboard, read straight from its file.
struct GalleryEntryInfo {
    QString name;
    QString path;
    // Lucide id: the gallery's own icon, else the first workspace's, else
    // empty (the card then draws the default dashboard glyph).
    QString icon;
    bool iconIsCustom = false;  // `icon` came from the "gallery" section
    int workspaces = 0;
    int widgets = 0;
};

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

    // Reads the file cheaply (no ProjectStore); an unreadable one gives just
    // the name and path. The ...ForPath forms take any project file path,
    // the built-in example included.
    static GalleryEntryInfo info(const QString& name);
    static GalleryEntryInfo infoForPath(const QString& path);

    // The icon lives in the entry's own file ("gallery": {"icon": ...}), so
    // it travels with an export. An empty id removes it (and the section,
    // once empty). Refuses the built-in example. When the entry is the open
    // project its in-memory copy follows, so the next Save keeps the icon.
    static bool setIcon(const QString& name, const QString& iconId, QString* error = nullptr);
    static bool setIconForPath(const QString& path, const QString& iconId,
                               QString* error = nullptr);

    // Both keep the default pointer in step (renamed along / cleared).
    static bool rename(const QString& oldName, const QString& newName, QString* error = nullptr);
    static bool remove(const QString& name, QString* error = nullptr);
};

}  // namespace traceview
