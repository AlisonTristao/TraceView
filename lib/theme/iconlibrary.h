#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>

namespace traceview {

// The user-pickable icon set (workspace buttons, see IconPickerDialog) --
// Lucide (https://lucide.dev, ISC), vendored by tools/import_lucide.py into a
// single resources/icons/lucide/lucide.json. Distinct from the app's own
// chrome glyphs (resources/icons/ribbon etc., drawn to TraceView's 16px
// grid): those are fixed per button, these are chosen by the user and saved
// into the project by id.
//
// Ids are "lucide:<name>" (e.g. "lucide:gauge") so a project file keeps
// working if another set is ever added beside this one. An id this build
// doesn't know -- renamed/removed upstream, or a newer project opened in an
// older build -- is simply !contains(), and callers fall back to a default.
//
// A process-wide singleton, same shape as ThemeManager/WorkspaceManager.
// The JSON is parsed on first use, not at startup: nothing needs it until a
// workspace button is drawn.
class IconLibrary {
public:
    static IconLibrary& instance();

    // Every id, sorted by name.
    QStringList ids() const;
    bool contains(const QString& id) const;
    // "arrow-up-right" -> "arrow up right"; empty for an unknown id.
    QString displayName(const QString& id) const;
    // True when every whitespace-separated word of `query` is a substring
    // (case-insensitive) of the icon's name or one of its tags. An empty
    // query matches everything.
    bool matches(const QString& id, const QString& query) const;

    // Tinted to `color` like every other UI glyph (see iconutils.h); a null
    // QIcon for an unknown id. Rendered pixmaps go through QPixmapCache, so
    // redrawing the same icon (theme refresh, picker scrolling) is cheap
    // while the picker's ~2000 glyphs can't pin memory forever.
    QIcon icon(const QString& id, const QColor& color, int size) const;
    // Same, with a second color for QIcon::On -- buttons have no checked
    // fill (see stylesheet.cpp), so a checkable button marks its active
    // state by tinting the glyph (e.g. accent) instead.
    QIcon icon(const QString& id, const QColor& color, const QColor& checkedColor, int size) const;

private:
    struct Entry {
        QString name;
        QStringList tags;
        QByteArray body;
    };

    IconLibrary() = default;
    void ensureLoaded() const;
    const Entry* find(const QString& id) const;
    QPixmap pixmap(const Entry& entry, const QColor& color, int size) const;

    mutable bool m_loaded = false;
    mutable QVector<Entry> m_entries;
    mutable QHash<QString, int> m_indexByName;
};

}  // namespace traceview
