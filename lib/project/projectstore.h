#pragma once

#include <QJsonObject>
#include <QString>

namespace traceview {

// Holds the on-disk project file as a set of independent top-level JSON
// sections (e.g. "dashboard"). Adding a new kind of persisted config later
// is just calling setSection() with a new key — this class never needs to
// change. See docs/DASHBOARD.md for the current section layout.
class ProjectStore {
public:
    static ProjectStore& instance();

    QJsonObject section(const QString& key) const;
    void setSection(const QString& key, const QJsonObject& value);

    // Writes to the last path used by saveAs()/load(). Returns false (and
    // sets lastError()) if no path is known yet.
    bool save();
    bool saveAs(const QString& filePath);
    bool load(const QString& filePath);

    // Drops all sections and the current path, as if the app had just
    // started — used by New Project to start from a blank slate.
    void reset();

    QString currentPath() const { return m_currentPath; }
    QString lastError() const { return m_lastError; }

private:
    ProjectStore() = default;

    QJsonObject m_root;
    QString m_currentPath;
    QString m_lastError;
};

} // namespace traceview
