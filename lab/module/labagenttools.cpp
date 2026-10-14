#include "lab/module/labagenttools.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <memory>

#include "agent/toolregistry.h"
#include "lab/code/codeblockowner.h"
#include "lab/code/codefiles.h"
#include "lab/control/labdevices.h"
#include "lab/module/labpanel.h"
#include "lab/plant/labplantdevice.h"
#include "lab/scenes/sceneregistry.h"
#include "lab/scenes/scenewidget.h"
#include "modules/appmodule.h"

namespace tracelab {

using code::BlockKind;
using code::CodeLibrary;
using code::DeclKind;
using traceview::AgentSchema;
using traceview::AgentTool;
using traceview::AgentToolReply;
using traceview::AgentToolResult;

namespace {

constexpr char kHeaderResource[] = ":/tracelab/code/runtime/tracelab.h";
// lab_run never waits longer than this, however much simulated time was
// asked for.
constexpr int kMaxRunWallMs = 120000;

QString kindName(BlockKind kind) {
    switch (kind) {
        case BlockKind::Plant:
            return QStringLiteral("plant");
        case BlockKind::Filter:
            return QStringLiteral("filter");
        default:
            return QStringLiteral("controller");
    }
}

bool kindFromName(const QString& name, BlockKind* kind) {
    if (name == QLatin1String("plant")) {
        *kind = BlockKind::Plant;
    } else if (name == QLatin1String("filter")) {
        *kind = BlockKind::Filter;
    } else if (name == QLatin1String("controller")) {
        *kind = BlockKind::Controller;
    } else {
        return false;
    }
    return true;
}

QString declKindName(DeclKind kind) {
    switch (kind) {
        case DeclKind::Param:
            return QStringLiteral("param");
        case DeclKind::ParamInt:
            return QStringLiteral("paramInt");
        case DeclKind::ParamEnum:
            return QStringLiteral("paramEnum");
        case DeclKind::ParamBool:
            return QStringLiteral("paramBool");
        case DeclKind::Input:
            return QStringLiteral("input");
        case DeclKind::State:
            return QStringLiteral("state");
        case DeclKind::Output:
            return QStringLiteral("output");
        case DeclKind::Discrete:
            return QStringLiteral("discrete");
        case DeclKind::In:
            return QStringLiteral("in");
        case DeclKind::Out:
            return QStringLiteral("out");
        case DeclKind::InOut:
            return QStringLiteral("inout");
        case DeclKind::Preview:
            return QStringLiteral("preview");
        case DeclKind::Diag:
            return QStringLiteral("diag");
    }
    return QStringLiteral("other");
}

const QStringList kRunModes = {"realtime", "fast", "lockstep"};

// The code blocks of a lab device: a controller device's filters and
// controller, a plant written in C's plant. Null for anything else.
CodeBlockOwner* ownerOf(const QString& deviceId) {
    LabDeviceRegistry& registry = LabDeviceRegistry::instance();
    if (LabControllerDevice* controller = registry.controller(deviceId)) {
        return controller;
    }
    if (LabPlantDevice* plant = registry.plant(deviceId)) {
        return plant->codeBlocks();
    }
    return nullptr;
}

QVector<int> blocksOf(CodeBlockOwner* owner) {
    if (auto* controller = qobject_cast<LabControllerDevice*>(owner)) {
        QVector<int> blocks;
        for (int i = 0; i < controller->filterCount(); ++i) {
            blocks.append(i);
        }
        blocks.append(LabControllerDevice::kController);
        return blocks;
    }
    return {0};
}

bool validBlock(CodeBlockOwner* owner, int block) {
    return blocksOf(owner).contains(block);
}

QJsonArray messagesJson(const QVector<code::CodeMessage>& messages) {
    QJsonArray list;
    for (const code::CodeMessage& message : messages) {
        list.append(QJsonObject{{"line", message.line},
                                {"text", message.text},
                                {"severity", message.warning ? "warning" : "error"}});
    }
    return list;
}

QJsonArray paramsJson(const code::CodeModule* module) {
    QJsonArray list;
    if (!module) {
        return list;
    }
    for (const code::Declaration* decl : module->params()) {
        QJsonObject param{{"name", decl->name},
                          {"kind", declKindName(decl->kind)},
                          {"value", module->paramValue(decl->name)},
                          {"default", decl->defaultValue}};
        if (!decl->unit.isEmpty()) {
            param["unit"] = decl->unit;
        }
        if (decl->minimum < decl->maximum) {
            param["min"] = decl->minimum;
            param["max"] = decl->maximum;
        }
        if (!decl->options.isEmpty()) {
            param["options"] = QJsonArray::fromStringList(decl->options);
        }
        list.append(param);
    }
    return list;
}

QJsonObject blockSummary(CodeBlockOwner* owner, int block) {
    const CodeBlockSpec spec = owner->spec(block);
    const CodeBlockOwner::Status status = owner->status(block);
    static const QStringList kTones = {"none", "good", "waiting", "wrong"};
    QJsonObject json{
        {"block", block},
        {"role", owner->blockRole(block)},
        {"name", owner->blockName(block)},
        {"kind", kindName(owner->blockKind(block))},
        {"compiled", owner->compiled(block)},
        {"status", kTones.value(status.tone, "none") + QStringLiteral(": ") + status.text},
        {"params", paramsJson(owner->module(block))},
    };
    if (!spec.templateId.isEmpty()) {
        json["template"] = spec.templateId;
    }
    if (!spec.path.isEmpty()) {
        json["path"] = spec.path;
    }
    if (!owner->runtimeError(block).isEmpty()) {
        json["runtimeError"] = owner->runtimeError(block);
    }
    if (!owner->note(block).isEmpty()) {
        json["note"] = owner->note(block);
    }
    return json;
}

// Compile outcome after a source change, for the model to fix its code.
// "compiled" says whether some version runs; "accepted" whether the latest
// source compiled (an error leaves the previous version running).
QJsonObject compileResult(CodeBlockOwner* owner, int block) {
    QJsonObject json = blockSummary(owner, block);
    const QVector<code::CodeMessage> messages = owner->messages(block);
    bool accepted = true;
    for (const code::CodeMessage& message : messages) {
        accepted = accepted && message.warning;
    }
    json["accepted"] = accepted;
    json["messages"] = messagesJson(messages);
    if (!accepted) {
        json["hint"] = owner->compiled(block)
                           ? QStringLiteral(
                                 "This source did not compile; the previous version "
                                 "keeps running. Fix the lines in messages and write "
                                 "the block again.")
                           : QStringLiteral(
                                 "This source did not compile and nothing runs. Fix "
                                 "the lines in messages and write the block again.");
    }
    return json;
}

QJsonObject settingJson(const PlantSetting& setting) {
    static const QStringList kTypes = {"int", "double", "enum", "bool"};
    QJsonObject json{{"key", setting.key},
                     {"label", setting.label},
                     {"type", kTypes.value(int(setting.type))},
                     {"value", QJsonValue::fromVariant(setting.value)}};
    if (!setting.unit.isEmpty()) {
        json["unit"] = setting.unit;
    }
    if (setting.type == PlantSetting::Type::Int || setting.type == PlantSetting::Type::Double) {
        json["min"] = setting.minimum;
        json["max"] = setting.maximum;
    }
    if (setting.type == PlantSetting::Type::Enum) {
        json["options"] = QJsonArray::fromStringList(setting.options);
    }
    return json;
}

QJsonObject plantJson(const QString& id, LabPlantDevice* plant) {
    QJsonObject status;
    for (const auto& [caption, text] : plant->statusItems()) {
        status[caption] = text;
    }
    QJsonArray settings;
    for (const PlantSetting& setting : plant->settingsSchema()) {
        settings.append(settingJson(setting));
    }
    QJsonObject json{
        {"deviceId", id},
        {"type", plant->typeName()},
        {"benchmark", LabPanel::benchmarkOf(plant)},
        {"running", plant->running()},
        {"tick", double(plant->tick())},
        {"time", plant->time()},
        {"sampleRate", plant->sampleRate()},
        {"runMode", kRunModes.value(int(plant->runMode()))},
        {"timeScale", plant->timeScale()},
        {"status", status},
        {"settings", settings},
        {"controllers",
         QJsonArray::fromStringList(LabDeviceRegistry::instance().controllersOf(id))},
    };
    if (!plant->worldName().isEmpty()) {
        json["world"] = plant->worldName();
    }
    if (CodeBlockOwner* owner = plant->codeBlocks()) {
        json["blocks"] = QJsonArray{blockSummary(owner, 0)};
    }
    return json;
}

QJsonObject controllerJson(const QString& id, LabControllerDevice* controller) {
    QJsonArray blocks;
    for (int block : blocksOf(controller)) {
        blocks.append(blockSummary(controller, block));
    }
    return QJsonObject{
        {"deviceId", id}, {"plantDeviceId", controller->plantDeviceId()}, {"blocks", blocks}};
}

bool confirm(const QString& title, const QString& text) {
    return QMessageBox::question(QApplication::activeWindow(), title, text,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

QString blockHelp() {
    return QStringLiteral(
        "deviceId is a lab plant or controller device (lab_get_state); block is 0 for a "
        "plant, -1 for a controller device's controller and 0..n-1 for its filters.");
}

}  // namespace

void registerLabAgentTools(traceview::ModuleHost* host, traceview::ToolRegistry* registry) {
    AgentTool state;
    state.name = "lab_get_state";
    state.title = "TraceLab state";
    state.readOnly = true;
    state.description =
        "TraceLab's simulation loop: each plant (its kind, clock -- running, tick, simulated "
        "time, sample rate, run mode -- status figures, settings, code block), each controller "
        "device (its filters and controller as code blocks, with compile state, live "
        "parameters and status), the plant kinds lab_add_plant can add and the scene types. "
        "Plants and controllers are ordinary devices too: list_signals, read_signal and "
        "add_widget work on their telemetry (loop.state, ctl.diag, sim.*).";
    state.handler = [](const QJsonObject&, const AgentToolReply& reply) {
        LabDeviceRegistry& registry = LabDeviceRegistry::instance();
        QJsonArray plants;
        for (const QString& id : registry.plantIds()) {
            if (LabPlantDevice* plant = registry.plant(id)) {
                plants.append(plantJson(id, plant));
            }
        }
        QJsonArray controllers;
        for (const QString& id : registry.controllerIds()) {
            if (LabControllerDevice* controller = registry.controller(id)) {
                controllers.append(controllerJson(id, controller));
            }
        }
        QJsonArray kinds;
        for (const LabDeviceRegistry::PlantType& type : registry.plantTypes()) {
            kinds.append(type.name);
        }
        QJsonArray scenes;
        for (const SceneTypeInfo& info : SceneRegistry::instance().types()) {
            scenes.append(QJsonObject{{"type", info.typeId}, {"name", info.displayName}});
        }
        reply(AgentToolResult::json(QJsonObject{{"plants", plants},
                                                {"controllers", controllers},
                                                {"plantKinds", kinds},
                                                {"sceneTypes", scenes}}));
    };
    registry->registerTool(state);

    AgentTool api;
    api.name = "lab_code_reference";
    api.title = "TraceLab C API";
    api.readOnly = true;
    api.description =
        "The full tracelab.h every code block includes: the TL_* declaration macros (inputs, "
        "states, outputs, params, diagnostics, preview), the functions each block kind "
        "defines (plant_deriv/plant_update/plant_output/plant_reset, filt_step/filt_reset, "
        "ctl_step/ctl_reset) and the library (math, tl_ts(), tl_noise(), tl_mat_*, tl_qp_box). "
        "Read it before writing a block. Signals bind by name: a controller's TL_IN(x) reads "
        "the plant's TL_OUTPUT(x) (or a filter's TL_INOUT(x)), its TL_OUT(u) drives the "
        "plant's TL_INPUT(u).";
    api.handler = [](const QJsonObject&, const AgentToolReply& reply) {
        QFile file(QString::fromLatin1(kHeaderResource));
        if (!file.open(QIODevice::ReadOnly)) {
            reply(AgentToolResult::error(QStringLiteral("tracelab.h is not available.")));
            return;
        }
        reply(AgentToolResult::text(QString::fromUtf8(file.readAll())));
    };
    registry->registerTool(api);

    AgentTool templates;
    templates.name = "lab_list_templates";
    templates.title = "List TraceLab templates";
    templates.readOnly = true;
    templates.description =
        "Code block templates the benchmarks ship (read-only starting points, by id such as "
        "\"furuta/qlpv_mpc\") and the user's own block files, per kind (plant, filter, "
        "controller).";
    templates.inputSchema =
        AgentSchema()
            .string("kind", "Only this kind.", false, {"plant", "filter", "controller"})
            .toJson();
    templates.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        QJsonObject result;
        for (BlockKind kind : {BlockKind::Plant, BlockKind::Filter, BlockKind::Controller}) {
            if (args.contains("kind") && args.value("kind").toString() != kindName(kind)) {
                continue;
            }
            QJsonArray list;
            for (const code::CodeTemplate& t : CodeLibrary::instance().templates(kind)) {
                list.append(
                    QJsonObject{{"id", t.id}, {"name", t.name}, {"description", t.description}});
            }
            result[kindName(kind)] =
                QJsonObject{{"templates", list},
                            {"userFiles", QJsonArray::fromStringList(CodeLibrary::userFiles(kind))},
                            {"userFolder", CodeLibrary::userDirectory(kind)}};
        }
        reply(AgentToolResult::json(result));
    };
    registry->registerTool(templates);

    AgentTool addPlant;
    addPlant.name = "lab_add_plant";
    addPlant.title = "Add plant";
    addPlant.description =
        "Adds a simulated plant of one of the kinds lab_get_state lists (\"Line follower\", "
        "\"Furuta pendulum\"...) to the project, with its default code and settings. Returns "
        "its device id.";
    addPlant.inputSchema = AgentSchema().string("kind", "Plant kind (plantKinds).", true).toJson();
    addPlant.handler = [host](const QJsonObject& args, const AgentToolReply& reply) {
        LabDeviceRegistry& registry = LabDeviceRegistry::instance();
        for (const LabDeviceRegistry::PlantType& type : registry.plantTypes()) {
            if (type.name != args.value("kind").toString()) {
                continue;
            }
            const QString id = host->addModuleDevice(
                type.typeId,
                QCoreApplication::translate("tracelab::LabPanel", "TraceLab: %1").arg(type.name),
                type.defaultConfig);
            if (id.isEmpty()) {
                reply(AgentToolResult::error(QStringLiteral("The plant could not be added.")));
                return;
            }
            reply(AgentToolResult::json(QJsonObject{{"deviceId", id}}));
            return;
        }
        reply(AgentToolResult::error(
            QStringLiteral("No plant kind \"%1\".").arg(args.value("kind").toString())));
    };
    registry->registerTool(addPlant);

    AgentTool addController;
    addController.name = "lab_add_controller";
    addController.title = "Add controller";
    addController.description =
        "Adds a controller device closing the loop of a plant, its controller block from a "
        "template (lab_list_templates) or a C file of the user's. A plant has one controller "
        "device: to change its code use lab_write_block or lab_set_block_source. Returns its "
        "device id.";
    addController.inputSchema = AgentSchema()
                                    .string("plantDeviceId", "The plant (lab_get_state).", true)
                                    .string("template", "Controller template id.")
                                    .string("path", "Or a controller .c file.")
                                    .toJson();
    addController.handler = [host](const QJsonObject& args, const AgentToolReply& reply) {
        const QString plantId = args.value("plantDeviceId").toString();
        LabDeviceRegistry& registry = LabDeviceRegistry::instance();
        if (!registry.plant(plantId)) {
            reply(AgentToolResult::error(QStringLiteral("No plant \"%1\".").arg(plantId)));
            return;
        }
        for (const QString& id : registry.controllerIds()) {
            LabControllerDevice* existing = registry.controller(id);
            if (existing && existing->plantDeviceId() == plantId) {
                reply(AgentToolResult::error(
                    QStringLiteral("The plant already has controller device %1.").arg(id)));
                return;
            }
        }
        CodeBlockSpec spec;
        spec.templateId = args.value("template").toString();
        spec.path = args.value("path").toString();
        if (spec.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Give a template or a path.")));
            return;
        }
        const QString id = host->addModuleDevice(
            QString::fromLatin1(kLabControllerDeviceType),
            QCoreApplication::translate("tracelab::LabPanel", "TraceLab: Controller"),
            QJsonObject{{"plantDeviceId", plantId}, {"controller", spec.toJson()}});
        if (id.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("The controller could not be added.")));
            return;
        }
        QJsonObject result{{"deviceId", id}};
        if (LabControllerDevice* controller = registry.controller(id)) {
            result["controller"] = compileResult(controller, LabControllerDevice::kController);
        }
        reply(AgentToolResult::json(result));
    };
    registry->registerTool(addController);

    AgentTool addFilter;
    addFilter.name = "lab_add_filter";
    addFilter.title = "Add filter";
    addFilter.description =
        "Adds a filter block (from a template or a .c file) to a controller device, after its "
        "other filters: it runs between the plant's outputs and the controller. Returns its "
        "block number and compile result.";
    addFilter.inputSchema = AgentSchema()
                                .string("controllerDeviceId", "Controller device.", true)
                                .string("template", "Filter template id.")
                                .string("path", "Or a filter .c file.")
                                .toJson();
    addFilter.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        LabControllerDevice* controller =
            LabDeviceRegistry::instance().controller(args.value("controllerDeviceId").toString());
        if (!controller) {
            reply(AgentToolResult::error(QStringLiteral("No such controller device.")));
            return;
        }
        CodeBlockSpec spec;
        spec.templateId = args.value("template").toString();
        spec.path = args.value("path").toString();
        if (spec.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Give a template or a path.")));
            return;
        }
        controller->addFilter(spec);
        reply(AgentToolResult::json(compileResult(controller, controller->filterCount() - 1)));
    };
    registry->registerTool(addFilter);

    AgentTool removeFilter;
    removeFilter.name = "lab_remove_filter";
    removeFilter.title = "Remove filter";
    removeFilter.description =
        "Takes a filter out of a controller device. Its file stays; the reply has its spec "
        "(template/path and parameters) to put it back with lab_add_filter.";
    removeFilter.inputSchema = AgentSchema()
                                   .string("controllerDeviceId", "Controller device.", true)
                                   .integer("block", "Filter number (0..n-1).", true)
                                   .toJson();
    removeFilter.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        LabControllerDevice* controller =
            LabDeviceRegistry::instance().controller(args.value("controllerDeviceId").toString());
        const int block = args.value("block").toInt();
        if (!controller || block < 0 || block >= controller->filterCount()) {
            reply(AgentToolResult::error(QStringLiteral("No such filter.")));
            return;
        }
        const QJsonObject spec = controller->spec(block).toJson();
        controller->removeFilter(block);
        reply(AgentToolResult::json(QJsonObject{{"removed", spec}}));
    };
    registry->registerTool(removeFilter);

    AgentTool getBlock;
    getBlock.name = "lab_get_block";
    getBlock.title = "Get code block";
    getBlock.readOnly = true;
    getBlock.description =
        "One code block in full: its C source as compiled, where it comes from (template or "
        "file), the compiler's messages, a runtime fault if any, what it printed since the "
        "last read, its declarations, live parameters, and the signals it can read and write "
        "with live values. " +
        blockHelp();
    getBlock.inputSchema = AgentSchema()
                               .string("deviceId", "Lab plant or controller device.", true)
                               .integer("block", "Block number.", true)
                               .toJson();
    getBlock.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        CodeBlockOwner* owner = ownerOf(args.value("deviceId").toString());
        const int block = args.value("block").toInt();
        if (!owner || !validBlock(owner, block)) {
            reply(AgentToolResult::error(QStringLiteral("No such code block. ") + blockHelp()));
            return;
        }
        QJsonObject json = compileResult(owner, block);
        json["source"] = owner->source(block);
        json["log"] = QJsonArray::fromStringList(owner->takeLog(block));
        QJsonArray declarations;
        if (const code::CodeModule* module = owner->module(block)) {
            for (const code::Declaration& decl : module->declarations()) {
                QJsonObject entry{{"kind", declKindName(decl.kind)}, {"name", decl.name}};
                if (decl.count > 1) {
                    entry["count"] = decl.count;
                }
                if (!decl.unit.isEmpty()) {
                    entry["unit"] = decl.unit;
                }
                declarations.append(entry);
            }
        }
        json["declarations"] = declarations;
        QJsonArray signalRows;
        for (const SignalRow& row : owner->signalRows(block)) {
            signalRows.append(row.header ? QStringLiteral("## ") + row.text : row.text);
        }
        json["signals"] = signalRows;
        reply(AgentToolResult::json(json));
    };
    registry->registerTool(getBlock);

    AgentTool writeBlock;
    writeBlock.name = "lab_write_block";
    writeBlock.title = "Write code block";
    writeBlock.description =
        "Replaces a code block's C source and compiles it at once, returning the compiler's "
        "messages: fix and write again until it compiles (a version that doesn't compile "
        "leaves the previous one running). A block on a template is first saved as a file of "
        "the user's (Documents/TraceLab, named fileName or after the block); a block on a "
        "file has that file overwritten. Parameters set by hand are kept. Read "
        "lab_code_reference for the API and lab_get_block for the current source. " +
        blockHelp();
    writeBlock.inputSchema = AgentSchema()
                                 .string("deviceId", "Lab plant or controller device.", true)
                                 .integer("block", "Block number.", true)
                                 .string("source", "The whole C file.", true)
                                 .string("fileName", "Base name for a new file (no extension).")
                                 .toJson();
    writeBlock.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        CodeBlockOwner* owner = ownerOf(args.value("deviceId").toString());
        const int block = args.value("block").toInt();
        if (!owner || !validBlock(owner, block)) {
            reply(AgentToolResult::error(QStringLiteral("No such code block. ") + blockHelp()));
            return;
        }
        CodeBlockSpec spec = owner->spec(block);
        const QString source = args.value("source").toString();
        QString error;
        if (spec.path.isEmpty() || args.contains("fileName")) {
            const QString base = args.value("fileName").toString(owner->blockName(block));
            const QString path = CodeLibrary::instance().createFile(owner->blockKind(block),
                                                                    spec.templateId, base, &error);
            if (path.isEmpty()) {
                reply(AgentToolResult::error(error));
                return;
            }
            // Keeps the template marker createFile() wrote, so "Back to the
            // original" still works.
            const QString origin = CodeLibrary::originOf(CodeLibrary::readFile(path));
            const QString text =
                origin.isEmpty() ? source : CodeLibrary::withOrigin(source, origin);
            if (!CodeLibrary::writeFile(path, text, &error)) {
                reply(AgentToolResult::error(error));
                return;
            }
            spec.path = path;
            spec.templateId.clear();
            owner->setSpec(block, spec);
        } else {
            // A copy of a template keeps saying which one, whatever the new
            // source starts with.
            const QString origin = CodeLibrary::originOf(CodeLibrary::readFile(spec.path));
            const QString text = origin.isEmpty() || !CodeLibrary::originOf(source).isEmpty()
                                     ? source
                                     : CodeLibrary::withOrigin(source, origin);
            if (!CodeLibrary::writeFile(spec.path, text, &error)) {
                reply(AgentToolResult::error(error));
                return;
            }
            owner->reload(block);
        }
        QJsonObject result = compileResult(owner, block);
        result["path"] = owner->spec(block).path;
        reply(AgentToolResult::json(result));
    };
    registry->registerTool(writeBlock);

    AgentTool setSource;
    setSource.name = "lab_set_block_source";
    setSource.title = "Set block source";
    setSource.description =
        "Points a code block at a template (lab_list_templates) or an existing .c file "
        "instead, and compiles it. Parameters set by hand are dropped. " +
        blockHelp();
    setSource.inputSchema = AgentSchema()
                                .string("deviceId", "Lab plant or controller device.", true)
                                .integer("block", "Block number.", true)
                                .string("template", "Template id.")
                                .string("path", "Or a .c file.")
                                .toJson();
    setSource.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        CodeBlockOwner* owner = ownerOf(args.value("deviceId").toString());
        const int block = args.value("block").toInt();
        if (!owner || !validBlock(owner, block)) {
            reply(AgentToolResult::error(QStringLiteral("No such code block. ") + blockHelp()));
            return;
        }
        CodeBlockSpec spec;
        spec.templateId = args.value("template").toString();
        spec.path = args.value("path").toString();
        if (spec.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Give a template or a path.")));
            return;
        }
        if (!spec.path.isEmpty() && !QFileInfo(spec.path).isFile()) {
            reply(AgentToolResult::error(QStringLiteral("No file at %1.").arg(spec.path)));
            return;
        }
        owner->setSpec(block, spec);
        reply(AgentToolResult::json(compileResult(owner, block)));
    };
    registry->registerTool(setSource);

    AgentTool params;
    params.name = "lab_set_params";
    params.title = "Set block parameters";
    params.description =
        "Changes a code block's TL_PARAM values live (the running simulation sees them on its "
        "next tick); they are kept in the project. values maps parameter names (lab_get_block) "
        "to numbers; an enum takes its option's index, a bool 0 or 1. " +
        blockHelp();
    params.inputSchema = AgentSchema()
                             .string("deviceId", "Lab plant or controller device.", true)
                             .integer("block", "Block number.", true)
                             .object("values", "Parameter name -> value.", true)
                             .toJson();
    params.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        CodeBlockOwner* owner = ownerOf(args.value("deviceId").toString());
        const int block = args.value("block").toInt();
        if (!owner || !validBlock(owner, block) || !owner->module(block)) {
            reply(AgentToolResult::error(QStringLiteral("No compiled code block there. ") +
                                         blockHelp()));
            return;
        }
        const QJsonObject values = args.value("values").toObject();
        const code::CodeModule* module = owner->module(block);
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            bool known = false;
            for (const code::Declaration* decl : module->params()) {
                known = known || decl->name == it.key();
            }
            if (!known || !it.value().isDouble()) {
                reply(AgentToolResult::error(
                    QStringLiteral("\"%1\" is not a numeric parameter of this block.")
                        .arg(it.key())));
                return;
            }
        }
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            owner->setParam(block, it.key(), it.value().toDouble());
        }
        reply(AgentToolResult::json(QJsonObject{{"params", paramsJson(owner->module(block))}}));
    };
    registry->registerTool(params);

    AgentTool simulation;
    simulation.name = "lab_simulation";
    simulation.title = "Simulation control";
    simulation.description =
        "Drives a plant's simulation: play, pause, reset (back to tick 0 with the current "
        "settings, keeping play/pause) or step (steps ticks while paused, default 1).";
    simulation.inputSchema =
        AgentSchema()
            .string("plantDeviceId", "The plant (lab_get_state).", true)
            .string("action", "What to do.", true, {"play", "pause", "reset", "step"})
            .integer("steps", "Ticks for step (max 10000).")
            .toJson();
    simulation.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("plantDeviceId").toString();
        LabPlantDevice* plant = LabDeviceRegistry::instance().plant(id);
        if (!plant) {
            reply(AgentToolResult::error(QStringLiteral("No plant \"%1\".").arg(id)));
            return;
        }
        const QString action = args.value("action").toString();
        if (action == QLatin1String("play")) {
            plant->run();
        } else if (action == QLatin1String("pause")) {
            plant->pause();
        } else if (action == QLatin1String("reset")) {
            plant->reset();
        } else {
            if (plant->running()) {
                plant->pause();
            }
            const int steps = qBound(1, args.value("steps").toInt(1), 10000);
            for (int i = 0; i < steps; ++i) {
                plant->step();
            }
        }
        reply(AgentToolResult::json(plantJson(id, plant)));
    };
    registry->registerTool(simulation);

    AgentTool run;
    run.name = "lab_run";
    run.title = "Run simulation";
    run.description =
        "Runs a plant's simulation for a span of simulated time and pauses it, then returns "
        "the plant's state and every block's status (runtime faults included). With reset "
        "true it starts from tick 0. Clock in realtime mode takes that long for real; fast "
        "and lockstep go as fast as the controller allows (at most 2 minutes of waiting). "
        "Then read the results with read_signal on charts bound to its topics, or "
        "lab_get_block's signals.";
    run.inputSchema = AgentSchema()
                          .string("plantDeviceId", "The plant (lab_get_state).", true)
                          .number("seconds", "Simulated seconds to run.", true)
                          .boolean("reset", "Start from tick 0 first.")
                          .toJson();
    run.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("plantDeviceId").toString();
        LabPlantDevice* plant = LabDeviceRegistry::instance().plant(id);
        if (!plant) {
            reply(AgentToolResult::error(QStringLiteral("No plant \"%1\".").arg(id)));
            return;
        }
        if (args.value("reset").toBool()) {
            plant->reset();
        }
        const double target = plant->time() + qMax(0.0, args.value("seconds").toDouble());
        auto* waiter = new QObject(plant);
        auto done = std::make_shared<bool>(false);
        QPointer<LabPlantDevice> guard(plant);
        const auto finish = [waiter, guard, id, done, reply](bool timedOut) {
            if (*done) {
                return;
            }
            *done = true;
            waiter->deleteLater();
            if (!guard) {
                reply(AgentToolResult::error(QStringLiteral("The plant went away.")));
                return;
            }
            guard->pause();
            QJsonObject result{{"plant", plantJson(id, guard)}};
            QJsonArray controllers;
            for (const QString& controllerId : LabDeviceRegistry::instance().controllersOf(id)) {
                if (LabControllerDevice* c =
                        LabDeviceRegistry::instance().controller(controllerId)) {
                    controllers.append(controllerJson(controllerId, c));
                }
            }
            result["controllers"] = controllers;
            if (timedOut) {
                result["note"] = QStringLiteral(
                    "Stopped by the wall-time limit before the "
                    "requested simulated time.");
            }
            reply(AgentToolResult::json(result));
        };
        QObject::connect(plant, &LabPlantDevice::ticked, waiter, [guard, target, finish] {
            if (guard && guard->time() >= target) {
                finish(false);
            }
        });
        QTimer::singleShot(kMaxRunWallMs, waiter, [finish] { finish(true); });
        if (plant->time() >= target) {
            finish(false);
            return;
        }
        plant->run();
    };
    registry->registerTool(run);

    AgentTool clock;
    clock.name = "lab_set_clock";
    clock.title = "Set simulation clock";
    clock.description =
        "A plant's clock: runMode realtime (ticks at the sample time divided by timeScale), "
        "fast (as fast as possible) or lockstep (as fast as possible, each tick waiting for "
        "the controller's answer); sampleRate in Hz (changing it restarts the run).";
    clock.inputSchema = AgentSchema()
                            .string("plantDeviceId", "The plant (lab_get_state).", true)
                            .string("runMode", "Clock mode.", false, kRunModes)
                            .number("timeScale", "Realtime speed factor (1 = real time).")
                            .integer("sampleRate", "Ticks per second.")
                            .toJson();
    clock.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("plantDeviceId").toString();
        LabPlantDevice* plant = LabDeviceRegistry::instance().plant(id);
        if (!plant) {
            reply(AgentToolResult::error(QStringLiteral("No plant \"%1\".").arg(id)));
            return;
        }
        if (args.contains("runMode") || args.contains("timeScale")) {
            const RunMode mode = args.contains("runMode")
                                     ? RunMode(kRunModes.indexOf(args.value("runMode").toString()))
                                     : plant->runMode();
            plant->setRunMode(mode, args.value("timeScale").toDouble(plant->timeScale()));
        }
        if (args.contains("sampleRate")) {
            const int hz = args.value("sampleRate").toInt();
            if (hz < 1 || hz > 10000) {
                reply(AgentToolResult::error(QStringLiteral("sampleRate must be 1-10000 Hz.")));
                return;
            }
            plant->setSampleRate(hz);
        }
        reply(AgentToolResult::json(plantJson(id, plant)));
    };
    registry->registerTool(clock);

    AgentTool settings;
    settings.name = "lab_set_plant_settings";
    settings.title = "Set plant settings";
    settings.description =
        "Changes a plant's own settings (lab_get_state's settings: track, sensor noise, "
        "initial conditions...) by key, then resets it to apply them. An enum takes its "
        "option's index or label.";
    settings.inputSchema = AgentSchema()
                               .string("plantDeviceId", "The plant (lab_get_state).", true)
                               .object("values", "Setting key -> value.", true)
                               .toJson();
    settings.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("plantDeviceId").toString();
        LabPlantDevice* plant = LabDeviceRegistry::instance().plant(id);
        if (!plant) {
            reply(AgentToolResult::error(QStringLiteral("No plant \"%1\".").arg(id)));
            return;
        }
        const QJsonObject values = args.value("values").toObject();
        QVariantMap applied;
        const QVector<PlantSetting> schema = plant->settingsSchema();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const PlantSetting* setting = nullptr;
            for (const PlantSetting& candidate : schema) {
                if (candidate.key == it.key()) {
                    setting = &candidate;
                }
            }
            if (!setting) {
                reply(AgentToolResult::error(
                    QStringLiteral("The plant has no setting \"%1\".").arg(it.key())));
                return;
            }
            QVariant value = it.value().toVariant();
            if (setting->type == PlantSetting::Type::Enum && it.value().isString()) {
                const int index = setting->options.indexOf(it.value().toString());
                if (index < 0) {
                    reply(AgentToolResult::error(QStringLiteral("\"%1\" has no option \"%2\".")
                                                     .arg(it.key(), it.value().toString())));
                    return;
                }
                value = index;
            }
            applied.insert(it.key(), value);
        }
        plant->applySettings(applied);
        reply(AgentToolResult::json(plantJson(id, plant)));
    };
    registry->registerTool(settings);

    AgentTool scene;
    scene.name = "lab_add_scene";
    scene.title = "Add scene";
    scene.description =
        "Places the plant's animated Scene (the car on its track, the pendulum...) on the "
        "active dashboard, bound by name to the plant and its controller.";
    scene.inputSchema =
        AgentSchema().string("plantDeviceId", "The plant (lab_get_state).", true).toJson();
    scene.handler = [host](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("plantDeviceId").toString();
        if (!LabDeviceRegistry::instance().plant(id)) {
            reply(AgentToolResult::error(QStringLiteral("No plant \"%1\".").arg(id)));
            return;
        }
        const QStringList controllers = LabDeviceRegistry::instance().controllersOf(id);
        const QJsonObject config = LabPanel::sceneWidgetConfig(
            id, controllers.isEmpty() ? QString() : controllers.first());
        if (config.isEmpty() ||
            !host->addDashboardWidget(QString::fromLatin1(kSceneWidgetTypeId), config)) {
            reply(AgentToolResult::error(QStringLiteral("No scene for this plant.")));
            return;
        }
        reply(AgentToolResult::text(
            QStringLiteral("Added the %1 scene.").arg(config.value("scene").toString())));
    };
    registry->registerTool(scene);

    AgentTool example;
    example.name = "lab_open_example";
    example.title = "Open TraceLab example";
    example.destructive = true;
    example.description =
        "Opens a benchmark project (plant, controller, dashboards ready to run) after the user "
        "confirms; it replaces the open project, so unsaved changes are lost.";
    example.inputSchema =
        AgentSchema()
            .string("example", "Which benchmark.", true, {"linefollower", "furuta"})
            .toJson();
    example.handler = [host](const QJsonObject& args, const AgentToolReply& reply) {
        const bool furuta = args.value("example").toString() == QLatin1String("furuta");
        const QString name = furuta ? QCoreApplication::translate("LabModule", "Furuta pendulum")
                                    : QCoreApplication::translate("LabModule", "Line follower");
        if (!confirm(QCoreApplication::translate("LabAgentTools", "Open Example"),
                     QCoreApplication::translate(
                         "LabAgentTools",
                         "An AI assistant wants to open the TraceLab example \"%1\". Unsaved "
                         "changes to the current project will be lost.")
                         .arg(name))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        const QString path =
            furuta ? LabPanel::furutaProjectPath() : LabPanel::lineFollowerProjectPath();
        if (!host->openProject(path)) {
            reply(AgentToolResult::error(QStringLiteral("The example could not be opened.")));
            return;
        }
        reply(AgentToolResult::text(QStringLiteral("Opened %1.").arg(name)));
    };
    registry->registerTool(example);
}

}  // namespace tracelab
