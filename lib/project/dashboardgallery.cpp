#include "dashboardgallery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

#include "project/projectstore.h"

namespace traceview {

namespace {
constexpr char kDefaultSettingsKey[] = "gallery/defaultDashboard";
constexpr char kSuffix[] = ".tvproj";

// Not a QObject, so no tr() -- same as ProjectStore. Callers wrap each
// literal in QT_TRANSLATE_NOOP so lupdate still extracts it.
QString translated(const char* text) {
    return QCoreApplication::translate("DashboardGallery", text);
}
}  // namespace

const QString DashboardGallery::kBuiltInExamplePath = QStringLiteral(":/projects/example.tvproj");

QString DashboardGallery::directory() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/dashboards";
    QDir().mkpath(dir);
    return dir;
}

QStringList DashboardGallery::names() {
    QStringList result;
    const QFileInfoList files =
        QDir(directory()).entryInfoList({QString("*") + kSuffix}, QDir::Files | QDir::Readable);
    for (const QFileInfo& info : files) {
        result.append(info.completeBaseName());
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QString DashboardGallery::pathFor(const QString& name) {
    return directory() + '/' + name + kSuffix;
}

bool DashboardGallery::contains(const QString& name) {
    return !name.isEmpty() && QFileInfo::exists(pathFor(name));
}

QString DashboardGallery::nameForPath(const QString& path) {
    if (path.isEmpty() || isBuiltInExample(path)) {
        return {};
    }
    const QFileInfo info(path);
    if (QDir(info.absolutePath()) != QDir(directory()) ||
        info.suffix() != QString(kSuffix).mid(1)) {
        return {};
    }
    return info.completeBaseName();
}

QString DashboardGallery::defaultName() {
    return QSettings().value(kDefaultSettingsKey).toString();
}

void DashboardGallery::setDefaultName(const QString& name) {
    QSettings settings;
    if (name.isEmpty()) {
        settings.remove(kDefaultSettingsKey);
    } else {
        settings.setValue(kDefaultSettingsKey, name);
    }
}

QString DashboardGallery::startupPath() {
    const QString name = defaultName();
    return contains(name) ? pathFor(name) : kBuiltInExamplePath;
}

bool DashboardGallery::isValidName(const QString& name, QString* error) {
    auto fail = [error](const char* text) {
        if (error != nullptr) {
            *error = translated(text);
        }
        return false;
    };
    if (name.trimmed().isEmpty()) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery", "The name can't be empty."));
    }
    if (name != name.trimmed() || name.endsWith('.') || name.startsWith('.')) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery",
                                      "The name can't start or end with a space or a dot."));
    }
    static const QString kForbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name) {
        if (kForbidden.contains(c) || c.unicode() < 0x20) {
            return fail(QT_TRANSLATE_NOOP(
                "DashboardGallery",
                "The name can't contain any of these characters: \\ / : * ? \" < > |"));
        }
    }
    if (name.size() > 100) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery",
                                      "The name is too long (100 characters at most)."));
    }
    return true;
}

GalleryEntryInfo DashboardGallery::info(const QString& name) {
    GalleryEntryInfo result = infoForPath(pathFor(name));
    result.name = name;
    return result;
}

GalleryEntryInfo DashboardGallery::infoForPath(const QString& path) {
    GalleryEntryInfo result;
    result.path = path;
    result.name = QFileInfo(path).completeBaseName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return result;
    }
    const QJsonObject root = doc.object();
    const QString own = root.value("gallery").toObject().value("icon").toString();
    QString firstWorkspaceIcon;
    const QJsonArray workspaces =
        root.value("workspaces").toObject().value("workspaces").toArray();
    result.workspaces = int(workspaces.size());
    for (const QJsonValue& value : workspaces) {
        const QJsonObject workspace = value.toObject();
        result.widgets += int(workspace.value("dashboard").toObject().value("items").toArray().size());
        if (firstWorkspaceIcon.isEmpty()) {
            firstWorkspaceIcon = workspace.value("icon").toString();
        }
    }
    result.iconIsCustom = !own.isEmpty();
    result.icon = result.iconIsCustom ? own : firstWorkspaceIcon;
    return result;
}

bool DashboardGallery::setIcon(const QString& name, const QString& iconId, QString* error) {
    return setIconForPath(pathFor(name), iconId, error);
}

bool DashboardGallery::setIconForPath(const QString& path, const QString& iconId,
                                      QString* error) {
    auto fail = [error](const char* text) {
        if (error != nullptr) {
            *error = translated(text);
        }
        return false;
    };
    if (isBuiltInExample(path)) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery",
                                      "The built-in example can't be changed."));
    }
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery", "Couldn't read the dashboard file."));
    }
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
    in.close();
    if (!doc.isObject()) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery", "Couldn't read the dashboard file."));
    }
    QJsonObject root = doc.object();
    QJsonObject section = root.value("gallery").toObject();
    if (iconId.isEmpty()) {
        section.remove("icon");
    } else {
        section["icon"] = iconId;
    }
    if (section.isEmpty()) {
        root.remove("gallery");
    } else {
        root["gallery"] = section;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery", "Couldn't write the dashboard file."));
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        return fail(QT_TRANSLATE_NOOP("DashboardGallery", "Couldn't write the dashboard file."));
    }
    // The open project keeps its own copy of the file; without this its next
    // Save would write the old section back over the new icon.
    ProjectStore& store = ProjectStore::instance();
    if (!store.currentPath().isEmpty() &&
        QFileInfo(store.currentPath()).absoluteFilePath() == QFileInfo(path).absoluteFilePath()) {
        if (section.isEmpty()) {
            store.removeSection("gallery");
        } else {
            store.setSection("gallery", section);
        }
    }
    return true;
}

bool DashboardGallery::rename(const QString& oldName, const QString& newName, QString* error) {
    if (!isValidName(newName, error)) {
        return false;
    }
    if (oldName == newName) {
        return true;
    }
    // A case-only rename is the same file on Windows/macOS, so it can't be
    // told apart from a clash by exists() alone.
    if (contains(newName) && oldName.compare(newName, Qt::CaseInsensitive) != 0) {
        if (error != nullptr) {
            *error = translated(QT_TRANSLATE_NOOP("DashboardGallery",
                                                 "A dashboard with that name already exists."));
        }
        return false;
    }
    if (!QFile::rename(pathFor(oldName), pathFor(newName))) {
        if (error != nullptr) {
            *error = translated(
                QT_TRANSLATE_NOOP("DashboardGallery", "Couldn't rename the dashboard file."));
        }
        return false;
    }
    if (defaultName() == oldName) {
        setDefaultName(newName);
    }
    return true;
}

bool DashboardGallery::remove(const QString& name, QString* error) {
    if (!QFile::remove(pathFor(name))) {
        if (error != nullptr) {
            *error = translated(
                QT_TRANSLATE_NOOP("DashboardGallery", "Couldn't delete the dashboard file."));
        }
        return false;
    }
    if (defaultName() == name) {
        setDefaultName({});
    }
    return true;
}

}  // namespace traceview
