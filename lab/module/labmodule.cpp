#include "labmodule.h"

#include <QCoreApplication>

#include "core/moduledevicetype.h"
#include "lab/benchmarks/linefollower/device/lfdevices.h"
#include "lab/module/labpanel.h"
#include "lab/scenes/sceneregistry.h"
#include "lab/scenes/scenewidget.h"

namespace tracelab {

QString LabModule::id() const {
    return QString::fromLatin1(kId);
}

QString LabModule::displayName() const {
    return QCoreApplication::translate("LabModule", "TraceLab");
}

QString LabModule::description() const {
    return QCoreApplication::translate(
        "LabModule", "Simulates a plant, closes the loop with your controller and animates it.");
}

void LabModule::install(traceview::ModuleHost& host) {
    registerBuiltinScenes(SceneRegistry::instance());
    registerSceneWidget(host.widgetRegistry());
    // The line-follower benchmark's plant and controller host, as devices of
    // their own on the Devices tab and in projects.
    for (const traceview::ModuleDeviceType& type : lf::lineFollowerDeviceTypes()) {
        host.registerDeviceType(type);
    }

    auto* panel = new LabPanel([&host](const QString& path) { return host.openProject(path); });
    host.addRibbonTab(displayName(), panel->ribbonActions(), panel);
}

}  // namespace tracelab
