#pragma once

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

namespace traceview {

// One list describing every app-wide appearance choice -- palette, frame,
// chart style, data colors, density, card header, canvas, motion -- so the
// View menu, Settings > Appearance, the presets and the per-workspace
// appearance are all built from the same place instead of each hand-rolling
// its own combo box or action group. The choices themselves live where they
// are applied (ThemeManager, AppSettings); this only describes and reaches
// them. See "Appearance" in docs/VISUAL_IDENTITY.md.

struct AppearanceChoice {
    QString id;
    QString label;  // translated
};

struct AppearanceOption {
    QString key;        // snapshot/JSON key: "palette", "frame", ...
    QString menuTitle;  // View menu, with its & accelerator
    QString label;      // Settings > Appearance row
    std::function<QVector<AppearanceChoice>()> choices;
    std::function<QString()> current;
    std::function<void(const QString&)> apply;
    // Part of presets and per-workspace snapshots. Motion is not -- it is an
    // accessibility preference that should follow the person, not the look.
    bool inSnapshot = true;
};

QVector<AppearanceOption> appearanceOptions();

// key -> choice id for every option with inSnapshot.
using AppearanceSnapshot = QMap<QString, QString>;

AppearanceSnapshot currentAppearance();
// Applies every key it knows; unknown keys and missing options are left as
// they are.
void applyAppearance(const AppearanceSnapshot& snapshot);
QJsonObject appearanceToJson(const AppearanceSnapshot& snapshot);
AppearanceSnapshot appearanceFromJson(const QJsonObject& json);

// Calls `onChange` (in `context`'s thread, disconnected with it) whenever
// any appearance choice or the list of palettes changes.
void connectAppearanceChanged(QObject* context, const std::function<void()>& onChange);

// A named look: built-in ones ship with the app, user ones are saved from
// the current appearance (QSettings "appearance/userPresets").
struct AppearancePreset {
    QString id;
    QString name;
    AppearanceSnapshot values;
    bool builtIn = true;
};

QVector<AppearancePreset> appearancePresets();
// Saves the current appearance under `name` (a new user preset, or the
// existing user preset of that name). Returns its id.
QString saveUserAppearancePreset(const QString& name);
void removeUserAppearancePreset(const QString& id);

}  // namespace traceview
