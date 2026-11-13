// AgentAppTools, preferences: appearance (options, presets, palettes),
// language and font, application settings, and the diagnostics trails
// (notifications, app log, BTP traffic). See agentapptools.h.

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QUuid>
#include <functional>

#include "agent/toolregistry.h"
#include "core/agentapptools.h"
#include "core/appearancecatalog.h"
#include "core/applog.h"
#include "core/mainwindow.h"
#include "dashboard/widgetconfigeditor.h"
#include "diagnostics/framelog.h"
#include "diagnostics/notificationlog.h"
#include "preferences/appsettings.h"
#include "project/workspacemanager.h"
#include "traceview/appearance.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kDefaultEntries = 50;
constexpr int kDefaultLogLines = 200;
constexpr int kMaxPayloadBytesShown = 64;

QJsonObject appearanceJson(bool subscriptionsShown) {
    QJsonArray options;
    for (const AppearanceOption& option : appearanceOptions()) {
        QJsonArray choices;
        for (const AppearanceChoice& choice : option.choices()) {
            choices.append(QJsonObject{{"id", choice.id}, {"label", choice.label}});
        }
        options.append(QJsonObject{{"key", option.key},
                                   {"label", option.label},
                                   {"current", option.current()},
                                   {"choices", choices}});
    }
    QJsonArray presets;
    for (const AppearancePreset& preset : appearancePresets()) {
        presets.append(
            QJsonObject{{"id", preset.id}, {"name", preset.name}, {"builtIn", preset.builtIn}});
    }
    QJsonArray languages;
    for (const LanguageInfo& language : LanguageManager::instance().availableLanguages()) {
        languages.append(QJsonObject{{"id", language.id}, {"name", language.displayName}});
    }
    QJsonArray fonts;
    for (const FontOption& font : FontManager::instance().availableFonts()) {
        fonts.append(QJsonObject{{"id", font.id}, {"name", font.displayName}});
    }
    const QString active = WorkspaceManager::instance().activeId();
    return QJsonObject{
        {"options", options},
        {"presets", presets},
        {"workspacePinned",
         !subscriptionsShown && !WorkspaceManager::instance().appearanceFor(active).isEmpty()},
        {"currentPalette", paletteToJson(ThemeManager::instance().currentTheme())},
        {"language", LanguageManager::instance().currentLanguage().id},
        {"languages", languages},
        {"font", FontManager::instance().currentFont().id},
        {"fonts", fonts},
    };
}

// One application setting as the assistant sees it.
struct SettingSpec {
    QString key;
    QString help;
    std::function<QJsonValue()> get;
    // Empty when the value is acceptable and applied, else why not.
    std::function<QString(const QJsonValue&)> set;
};

SettingSpec boolSetting(const QString& key, const QString& help, bool (AppSettings::*get)() const,
                        void (AppSettings::*set)(bool)) {
    return {key, help + QStringLiteral(" (bool)"),
            [get] { return QJsonValue((AppSettings::instance().*get)()); },
            [set](const QJsonValue& value) {
                if (!value.isBool()) {
                    return QStringLiteral("expects true or false");
                }
                (AppSettings::instance().*set)(value.toBool());
                return QString();
            }};
}

SettingSpec intSetting(const QString& key, const QString& help, int minimum, int maximum,
                       int (AppSettings::*get)() const, void (AppSettings::*set)(int)) {
    return {
        key, help + QStringLiteral(" (integer %1-%2)").arg(minimum).arg(maximum),
        [get] { return QJsonValue((AppSettings::instance().*get)()); },
        [set, minimum, maximum](const QJsonValue& value) {
            const int number = value.toInt(minimum - 1);
            if (!value.isDouble() || number < minimum || number > maximum) {
                return QStringLiteral("expects an integer from %1 to %2").arg(minimum).arg(maximum);
            }
            (AppSettings::instance().*set)(number);
            return QString();
        }};
}

QVector<SettingSpec> settingSpecs() {
    using RenderProfile = AppSettings::RenderProfile;
    static const QStringList kProfiles = {"low", "medium", "high", "custom", "extraHigh"};
    return {
        {"renderProfile",
         "Dashboard repaint rate: low (30 fps), medium (60), high (120), extraHigh (240) or custom "
         "(customRenderFps)",
         [] { return QJsonValue(kProfiles.value(int(AppSettings::instance().renderProfile()))); },
         [](const QJsonValue& value) {
             const int index = kProfiles.indexOf(value.toString());
             if (index < 0) {
                 return QStringLiteral("expects one of %1").arg(kProfiles.join(", "));
             }
             AppSettings::instance().setRenderProfile(RenderProfile(index));
             return QString();
         }},
        intSetting("customRenderFps", "Repaint rate for the custom render profile", 1, 240,
                   &AppSettings::customRenderFps, &AppSettings::setCustomRenderFps),
        boolSetting("subscribeRateOverrideEnabled",
                    "Subscribe every widget at subscribeRateOverrideHz instead of its own rate",
                    &AppSettings::subscribeRateOverrideEnabled,
                    &AppSettings::setSubscribeRateOverrideEnabled),
        intSetting("subscribeRateOverrideHz", "The override subscribe rate", 1, 1000,
                   &AppSettings::subscribeRateOverrideHz, &AppSettings::setSubscribeRateOverrideHz),
        intSetting("recentProjectsLimit", "How many recent projects File > Open Recent keeps", 1,
                   50, &AppSettings::recentProjectsLimit, &AppSettings::setRecentProjectsLimit),
        boolSetting("autoConnectOnProjectOpen", "Connect a project's devices when it opens",
                    &AppSettings::autoConnectOnProjectOpen,
                    &AppSettings::setAutoConnectOnProjectOpen),
        intSetting("terminalScrollbackLines", "Lines a serial monitor keeps", 100, 1000000,
                   &AppSettings::terminalScrollbackLines, &AppSettings::setTerminalScrollbackLines),
        boolSetting("terminalWordWrap", "Wrap long lines in serial monitors",
                    &AppSettings::terminalWordWrap, &AppSettings::setTerminalWordWrap),
        boolSetting("terminalAutoScroll", "Serial monitors follow new output",
                    &AppSettings::terminalAutoScroll, &AppSettings::setTerminalAutoScroll),
        boolSetting("terminalCursorBlink", "Blinking cursor in serial monitors",
                    &AppSettings::terminalCursorBlink, &AppSettings::setTerminalCursorBlink),
        boolSetting("autoReconnect", "Retry disconnected devices automatically",
                    &AppSettings::autoReconnect, &AppSettings::setAutoReconnect),
        intSetting("reconnectIntervalSeconds", "Seconds between reconnect attempts", 1, 60,
                   &AppSettings::reconnectIntervalSeconds,
                   &AppSettings::setReconnectIntervalSeconds),
        boolSetting("manifestCacheEnabled", "Remember each device's manifest between sessions",
                    &AppSettings::manifestCacheEnabled, &AppSettings::setManifestCacheEnabled),
        boolSetting("manifestCacheSkipOnHello",
                    "Trust the manifest cache without asking when the revision matches (TCP/BLE; "
                    "not recommended)",
                    &AppSettings::manifestCacheSkipOnHello,
                    &AppSettings::setManifestCacheSkipOnHello),
        boolSetting("verboseSerialLogging", "Log every serial byte to the app log (debugging)",
                    &AppSettings::verboseSerialLogging, &AppSettings::setVerboseSerialLogging),
        intSetting("frameLogCapacity", "BTP frames the traffic monitor keeps", 100, 50000,
                   &AppSettings::frameLogCapacity, &AppSettings::setFrameLogCapacity),
        intSetting("notificationHistoryCapacity", "Notifications the history keeps", 50, 10000,
                   &AppSettings::notificationHistoryCapacity,
                   &AppSettings::setNotificationHistoryCapacity),
        boolSetting("updateAutoCheckEnabled", "Check for TraceView updates at startup",
                    &AppSettings::updateAutoCheckEnabled, &AppSettings::setUpdateAutoCheckEnabled),
    };
}

QString frameTypeName(btp::MessageType type) {
    switch (type) {
        case btp::MessageType::Telemetry:
            return QStringLiteral("TELEMETRY");
        case btp::MessageType::Log:
            return QStringLiteral("LOG");
        case btp::MessageType::Command:
            return QStringLiteral("COMMAND");
        case btp::MessageType::Terminal:
            return QStringLiteral("TERMINAL");
        case btp::MessageType::Control:
            return QStringLiteral("CONTROL");
        default:
            return QStringLiteral("INVALID");
    }
}

QString severityName(StatusSeverity severity) {
    return QString::fromLatin1(statusSeverityKey(severity));
}

}  // namespace

void AgentAppTools::registerAppearanceTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool get;
    get.name = "get_appearance";
    get.title = "Get appearance";
    get.readOnly = true;
    get.description =
        "TraceView's look: every appearance option (palette, frame, chartStyle, dataColors, "
        "density, cardHeader, canvas, motion) with its current value and choices, the presets, "
        "whether the active workspace pins its own look, the current palette's colors, and the "
        "language and font with their choices.";
    get.handler = [window](const QJsonObject&, const AgentToolReply& reply) {
        reply(AgentToolResult::json(appearanceJson(window->m_subscriptionsWorkspaceActive)));
    };
    registry->registerTool(get);

    AgentTool set;
    set.name = "set_appearance";
    set.title = "Set appearance";
    set.description =
        "Changes appearance options: values maps option keys to choice ids from "
        "get_appearance, e.g. {\"palette\": \"dark\", \"chartStyle\": \"scientific\"}. Applies "
        "live. If the active workspace pins its look, the change goes to that workspace only.";
    set.inputSchema = AgentSchema().object("values", "Option key -> choice id.", true).toJson();
    set.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QJsonObject values = args.value("values").toObject();
        const QVector<AppearanceOption> options = appearanceOptions();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const AppearanceOption* option = nullptr;
            for (const AppearanceOption& candidate : options) {
                if (candidate.key == it.key()) {
                    option = &candidate;
                }
            }
            if (!option) {
                reply(AgentToolResult::error(
                    QStringLiteral("Unknown appearance option \"%1\".").arg(it.key())));
                return;
            }
            bool known = false;
            for (const AppearanceChoice& choice : option->choices()) {
                known = known || choice.id == it.value().toString();
            }
            if (!known) {
                reply(AgentToolResult::error(QStringLiteral("\"%1\" has no choice \"%2\".")
                                                 .arg(it.key(), it.value().toString())));
                return;
            }
        }
        for (const AppearanceOption& option : options) {
            if (values.contains(option.key)) {
                option.apply(values.value(option.key).toString());
            }
        }
        reply(AgentToolResult::json(appearanceJson(window->m_subscriptionsWorkspaceActive)));
    };
    registry->registerTool(set);

    AgentTool preset;
    preset.name = "apply_appearance_preset";
    preset.title = "Apply appearance preset";
    preset.description = "Applies a whole look at once from get_appearance's presets.";
    preset.inputSchema = AgentSchema().string("presetId", "Preset id.", true).toJson();
    preset.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        for (const AppearancePreset& candidate : appearancePresets()) {
            if (candidate.id == args.value("presetId").toString()) {
                applyAppearance(candidate.values);
                reply(AgentToolResult::text(QStringLiteral("Applied \"%1\".").arg(candidate.name)));
                return;
            }
        }
        reply(AgentToolResult::error(QStringLiteral("No such preset.")));
    };
    registry->registerTool(preset);

    AgentTool savePreset;
    savePreset.name = "save_appearance_preset";
    savePreset.title = "Save appearance preset";
    savePreset.description = "Saves the current look as a user preset. Returns its id.";
    savePreset.inputSchema = AgentSchema().string("name", "Preset name.", true).toJson();
    savePreset.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString().trimmed();
        if (name.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("The name can't be blank.")));
            return;
        }
        reply(AgentToolResult::json(QJsonObject{{"presetId", saveUserAppearancePreset(name)}}));
    };
    registry->registerTool(savePreset);

    AgentTool deletePreset;
    deletePreset.name = "delete_appearance_preset";
    deletePreset.title = "Delete appearance preset";
    deletePreset.destructive = true;
    deletePreset.description = "Deletes a user preset after the user confirms.";
    deletePreset.inputSchema = AgentSchema().string("presetId", "Preset id.", true).toJson();
    deletePreset.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        for (const AppearancePreset& candidate : appearancePresets()) {
            if (candidate.id != args.value("presetId").toString()) {
                continue;
            }
            if (candidate.builtIn) {
                reply(AgentToolResult::error(QStringLiteral("Built-in presets can't be deleted.")));
                return;
            }
            if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Delete Preset"),
                         QCoreApplication::translate(
                             "AgentAppTools",
                             "An AI assistant wants to delete the appearance preset \"%1\".")
                             .arg(candidate.name))) {
                reply(AgentToolResult::error(QStringLiteral("The user declined.")));
                return;
            }
            removeUserAppearancePreset(candidate.id);
            reply(AgentToolResult::text(QStringLiteral("Deleted.")));
            return;
        }
        reply(AgentToolResult::error(QStringLiteral("No such preset.")));
    };
    registry->registerTool(deletePreset);

    AgentTool pin;
    pin.name = "pin_workspace_appearance";
    pin.title = "Pin workspace appearance";
    pin.description =
        "Pins the current look to the active workspace (it comes back whenever that workspace "
        "is shown), or unpins it so the workspace follows the app-wide look again.";
    pin.inputSchema = AgentSchema().boolean("pinned", "Pin or unpin.", true).toJson();
    pin.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        if (window->m_subscriptionsWorkspaceActive) {
            reply(AgentToolResult::error(QStringLiteral("Switch to a workspace first.")));
            return;
        }
        window->setWorkspaceAppearancePinned(args.value("pinned").toBool());
        reply(AgentToolResult::text(QStringLiteral("Done.")));
    };
    registry->registerTool(pin);

    const QString paletteHelp = QStringLiteral(
        "Colors as \"#rrggbb\" (or \"#aarrggbb\"): background, surface, surfaceAlt, border, "
        "borderStrong, textPrimary, textSecondary, textDisabled, accent, accentHover, "
        "accentPressed, success, warning, danger, and series (array, the default data colors). "
        "Keys left out keep the base palette's.");

    AgentTool createPalette;
    createPalette.name = "create_palette";
    createPalette.title = "Create palette";
    createPalette.description =
        "Creates a custom color palette from the current one (or basePaletteId) with the given "
        "colors and switches to it. Returns its id.";
    createPalette.inputSchema = AgentSchema()
                                    .string("name", "Palette name.", true)
                                    .object("colors", paletteHelp)
                                    .string("basePaletteId", "Palette to start from.")
                                    .toJson();
    createPalette.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        ThemeManager& theme = ThemeManager::instance();
        ThemePalette base = theme.currentTheme();
        if (args.contains("basePaletteId")) {
            bool found = false;
            for (const ThemePalette& candidate : theme.availableThemes()) {
                if (candidate.id == args.value("basePaletteId").toString()) {
                    base = candidate;
                    found = true;
                }
            }
            if (!found) {
                reply(AgentToolResult::error(QStringLiteral("No such base palette.")));
                return;
            }
        }
        QJsonObject json = args.value("colors").toObject();
        json["id"] = QStringLiteral("custom:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        json["name"] = args.value("name").toString();
        const ThemePalette palette = paletteFromJson(json, base);
        theme.saveCustomTheme(palette);
        theme.setTheme(palette.id);
        reply(AgentToolResult::json(paletteToJson(palette)));
    };
    registry->registerTool(createPalette);

    AgentTool updatePalette;
    updatePalette.name = "update_palette";
    updatePalette.title = "Update palette";
    updatePalette.description = "Changes a custom palette's name and/or colors.";
    updatePalette.inputSchema =
        AgentSchema()
            .string("paletteId", "A custom palette id (\"custom:...\").", true)
            .string("name", "New name.")
            .object("colors", paletteHelp)
            .toJson();
    updatePalette.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        ThemeManager& theme = ThemeManager::instance();
        const QString id = args.value("paletteId").toString();
        for (const ThemePalette& candidate : theme.availableThemes()) {
            if (candidate.id != id) {
                continue;
            }
            if (!isCustomPaletteId(id)) {
                reply(AgentToolResult::error(QStringLiteral("Only custom palettes can change.")));
                return;
            }
            QJsonObject json = args.value("colors").toObject();
            json["id"] = id;
            json["name"] = args.value("name").toString(candidate.displayName);
            const ThemePalette palette = paletteFromJson(json, candidate);
            theme.saveCustomTheme(palette);
            reply(AgentToolResult::json(paletteToJson(palette)));
            return;
        }
        reply(AgentToolResult::error(QStringLiteral("No such palette.")));
    };
    registry->registerTool(updatePalette);

    AgentTool deletePalette;
    deletePalette.name = "delete_palette";
    deletePalette.title = "Delete palette";
    deletePalette.destructive = true;
    deletePalette.description = "Deletes a custom palette after the user confirms.";
    deletePalette.inputSchema =
        AgentSchema().string("paletteId", "A custom palette id.", true).toJson();
    deletePalette.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        ThemeManager& theme = ThemeManager::instance();
        const QString id = args.value("paletteId").toString();
        for (const ThemePalette& candidate : theme.availableThemes()) {
            if (candidate.id != id) {
                continue;
            }
            if (!isCustomPaletteId(id)) {
                reply(
                    AgentToolResult::error(QStringLiteral("Built-in palettes can't be deleted.")));
                return;
            }
            if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Delete Palette"),
                         QCoreApplication::translate(
                             "AgentAppTools", "An AI assistant wants to delete the palette \"%1\".")
                             .arg(candidate.displayName))) {
                reply(AgentToolResult::error(QStringLiteral("The user declined.")));
                return;
            }
            theme.removeCustomTheme(id);
            reply(AgentToolResult::text(QStringLiteral("Deleted.")));
            return;
        }
        reply(AgentToolResult::error(QStringLiteral("No such palette.")));
    };
    registry->registerTool(deletePalette);

    AgentTool language;
    language.name = "set_language";
    language.title = "Set language";
    language.description =
        "Changes TraceView's interface language (ids from get_appearance). It applies after "
        "TraceView restarts.";
    language.inputSchema = AgentSchema().string("languageId", "Language id.", true).toJson();
    language.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("languageId").toString();
        for (const LanguageInfo& info : LanguageManager::instance().availableLanguages()) {
            if (info.id == id) {
                LanguageManager::instance().setLanguage(id);
                reply(AgentToolResult::text(
                    QStringLiteral("%1 applies after a restart.").arg(info.displayName)));
                return;
            }
        }
        reply(AgentToolResult::error(QStringLiteral("No such language.")));
    };
    registry->registerTool(language);

    AgentTool font;
    font.name = "set_font";
    font.title = "Set font";
    font.description = "Changes the interface font (ids from get_appearance). Applies live.";
    font.inputSchema = AgentSchema().string("fontId", "Font id.", true).toJson();
    font.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("fontId").toString();
        for (const FontOption& option : FontManager::instance().availableFonts()) {
            if (option.id == id) {
                FontManager::instance().setFont(id);
                reply(AgentToolResult::text(QStringLiteral("Done.")));
                return;
            }
        }
        reply(AgentToolResult::error(QStringLiteral("No such font.")));
    };
    registry->registerTool(font);
}

void AgentAppTools::registerSettingsTools(MainWindow* window, ToolRegistry* registry) {
    Q_UNUSED(window);
    AgentTool get;
    get.name = "get_settings";
    get.title = "Get settings";
    get.readOnly = true;
    get.description =
        "TraceView's application settings (Settings window: dashboard rendering, connections, "
        "terminal, diagnostics, updates) with their current values and what each accepts. "
        "Appearance and language have their own tools.";
    get.handler = [](const QJsonObject&, const AgentToolReply& reply) {
        QJsonObject values;
        QJsonObject help;
        for (const SettingSpec& spec : settingSpecs()) {
            values[spec.key] = spec.get();
            help[spec.key] = spec.help;
        }
        reply(AgentToolResult::json(QJsonObject{{"values", values}, {"help", help}}));
    };
    registry->registerTool(get);

    AgentTool update;
    update.name = "update_settings";
    update.title = "Update settings";
    update.description =
        "Changes application settings: values maps setting keys (get_settings) to new values. "
        "All are checked before any is applied.";
    update.inputSchema = AgentSchema().object("values", "Setting key -> value.", true).toJson();
    update.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QJsonObject values = args.value("values").toObject();
        const QVector<SettingSpec> specs = settingSpecs();
        QHash<QString, const SettingSpec*> byKey;
        for (const SettingSpec& spec : specs) {
            byKey.insert(spec.key, &spec);
        }
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (!byKey.contains(it.key())) {
                reply(AgentToolResult::error(
                    QStringLiteral("Unknown setting \"%1\".").arg(it.key())));
                return;
            }
        }
        // Applied in turn; a bad value stops there with the earlier ones in
        // effect, which the reply's values show.
        QJsonObject applied;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            const QString problem = byKey.value(it.key())->set(it.value());
            if (!problem.isEmpty()) {
                reply(
                    AgentToolResult::error(QStringLiteral("\"%1\" %2. Applied before it: %3.")
                                               .arg(it.key(), problem, applied.keys().join(", "))));
                return;
            }
            applied[it.key()] = byKey.value(it.key())->get();
        }
        reply(AgentToolResult::json(QJsonObject{{"applied", applied}}));
    };
    registry->registerTool(update);
}

void AgentAppTools::registerDiagnosticsTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool notifications;
    notifications.name = "read_notifications";
    notifications.title = "Read notifications";
    notifications.readOnly = true;
    notifications.description =
        "The latest status-bar notifications (connections, sessions, subscriptions, errors), "
        "oldest first, with severity and the device they came from.";
    notifications.inputSchema =
        AgentSchema().integer("max", "How many of the latest (default 50).").toJson();
    notifications.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QVector<NotificationEntry>& entries = window->m_notificationLog->entries();
        const int max = qMax(1, args.contains("max") ? args.value("max").toInt() : kDefaultEntries);
        QJsonArray list;
        for (int i = qMax(0, int(entries.size()) - max); i < entries.size(); ++i) {
            const NotificationEntry& entry = entries[i];
            list.append(QJsonObject{{"time", entry.timestamp.toString(Qt::ISODateWithMs)},
                                    {"severity", severityName(entry.severity)},
                                    {"source", entry.source},
                                    {"text", entry.text}});
        }
        reply(AgentToolResult::json(QJsonObject{{"notifications", list}}));
    };
    registry->registerTool(notifications);

    AgentTool appLog;
    appLog.name = "read_app_log";
    appLog.title = "Read app log";
    appLog.readOnly = true;
    appLog.description =
        "The end of TraceView's own log file for this session (connections, transports, "
        "errors), for diagnosing a problem.";
    appLog.inputSchema =
        AgentSchema().integer("maxLines", "How many of the last lines (default 200).").toJson();
    appLog.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        QFile file(AppLog::currentLogFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            reply(AgentToolResult::error(QStringLiteral("The log file can't be read.")));
            return;
        }
        const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
        const int max =
            qMax(1, args.contains("maxLines") ? args.value("maxLines").toInt() : kDefaultLogLines);
        reply(AgentToolResult::text(lines.mid(qMax(0, int(lines.size()) - max)).join('\n')));
    };
    registry->registerTool(appLog);

    AgentTool traffic;
    traffic.name = "read_btp_traffic";
    traffic.title = "Read BTP traffic";
    traffic.readOnly = true;
    traffic.description =
        "The latest BTP frames TraceView sent and received (what the BTP monitor shows): "
        "direction, device, message type, object id, source, sequence, payload (hex, first 64 "
        "bytes) and decode errors.";
    traffic.inputSchema = AgentSchema()
                              .string("deviceId", "Only this device.")
                              .integer("max", "How many of the latest (default 50).")
                              .toJson();
    traffic.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString only = args.value("deviceId").toString();
        const int max = qMax(1, args.contains("max") ? args.value("max").toInt() : kDefaultEntries);
        const QVector<FrameLogEntry>& entries = window->m_frameLog->entries();
        QJsonArray list;
        for (int i = int(entries.size()) - 1; i >= 0 && list.size() < max; --i) {
            const FrameLogEntry& entry = entries[i];
            if (!only.isEmpty() && entry.deviceId != only) {
                continue;
            }
            QJsonObject frame{
                {"time", entry.wallClock.toString(Qt::ISODateWithMs)},
                {"direction", entry.direction == FrameDirection::Inbound ? "in" : "out"},
                {"device", entry.deviceName},
            };
            if (entry.decodeError) {
                frame["decodeError"] = entry.errorText;
            } else {
                frame["type"] = frameTypeName(entry.frame.type);
                frame["objectId"] = formatHexId(entry.frame.objectId, 4);
                frame["sourceId"] = formatHexId(entry.frame.sourceId, 8);
                frame["sequence"] = double(entry.frame.sequence);
                frame["payloadBytes"] = int(entry.frame.payload.size());
                frame["payload"] =
                    QString::fromLatin1(entry.frame.payload.left(kMaxPayloadBytesShown).toHex(' '));
            }
            list.prepend(frame);
        }
        reply(AgentToolResult::json(QJsonObject{{"frames", list}}));
    };
    registry->registerTool(traffic);
}

}  // namespace traceview
