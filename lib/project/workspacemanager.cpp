#include "workspacemanager.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QUuid>
#include <algorithm>

namespace traceview {

WorkspaceManager& WorkspaceManager::instance() {
    static WorkspaceManager manager;
    return manager;
}

WorkspaceManager::WorkspaceManager() {
    reset();
}

int WorkspaceManager::indexOf(const QString& id) const {
    for (int i = 0; i < m_workspaces.size(); ++i) {
        if (m_workspaces[i].id == id) {
            return i;
        }
    }
    return -1;
}

QJsonObject WorkspaceManager::dashboardFor(const QString& id) const {
    const int index = indexOf(id);
    return index >= 0 ? m_workspaces[index].dashboard : QJsonObject();
}

void WorkspaceManager::setDashboardFor(const QString& id, const QJsonObject& dashboard) {
    const int index = indexOf(id);
    if (index >= 0) {
        m_workspaces[index].dashboard = dashboard;
    }
}

QString WorkspaceManager::nameFor(const QString& id) const {
    const int index = indexOf(id);
    return index >= 0 ? m_workspaces[index].name : QString();
}

QString WorkspaceManager::disambiguate(const QString& name) const {
    bool collides = false;
    for (const Workspace& workspace : m_workspaces) {
        if (workspace.name == name) {
            collides = true;
            break;
        }
    }
    if (!collides) {
        return name;
    }

    int suffix = 2;
    QString candidate;
    do {
        candidate = QStringLiteral("%1 %2").arg(name).arg(suffix++);
    } while (std::any_of(
        m_workspaces.cbegin(), m_workspaces.cend(),
        [&candidate](const Workspace& workspace) { return workspace.name == candidate; }));
    return candidate;
}

QString WorkspaceManager::addWorkspace(const QString& name, const QJsonObject& dashboard) {
    Workspace workspace;
    workspace.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    workspace.name = disambiguate(name);
    workspace.dashboard = dashboard;
    m_workspaces.append(workspace);
    return workspace.id;
}

QString WorkspaceManager::createWorkspace(const QString& name) {
    const QString id = addWorkspace(name, QJsonObject());
    m_activeId = id;
    return id;
}

void WorkspaceManager::removeWorkspace(const QString& id) {
    if (m_workspaces.size() <= 1) {
        return;
    }
    const int index = indexOf(id);
    if (index < 0) {
        return;
    }

    m_workspaces.remove(index);
    if (m_activeId == id) {
        m_activeId = m_workspaces.first().id;
    }
}

void WorkspaceManager::setActiveId(const QString& id) {
    if (indexOf(id) >= 0) {
        m_activeId = id;
    }
}

QJsonObject WorkspaceManager::toJson() const {
    QJsonArray list;
    for (const Workspace& workspace : m_workspaces) {
        QJsonObject entry;
        entry["id"] = workspace.id;
        entry["name"] = workspace.name;
        entry["dashboard"] = workspace.dashboard;
        list.append(entry);
    }

    QJsonObject object;
    object["activeId"] = m_activeId;
    object["list"] = list;
    return object;
}

void WorkspaceManager::fromJson(const QJsonObject& object) {
    const QJsonArray list = object.value("list").toArray();
    if (list.isEmpty()) {
        reset();
        return;
    }

    QVector<Workspace> workspaces;
    for (const QJsonValue& value : list) {
        const QJsonObject entryObject = value.toObject();
        const QString id = entryObject.value("id").toString();
        if (id.isEmpty()) {
            continue;
        }
        Workspace workspace;
        workspace.id = id;
        workspace.name = entryObject.value("name").toString();
        workspace.dashboard = entryObject.value("dashboard").toObject();
        workspaces.append(workspace);
    }
    if (workspaces.isEmpty()) {
        reset();
        return;
    }

    m_workspaces = workspaces;
    const QString activeId = object.value("activeId").toString();
    m_activeId = indexOf(activeId) >= 0 ? activeId : m_workspaces.first().id;
}

void WorkspaceManager::reset() {
    m_workspaces.clear();
    // WorkspaceManager is not a QObject, so tr() isn't available here; use
    // QCoreApplication::translate() with an explicit context instead, same
    // as ProjectStore's error strings.
    m_activeId =
        addWorkspace(QCoreApplication::translate("WorkspaceManager", "Default"), QJsonObject());
}

}  // namespace traceview
