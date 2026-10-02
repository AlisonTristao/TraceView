#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "modules/appmodule.h"

namespace traceview {

// The modules compiled into this executable, and which of them are enabled.
// src/main.cpp fills instance() before the main window is built; tests can
// use a registry of their own.
class ModuleRegistry {
public:
    ModuleRegistry() = default;
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    static ModuleRegistry& instance();

    // Takes ownership. Ignored if another module already uses the same id.
    void registerModule(std::unique_ptr<AppModule> module);

    // In registration order.
    QList<AppModule*> modules() const;
    // nullptr if no module has this id.
    AppModule* module(const QString& id) const;

    // The saved choice from Settings > General > Modules (off by default).
    // Only read at startup, so a change applies after a restart.
    bool isEnabledInSettings(const QString& id) const;
    void setEnabledInSettings(const QString& id, bool enabled);

    // Handles `--mode <id>` / `--mode=<id>` in `arguments` (normally
    // QCoreApplication::arguments()): that module is enabled for this run
    // only, without touching the saved choice, and lends the app its
    // identity (see modeModule()). An unknown id is logged and ignored.
    // Returns the mode module, or nullptr.
    AppModule* applyModeArgument(const QStringList& arguments);
    // The module picked by --mode, or nullptr when the app runs as itself.
    AppModule* modeModule() const;

    // Enabled in Settings, or picked by --mode for this run.
    bool isEnabled(const QString& id) const;

    // Calls install() on every enabled module, in registration order.
    void installEnabled(ModuleHost& host);

private:
    std::vector<std::unique_ptr<AppModule>> m_modules;
    AppModule* m_modeModule = nullptr;
};

}  // namespace traceview
