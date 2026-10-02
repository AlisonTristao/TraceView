#include "modules/moduleregistry.h"

#include <QSettings>

#include "core/applog.h"

namespace traceview {

namespace {

constexpr char kModeOption[] = "--mode";

QString enabledKey(const QString& id) {
    return QStringLiteral("modules/%1/enabled").arg(id);
}

}  // namespace

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

void ModuleRegistry::registerModule(std::unique_ptr<AppModule> module) {
    if (!module || this->module(module->id()) != nullptr) {
        return;
    }
    m_modules.push_back(std::move(module));
}

QList<AppModule*> ModuleRegistry::modules() const {
    QList<AppModule*> result;
    for (const std::unique_ptr<AppModule>& module : m_modules) {
        result.append(module.get());
    }
    return result;
}

AppModule* ModuleRegistry::module(const QString& id) const {
    for (const std::unique_ptr<AppModule>& module : m_modules) {
        if (module->id() == id) {
            return module.get();
        }
    }
    return nullptr;
}

bool ModuleRegistry::isEnabledInSettings(const QString& id) const {
    return QSettings().value(enabledKey(id), false).toBool();
}

void ModuleRegistry::setEnabledInSettings(const QString& id, bool enabled) {
    QSettings().setValue(enabledKey(id), enabled);
}

AppModule* ModuleRegistry::applyModeArgument(const QStringList& arguments) {
    const QString option = QString::fromLatin1(kModeOption);
    const QString optionWithValue = option + QLatin1Char('=');
    for (int i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        QString id;
        if (argument == option) {
            if (i + 1 >= arguments.size()) {
                qCWarning(lcApp, "--mode needs a module id; ignored");
                return nullptr;
            }
            id = arguments.at(i + 1);
        } else if (argument.startsWith(optionWithValue)) {
            id = argument.mid(optionWithValue.size());
        } else {
            continue;
        }
        m_modeModule = module(id);
        if (m_modeModule == nullptr) {
            qCWarning(lcApp, "--mode %s: no such module; ignored", qUtf8Printable(id));
        }
        return m_modeModule;
    }
    return nullptr;
}

AppModule* ModuleRegistry::modeModule() const {
    return m_modeModule;
}

bool ModuleRegistry::isEnabled(const QString& id) const {
    return (m_modeModule != nullptr && m_modeModule->id() == id) || isEnabledInSettings(id);
}

void ModuleRegistry::installEnabled(ModuleHost& host) {
    for (const std::unique_ptr<AppModule>& module : m_modules) {
        if (isEnabled(module->id())) {
            module->install(host);
        }
    }
}

}  // namespace traceview
