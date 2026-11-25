#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class QButtonGroup;
class QHBoxLayout;
class QScrollArea;

namespace traceview {

// The workspace strip: one row of chips (icon + name) directly above the
// dashboard canvas, one chip per workspace, the active one highlighted with
// the accent colour. MainWindow shows it only while the Dashboard tab is
// current -- workspaces are a dashboard concern -- on every screen size and
// in fullscreen. The row scrolls horizontally (no scrollbar; a finger drag
// scrolls it through the app-wide touch scrolling, see touchscroll.h) and
// the active chip is kept in view.
//
// Tap switches workspace. Right-click or long-press on a chip opens a small
// menu headed by the workspace's name, plus Rename.../Change Icon.../Delete
// while management is enabled; a trailing "+" chip creates a workspace.
// Dumb like Ribbon/LayersPanel: MainWindow feeds it entries and handles its
// signals (confirm-before-delete, name prompt and icon picker live there).
class WorkspaceStrip : public QWidget {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        QString name;
        bool builtIn = false;
        // IconLibrary id, already resolved by MainWindow (a workspace with
        // no pick gets the default).
        QString icon;
    };

    explicit WorkspaceStrip(QWidget* parent = nullptr);

    // Rebuilds the chips from `entries`; `activeId` picks the checked one.
    // The last editable workspace can't be deleted.
    void setWorkspaces(const QVector<Entry>& entries, const QString& activeId);
    // Hides "+" and the menus' management actions while false -- switching
    // between existing workspaces stays available either way, only creating/
    // renaming/re-iconing/deleting is gated (Developer mode).
    void setManagementEnabled(bool enabled);
    // `color` for idle glyphs, `checkedColor` for the active chip's.
    void updateIcons(const QColor& color, const QColor& checkedColor);

signals:
    void workspaceSelected(const QString& id);
    void workspaceDeleteRequested(const QString& id);
    void iconChangeRequested(const QString& id);
    void renameRequested(const QString& id);
    void newWorkspaceRequested();

private:
    void rebuild();
    void showEntryMenu(const Entry& entry, QWidget* anchor, bool deletable);
    // Emits on the next event-loop turn: every handler ends back in
    // rebuild() via MainWindow, which deletes the very chip whose click is
    // still on the stack.
    void emitDeferred(std::function<void()> emitter);

    QScrollArea* m_scroll;
    QWidget* m_body;
    QHBoxLayout* m_layout;
    QButtonGroup* m_group;
    QVector<Entry> m_entries;
    QString m_activeId;
    QColor m_iconColor;
    QColor m_checkedIconColor;
    bool m_managementEnabled = true;
};

}  // namespace traceview
