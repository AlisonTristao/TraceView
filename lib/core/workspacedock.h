#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

#include "workspaceswitcher.h"

class QButtonGroup;
class QHBoxLayout;

namespace traceview {

// The bottom bar in compact chrome (Android, or a Developer-mode Small/
// Medium preview -- see MainWindow::compactChromeActive()): replaces
// m_statusRow there and gives the whole bar to workspace navigation. One
// icon-only button per workspace (each Entry::icon, an IconLibrary id),
// spaced evenly and centered, on a bar taller than the status row (see
// kDockScale in the .cpp) so they are comfortable touch targets. The active workspace's button is checked.
//
// Tap switches workspace. Long-press (right-click on desktop) opens a small
// menu headed by the workspace's name -- the only place an icon-only bar
// can show it -- plus Rename.../Change Icon.../Delete while management is enabled,
// and a trailing "+" button creates a workspace. Dumb like
// WorkspaceSwitcher: MainWindow feeds it entries and handles its signals
// (the same ones).
class WorkspaceDock : public QWidget {
    Q_OBJECT

public:
    explicit WorkspaceDock(QWidget* parent = nullptr);

    void setWorkspaces(const QVector<WorkspaceSwitcher::Entry>& entries, const QString& activeId);
    // Same gate as WorkspaceSwitcher::setManagementEnabled(): switching is
    // always allowed, creating/deleting/re-iconing is Developer-only.
    void setManagementEnabled(bool enabled);
    // `color` for idle glyphs, `checkedColor` for the active one -- the dock
    // has no checked fill (see stylesheet.cpp), so the tint marks it.
    void updateIcons(const QColor& color, const QColor& checkedColor);

signals:
    void workspaceSelected(const QString& id);
    void workspaceDeleteRequested(const QString& id);
    void iconChangeRequested(const QString& id);
    void renameRequested(const QString& id);
    void newWorkspaceRequested();

private:
    void rebuild();
    void showEntryMenu(const WorkspaceSwitcher::Entry& entry, QWidget* anchor, bool deletable);
    // Emits on the next event-loop turn: every handler ends back in
    // rebuild() via MainWindow, which deletes the very button whose click
    // is still on the stack (same hazard WorkspaceSwitcher::rebuildMenu()
    // documents).
    void emitDeferred(std::function<void()> emitter);

    QHBoxLayout* m_layout;
    QButtonGroup* m_group;
    QVector<WorkspaceSwitcher::Entry> m_entries;
    QString m_activeId;
    QColor m_iconColor;
    QColor m_checkedIconColor;
    bool m_managementEnabled = true;
};

}  // namespace traceview
