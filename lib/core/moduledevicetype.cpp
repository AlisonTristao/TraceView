#include "core/moduledevicetype.h"

namespace traceview {

ModuleDeviceTypeRegistry& ModuleDeviceTypeRegistry::instance() {
    static ModuleDeviceTypeRegistry registry;
    return registry;
}

bool ModuleDeviceTypeRegistry::registerType(const ModuleDeviceType& type) {
    if (type.id.isEmpty() || !type.createTransport || find(type.id) != nullptr) {
        return false;
    }
    m_types.append(type);
    return true;
}

void ModuleDeviceTypeRegistry::unregisterType(const QString& id) {
    for (int i = 0; i < m_types.size(); ++i) {
        if (m_types.at(i).id == id) {
            m_types.removeAt(i);
            return;
        }
    }
}

const ModuleDeviceType* ModuleDeviceTypeRegistry::find(const QString& id) const {
    for (const ModuleDeviceType& type : m_types) {
        if (type.id == id) {
            return &type;
        }
    }
    return nullptr;
}

}  // namespace traceview
