#pragma once

#include <QFont>
#include <QObject>
#include <QVector>

#include "traceview/font.h"

namespace traceview {

// Owns the set of available FontOptions, tracks which one is active, and
// applies it to the running QApplication's font. Persists the selection
// across launches via QSettings. Deliberately independent of ThemeManager --
// font family and color palette are orthogonal choices, so any font pairs
// with any theme instead of being bundled per-palette.
class FontManager : public QObject {
    Q_OBJECT

public:
    static FontManager& instance();

    const FontOption& currentFont() const;
    QVector<FontOption> availableFonts() const;

    // Adds a new selectable font. No-op if `font.id` is already registered.
    void registerFont(const FontOption& font);

    void setFont(const QString& id);

    // Re-applies the current font selection to QApplication. Call once at
    // startup, after QApplication exists and before the first window shows.
    void applyCurrentFont();

signals:
    void fontChanged(const FontOption& font);

private:
    FontManager();

    int indexOf(const QString& id) const;

    QVector<FontOption> m_fonts;
    int m_currentIndex = 0;

    // Captured from QApplication at construction time, before any override
    // is ever applied -- lets "System Default" restore exactly what Qt chose
    // for this platform instead of hardcoding a guess like "Segoe UI" that
    // would be wrong off Windows. Also used as the base (size/weight) for
    // every other option, since only the family should change (TAREFA 0 in
    // TODO_VISUAL_IDENTITY.txt already decided default size everywhere).
    QFont m_baseFont;
};

} // namespace traceview
