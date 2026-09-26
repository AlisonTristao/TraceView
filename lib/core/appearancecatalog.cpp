#include "appearancecatalog.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSettings>
#include <QUuid>

#include "dashboard/widgets/chartstyle.h"
#include "preferences/appsettings.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr char kUserPresetsKey[] = "appearance/userPresets";

// Menu titles and row labels keep the contexts they were first translated
// in (MainWindow's View menu, SettingsPage's rows), so existing
// translations of "&Palette"/"Palette" etc. carry over.
QString menuTitle(const char* text) {
    return QCoreApplication::translate("traceview::MainWindow", text);
}

QString rowLabel(const char* text) {
    return QCoreApplication::translate("traceview::SettingsPage", text);
}

template <typename Id, typename IdFn, typename NameFn>
std::function<QVector<AppearanceChoice>()> choicesOf(QVector<Id> (*all)(), IdFn idString,
                                                     NameFn displayName) {
    return [all, idString, displayName]() {
        QVector<AppearanceChoice> choices;
        for (Id id : all()) {
            choices.append({idString(id), displayName(id)});
        }
        return choices;
    };
}

// Listeners of presets being saved/removed -- not a Qt signal since this
// file has no QObject of its own.
QVector<QPair<QPointer<QObject>, std::function<void()>>>& presetListeners() {
    static QVector<QPair<QPointer<QObject>, std::function<void()>>> listeners;
    return listeners;
}

void notifyPresetListeners() {
    auto& listeners = presetListeners();
    for (int i = listeners.size() - 1; i >= 0; --i) {
        if (listeners[i].first.isNull()) {
            listeners.removeAt(i);
        }
    }
    const auto copy = listeners;
    for (const auto& listener : copy) {
        if (!listener.first.isNull()) {
            listener.second();
        }
    }
}

QJsonArray storedUserPresets() {
    return QJsonDocument::fromJson(QSettings().value(kUserPresetsKey).toByteArray()).array();
}

void storeUserPresets(const QJsonArray& presets) {
    QSettings().setValue(kUserPresetsKey, QJsonDocument(presets).toJson(QJsonDocument::Compact));
    notifyPresetListeners();
}

AppearanceSnapshot snapshot(const char* palette, const char* frame, const char* chartStyle,
                            const char* dataColors, const char* density,
                            const char* cardHeader, const char* canvas) {
    return {{QStringLiteral("palette"), QString::fromLatin1(palette)},
            {QStringLiteral("frame"), QString::fromLatin1(frame)},
            {QStringLiteral("chartStyle"), QString::fromLatin1(chartStyle)},
            {QStringLiteral("dataColors"), QString::fromLatin1(dataColors)},
            {QStringLiteral("density"), QString::fromLatin1(density)},
            {QStringLiteral("cardHeader"), QString::fromLatin1(cardHeader)},
            {QStringLiteral("canvas"), QString::fromLatin1(canvas)}};
}

}  // namespace

QVector<AppearanceOption> appearanceOptions() {
    ThemeManager& theme = ThemeManager::instance();
    QVector<AppearanceOption> options;

    AppearanceOption palette;
    palette.key = QStringLiteral("palette");
    palette.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "&Palette"));
    palette.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Palette"));
    palette.choices = [&theme]() {
        QVector<AppearanceChoice> choices;
        for (const ThemePalette& p : theme.availableThemes()) {
            choices.append({p.id, p.displayName});
        }
        return choices;
    };
    palette.current = [&theme]() { return theme.currentTheme().id; };
    palette.apply = [&theme](const QString& id) {
        if (id != theme.currentTheme().id) {
            theme.setTheme(id);
        }
    };
    options.append(palette);

    AppearanceOption frame;
    frame.key = QStringLiteral("frame");
    frame.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "Fra&me"));
    frame.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Frame"));
    frame.choices = choicesOf(&allFrameStyles, frameStyleIdString, frameStyleDisplayName);
    frame.current = [&theme]() { return frameStyleIdString(theme.currentFrameStyle().id); };
    frame.apply = [&theme](const QString& id) { theme.setFrameStyle(id); };
    options.append(frame);

    AppearanceOption chart;
    chart.key = QStringLiteral("chartStyle");
    chart.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "&Chart Style"));
    chart.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Chart style"));
    chart.choices = choicesOf(&allChartStyles, chartStyleIdString, chartStyleDisplayName);
    chart.current = []() {
        return chartStyleIdString(chartStyleFromId(AppSettings::instance().chartStyleId()));
    };
    chart.apply = [](const QString& id) {
        AppSettings::instance().setChartStyleId(chartStyleIdString(chartStyleFromId(id)));
    };
    options.append(chart);

    AppearanceOption dataColors;
    dataColors.key = QStringLiteral("dataColors");
    dataColors.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "&Data Colors"));
    dataColors.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Data colors"));
    dataColors.choices = choicesOf(&allDataColors, dataColorsIdString, dataColorsDisplayName);
    dataColors.current = [&theme]() { return dataColorsIdString(theme.dataColors()); };
    dataColors.apply = [&theme](const QString& id) { theme.setDataColors(id); };
    options.append(dataColors);

    AppearanceOption densityOption;
    densityOption.key = QStringLiteral("density");
    densityOption.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "&Density"));
    densityOption.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Density"));
    densityOption.choices = choicesOf(&allDensities, densityIdString, densityDisplayName);
    densityOption.current = [&theme]() { return densityIdString(theme.currentDensity().id); };
    densityOption.apply = [&theme](const QString& id) { theme.setDensity(id); };
    options.append(densityOption);

    AppearanceOption header;
    header.key = QStringLiteral("cardHeader");
    header.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "Card &Header"));
    header.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Card header"));
    header.choices = choicesOf(&allCardHeaders, cardHeaderIdString, cardHeaderDisplayName);
    // The stored choice, not the touch-screen fallback cardHeader() returns.
    header.current = [&theme]() { return cardHeaderIdString(theme.cardHeaderChoice()); };
    header.apply = [&theme](const QString& id) { theme.setCardHeader(id); };
    options.append(header);

    AppearanceOption canvas;
    canvas.key = QStringLiteral("canvas");
    canvas.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "Can&vas"));
    canvas.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Canvas"));
    canvas.choices = choicesOf(&allCanvases, canvasIdString, canvasDisplayName);
    canvas.current = [&theme]() { return canvasIdString(theme.canvas()); };
    canvas.apply = [&theme](const QString& id) { theme.setCanvas(id); };
    options.append(canvas);

    AppearanceOption motion;
    motion.key = QStringLiteral("motion");
    motion.menuTitle = menuTitle(QT_TRANSLATE_NOOP("traceview::MainWindow", "M&otion"));
    motion.label = rowLabel(QT_TRANSLATE_NOOP("traceview::SettingsPage", "Motion"));
    motion.choices = choicesOf(&allMotions, motionIdString, motionDisplayName);
    motion.current = [&theme]() { return motionIdString(theme.motion()); };
    motion.apply = [&theme](const QString& id) { theme.setMotion(id); };
    motion.inSnapshot = false;
    options.append(motion);

    return options;
}

AppearanceSnapshot currentAppearance() {
    AppearanceSnapshot values;
    for (const AppearanceOption& option : appearanceOptions()) {
        if (option.inSnapshot) {
            values.insert(option.key, option.current());
        }
    }
    return values;
}

void applyAppearance(const AppearanceSnapshot& snapshot) {
    for (const AppearanceOption& option : appearanceOptions()) {
        const auto it = snapshot.constFind(option.key);
        if (option.inSnapshot && it != snapshot.constEnd() && option.current() != it.value()) {
            option.apply(it.value());
        }
    }
}

QJsonObject appearanceToJson(const AppearanceSnapshot& snapshot) {
    QJsonObject json;
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
        json[it.key()] = it.value();
    }
    return json;
}

AppearanceSnapshot appearanceFromJson(const QJsonObject& json) {
    AppearanceSnapshot snapshot;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (it.value().isString()) {
            snapshot.insert(it.key(), it.value().toString());
        }
    }
    return snapshot;
}

void connectAppearanceChanged(QObject* context, const std::function<void()>& onChange) {
    QObject::connect(&ThemeManager::instance(), &ThemeManager::appearanceChanged, context,
                     onChange);
    QObject::connect(&ThemeManager::instance(), &ThemeManager::themesChanged, context, onChange);
    QObject::connect(&AppSettings::instance(), &AppSettings::chartStyleChanged, context,
                     onChange);
    presetListeners().append({QPointer<QObject>(context), onChange});
}

QVector<AppearancePreset> appearancePresets() {
    auto name = [](const char* text) { return QCoreApplication::translate("Appearance", text); };
    QVector<AppearancePreset> presets = {
        {QStringLiteral("builtin:traceview"), name(QT_TRANSLATE_NOOP("Appearance", "TraceView")),
         snapshot("dark", "rounded", "dashboard", "custom", "normal", "filled", "plain")},
        {QStringLiteral("builtin:lab"), name(QT_TRANSLATE_NOOP("Appearance", "Lab")),
         snapshot("light", "square", "engineering", "matlab", "normal", "line", "grid")},
        {QStringLiteral("builtin:paper"), name(QT_TRANSLATE_NOOP("Appearance", "Paper")),
         snapshot("light", "borderless", "scientific", "tableau", "comfortable", "line", "plain")},
        {QStringLiteral("builtin:hud"), name(QT_TRANSLATE_NOOP("Appearance", "HUD")),
         snapshot("matrix", "chamfered", "engineering", "palette", "compact", "line", "grid")},
        {QStringLiteral("builtin:synthwave"), name(QT_TRANSLATE_NOOP("Appearance", "Synthwave")),
         snapshot("synthwave", "chamfered", "dashboard", "palette", "normal", "filled",
                  "gradient")},
        {QStringLiteral("builtin:accessible"), name(QT_TRANSLATE_NOOP("Appearance", "Accessible")),
         snapshot("light", "rounded", "scientific", "okabeIto", "comfortable", "filled", "plain")},
    };
    for (const QJsonValue& value : storedUserPresets()) {
        const QJsonObject object = value.toObject();
        AppearancePreset preset;
        preset.id = object.value("id").toString();
        preset.name = object.value("name").toString();
        preset.values = appearanceFromJson(object.value("values").toObject());
        preset.builtIn = false;
        if (!preset.id.isEmpty() && !preset.name.isEmpty()) {
            presets.append(preset);
        }
    }
    return presets;
}

QString saveUserAppearancePreset(const QString& name) {
    QJsonArray presets = storedUserPresets();
    QString id;
    for (int i = 0; i < presets.size(); ++i) {
        QJsonObject object = presets[i].toObject();
        if (object.value("name").toString() == name) {
            id = object.value("id").toString();
            object["values"] = appearanceToJson(currentAppearance());
            presets[i] = object;
        }
    }
    if (id.isEmpty()) {
        id = QStringLiteral("user:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject object;
        object["id"] = id;
        object["name"] = name;
        object["values"] = appearanceToJson(currentAppearance());
        presets.append(object);
    }
    storeUserPresets(presets);
    return id;
}

void removeUserAppearancePreset(const QString& id) {
    QJsonArray presets = storedUserPresets();
    for (int i = presets.size() - 1; i >= 0; --i) {
        if (presets[i].toObject().value("id").toString() == id) {
            presets.removeAt(i);
        }
    }
    storeUserPresets(presets);
}

}  // namespace traceview
