#pragma once

#include <QString>
#include <QVector>
#include <functional>
#include <memory>

#include "lab/scenes/scenerenderer.h"

namespace tracelab {

// One scene type: the stable id saved in the .tvproj ("scene"), the
// translated name for the editor's picker and a factory.
struct SceneTypeInfo {
    QString typeId;
    QString displayName;
    std::function<std::unique_ptr<SceneRenderer>()> factory;
};

// The scene types the Scene widget can show. LabModule fills instance() at
// install, after the translator is in place (display names are translated
// once, at registration).
class SceneRegistry {
public:
    SceneRegistry() = default;
    SceneRegistry(const SceneRegistry&) = delete;
    SceneRegistry& operator=(const SceneRegistry&) = delete;

    static SceneRegistry& instance();

    // Ignored if `info.typeId` is already registered.
    void registerType(const SceneTypeInfo& info);
    // In registration order.
    QVector<SceneTypeInfo> types() const;
    bool contains(const QString& typeId) const;
    QString displayName(const QString& typeId) const;
    // nullptr for an unknown id.
    std::unique_ptr<SceneRenderer> create(const QString& typeId) const;

private:
    int indexOf(const QString& typeId) const;

    QVector<SceneTypeInfo> m_types;
};

// Registers every scene TraceLab ships: the line follower ("diffdrive").
void registerBuiltinScenes(SceneRegistry& registry);

}  // namespace tracelab
