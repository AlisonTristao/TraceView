#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace traceview {

// One named dashboard layout. `dashboard` is exactly what DashboardGrid::
// toJson()/fromJson() produce/consume -- WorkspaceManager never looks inside
// it.
struct Workspace {
    QString id;
    QString name;
    QJsonObject dashboard;
};

// Holds every workspace ("N dashboard layouts per project") and which one is
// active. A process-wide singleton, same shape as ProjectStore (no QObject,
// no signals) -- MainWindow calls it directly and refreshes the UI itself
// after each mutation, exactly like it already does around ProjectStore.
// Persisted as the "workspaces" section of the .tvproj file (see
// docs/DASHBOARD.md "Project file"); MainWindow is responsible for keeping
// the active workspace's `dashboard` field in sync with the live
// DashboardGrid before every save/switch (setDashboardFor()).
class WorkspaceManager {
public:
    static WorkspaceManager& instance();

    const QVector<Workspace>& workspaces() const { return m_workspaces; }
    QString activeId() const { return m_activeId; }

    // Empty QJsonObject if `id` is unknown.
    QJsonObject dashboardFor(const QString& id) const;
    // No-op if `id` is unknown.
    void setDashboardFor(const QString& id, const QJsonObject& dashboard);
    // Empty QString if `id` is unknown.
    QString nameFor(const QString& id) const;

    // Adds a new workspace with an empty dashboard and makes it active.
    // `name` is disambiguated against existing names ("Workspace" ->
    // "Workspace 2") rather than rejected. Returns the new id.
    QString createWorkspace(const QString& name);
    // No-op if `id` is unknown or it's the only workspace left (there must
    // always be at least one). If `id` was active, activeId() becomes the
    // first remaining workspace afterward -- the caller is responsible for
    // reloading DashboardGrid from dashboardFor(activeId()) since this class
    // never touches DashboardGrid itself.
    void removeWorkspace(const QString& id);
    // No-op if `id` is unknown.
    void setActiveId(const QString& id);

    QJsonObject toJson() const;
    // Malformed or empty input resets to a single Default workspace, same
    // as reset() -- covers both a fresh project and an older .tvproj file
    // that predates this section.
    void fromJson(const QJsonObject& object);

    // Back to one "Default" workspace with an empty dashboard, as if the
    // app had just started -- used by New Project and as fromJson()'s
    // fallback.
    void reset();

private:
    WorkspaceManager();

    // Appends `name` (disambiguated) as a fresh workspace with `dashboard`
    // and returns its id, without touching m_activeId.
    QString addWorkspace(const QString& name, const QJsonObject& dashboard);
    QString disambiguate(const QString& name) const;
    int indexOf(const QString& id) const;

    QVector<Workspace> m_workspaces;
    QString m_activeId;
};

} // namespace traceview
