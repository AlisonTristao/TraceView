#include "sceneregistry.h"

#include <QCoreApplication>

#include "dashboard/scenes/diffdrivescene.h"
#include "dashboard/scenes/furutascene.h"

namespace traceview {

SceneRegistry& SceneRegistry::instance() {
    static SceneRegistry registry;
    return registry;
}

void SceneRegistry::registerType(const SceneTypeInfo& info) {
    if (info.typeId.isEmpty() || !info.factory || indexOf(info.typeId) >= 0) {
        return;
    }
    m_types.append(info);
}

QVector<SceneTypeInfo> SceneRegistry::types() const {
    return m_types;
}

bool SceneRegistry::contains(const QString& typeId) const {
    return indexOf(typeId) >= 0;
}

QString SceneRegistry::displayName(const QString& typeId) const {
    const int index = indexOf(typeId);
    return index >= 0 ? m_types[index].displayName : QString();
}

std::unique_ptr<SceneRenderer> SceneRegistry::create(const QString& typeId) const {
    const int index = indexOf(typeId);
    return index >= 0 ? m_types[index].factory() : nullptr;
}

int SceneRegistry::indexOf(const QString& typeId) const {
    for (int i = 0; i < m_types.size(); ++i) {
        if (m_types[i].typeId == typeId) {
            return i;
        }
    }
    return -1;
}

void registerBuiltinScenes(SceneRegistry& registry) {
    registry.registerType({QString::fromLatin1(DiffDriveScene::kTypeId),
                           QCoreApplication::translate("DiffDriveScene", "Line follower"),
                           [] { return std::make_unique<DiffDriveScene>(); }});
    registry.registerType({QString::fromLatin1(FurutaScene::kTypeId),
                           QCoreApplication::translate("FurutaScene", "Furuta pendulum"),
                           [] { return std::make_unique<FurutaScene>(); }});
}

}  // namespace traceview
