#include "labmodule.h"

#include <QCoreApplication>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

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

    // Placeholder until the run panel exists (plan phase T6).
    auto* content = new QWidget;
    auto* label = new QLabel(
        QCoreApplication::translate("LabModule",
                                    "The simulation engine is not available yet. Add a Scene "
                                    "widget to a dashboard to show a plant."),
        content);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    auto* layout = new QVBoxLayout(content);
    layout->addWidget(label);
    host.addRibbonTab(displayName(), {}, content);
}

}  // namespace tracelab
