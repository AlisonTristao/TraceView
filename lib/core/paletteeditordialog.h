#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

#include "traceview/theme.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace traceview {

// Edits a custom palette (View > Palette > New Palette... / Edit
// Palette...): one color button per ThemePalette token plus the six data
// series colors, previewed live on the whole app as they change, with a
// contrast warning when text would be hard to read. Save keeps it; Cancel
// restores exactly what was there before (the previous palette for a new
// one, the original colors for an edited one). See "Custom palettes" in
// docs/THEMING.md.
class PaletteEditorDialog : public QDialog {
    Q_OBJECT

public:
    PaletteEditorDialog(const ThemePalette& palette, bool isNew, QWidget* parent = nullptr);

    void accept() override;
    void reject() override;

private:
    // Applies m_palette app-wide as a saved custom palette -- the live
    // preview.
    void preview();
    void refreshButtons();
    void refreshContrast();
    void pickColor(const QString& token);
    QColor colorOf(const QString& token) const;
    void setColorOf(const QString& token, const QColor& color);

    ThemePalette m_palette;
    const ThemePalette m_original;
    const QString m_previousThemeId;
    const bool m_isNew;
    bool m_done = false;

    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_contrastLabel = nullptr;
    QHash<QString, QPushButton*> m_buttons;  // token -> its color button
};

}  // namespace traceview
