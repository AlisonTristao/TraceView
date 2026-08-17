#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QMenu;
class QToolButton;

namespace traceview {

// The workspace switcher: a single button (current workspace's name) that
// opens a menu listing every workspace, each with its own delete
// affordance, plus a "New Workspace..." entry. Lives as a permanent widget
// in the status bar (bottom-right, see MainWindow::buildWorkspaceSwitcher())
// rather than in the ribbon: the ribbon's tab row hides during fullscreen
// (Ribbon::setTabBarVisible(false)) and the status bar doesn't, so anchoring
// it there keeps it reachable regardless of fullscreen state. Dumb like
// Ribbon/LayersPanel/PropertiesPanel -- it owns no app/project state and
// shows no dialogs itself; MainWindow feeds it the current list via
// setWorkspaces() and reacts to its signals (including the
// confirm-before-delete prompt and the new-workspace name prompt, both
// QMessageBox/QInputDialog calls that live in MainWindow, same as
// onNewProject's discard confirmation).
class WorkspaceSwitcher : public QWidget {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString name;
    };

    explicit WorkspaceSwitcher(QWidget* parent = nullptr);

    // Rebuilds the menu from `entries`; `activeId` picks which one the
    // button label reflects and which row is marked current. A row's
    // delete button is hidden when `entries` has only one element -- the
    // last workspace can't be deleted.
    void setWorkspaces(const QVector<Entry>& entries, const QString& activeId);

    // Re-themes the button/menu icons -- called from MainWindow::
    // updateRibbonIcons() alongside every other ribbon icon.
    void updateIcons(const QColor& color);

signals:
    void workspaceSelected(const QString& id);
    void workspaceDeleteRequested(const QString& id);
    void newWorkspaceRequested();

private:
    void rebuildMenu();

    QToolButton* m_button;
    QMenu* m_menu;
    QVector<Entry> m_entries;
    QString m_activeId;
    QColor m_iconColor;
};

} // namespace traceview
