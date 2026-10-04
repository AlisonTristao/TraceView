#include "labmodule.h"

#include <QCoreApplication>

#include "core/moduledevicetype.h"
#include "lab/benchmarks/linefollower/code/lftemplates.h"
#include "lab/benchmarks/linefollower/device/lfdevices.h"
#include "lab/benchmarks/linefollower/device/lfplantdevice.h"
#include "lab/control/labdevices.h"
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
    // The controller written in C: a device of its own on the Devices tab and
    // in projects, for any plant.
    host.registerDeviceType(labControllerDeviceType());
    // The line-follower benchmark: its plant (and its C++ controller, for the
    // projects that still use it) as devices, the plant as a kind the tab can
    // add, and its controllers, filters and plant in C as templates.
    for (const traceview::ModuleDeviceType& type : lf::lineFollowerDeviceTypes()) {
        host.registerDeviceType(type);
    }
    LabDeviceRegistry::instance().registerPlantType(
        {QString::fromLatin1(lf::kPlantDeviceType),
         QCoreApplication::translate("LabModule", "Line follower"), lf::PlantSettings().toJson()});
    lf::registerLineFollowerTemplates();

    // Each benchmark is a project of its own.
    host.registerExampleProject(QCoreApplication::translate("LabModule", "TraceLab benchmarks"),
                                QCoreApplication::translate("LabModule", "Line follower"),
                                LabPanel::lineFollowerProjectPath());

    // The tab holds the tools to build a lab and drive it.
    auto* panel = new LabPanel(&host);
    host.addRibbonTab(displayName(), panel->ribbonActions(), panel);
}

}  // namespace tracelab
