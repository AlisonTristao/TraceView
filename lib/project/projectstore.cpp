#include "projectstore.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace traceview {

namespace {
constexpr int kFormatVersion = 1;
} // namespace

ProjectStore& ProjectStore::instance() {
    static ProjectStore store;
    return store;
}

QJsonObject ProjectStore::section(const QString& key) const {
    return m_root.value(key).toObject();
}

void ProjectStore::setSection(const QString& key, const QJsonObject& value) {
    m_root[key] = value;
}

bool ProjectStore::save() {
    if (m_currentPath.isEmpty()) {
        // ProjectStore is not a QObject, so tr() isn't available here; use
        // QCoreApplication::translate() with an explicit context instead.
        m_lastError = QCoreApplication::translate(
            "ProjectStore", "No project path set yet — use Save Project to choose a file first.");
        return false;
    }
    return saveAs(m_currentPath);
}

bool ProjectStore::saveAs(const QString& filePath) {
    QJsonObject meta;
    meta["formatVersion"] = kFormatVersion;
    m_root["traceview"] = meta;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = file.errorString();
        return false;
    }

    file.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        m_lastError = file.errorString();
        return false;
    }

    m_currentPath = filePath;
    return true;
}

bool ProjectStore::load(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = file.errorString();
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = parseError.errorString();
        return false;
    }

    m_root = doc.object();
    m_currentPath = filePath;
    return true;
}

void ProjectStore::reset() {
    m_root = QJsonObject();
    m_currentPath.clear();
    m_lastError.clear();
}

} // namespace traceview
