#pragma once

#include <QList>
#include <QString>

class QAction;
class QWidget;

namespace traceview {

class WidgetRegistry;
struct ModuleDeviceType;

// What an enabled module is given by install(): the few places it may plug
// into the main window. Implemented by MainWindow; see docs/MODULES.md.
class ModuleHost {
public:
    // The dashboard's widget types -- register the module's own here so the
    // properties panel's type picker and saved projects can use them.
    virtual WidgetRegistry& widgetRegistry() = 0;

    // Adds one fixed (non-closable) ribbon tab labelled `label`. Its ribbon
    // page holds one button group built from `actions` (none: an empty page),
    // and `content` fills the area below the ribbon while the tab is current.
    // Takes ownership of `content`; `actions` are not owned, so parent them
    // to `content` to keep them alive as long as the tab.
    virtual void addRibbonTab(const QString& label, const QList<QAction*>& actions,
                              QWidget* content) = 0;

    // Registers a kind of in-process device (core/moduledevicetype.h): it
    // appears in the Devices tab's Add Device menu and in projects as a
    // device of TransportType::Module. Install runs before the startup
    // project loads, so saved devices of the type connect. False when the
    // id is taken or the host offers no devices (the default).
    virtual bool registerDeviceType(const ModuleDeviceType& type) {
        (void)type;
        return false;
    }

    // Opens the project at `path` (a file or a Qt resource) in place of the
    // current one and shows its dashboard, as File > Open does. False, with
    // nothing changed, when it cannot be read (the default: no projects).
    virtual bool openProject(const QString& path) {
        (void)path;
        return false;
    }

protected:
    ~ModuleHost() = default;
};

// An optional part of the application, compiled in by the executable that
// registers it (see ModuleRegistry) and switched on in Settings or for one
// run with `--mode <id>`. There is no unload: enabling or disabling takes
// effect on the next start, like the language setting.
class AppModule {
public:
    virtual ~AppModule() = default;

    // Stable and never translated: it names the module in QSettings and on
    // the command line ("--mode <id>").
    virtual QString id() const = 0;
    // Translated (QCoreApplication::translate). Also the window title and
    // wordmark when the app is started with `--mode <id>`.
    virtual QString displayName() const = 0;
    // One translated sentence for the Settings page.
    virtual QString description() const = 0;

    // Called once, while the main window is being built, when the module is
    // enabled.
    virtual void install(ModuleHost& host) = 0;
};

}  // namespace traceview
