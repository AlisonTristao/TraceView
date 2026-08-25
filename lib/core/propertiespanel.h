#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgetregistry.h"
#include "dockablepanel.h"

class QComboBox;
class QFrame;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace traceview {

// Fixed width the panel is docked at by default, left/right (see
// LayersPanel/PropertiesPanel::preferredThickness()) -- wide enough for the
// combo/line-edit fields and their labels, and for a WidgetConfigEditor's
// table-shaped content (see the divider/scroll area below) without eating
// too much into the canvas.
inline constexpr int kPropertiesPanelWidth = 380;

// Editable properties of the currently selected dashboard widget. Two
// sections, stacked vertically:
// - Common fields (type, display name, user-defined key) identical for
//   every widget kind, in a QFormLayout at the top — unchanged behavior.
// - Below a divider, the selected type's own settings: whichever
//   WidgetConfigEditor its WidgetTypeInfo::configEditorFactory builds (see
//   dashboard/widgetregistry.h), hosted in a QScrollArea so a type with a
//   lot of settings (e.g. a chart's series table) can grow without pushing
//   the common fields off-panel. Types with no factory registered just
//   leave that area empty.
//
// Dumb like Ribbon — MainWindow feeds it state via
// setAvailableTypes()/setSelection() and reacts to the *ChangeRequested
// signals by calling into DashboardGrid; this widget never touches
// DashboardGrid directly. Header/pin toggle/drag-to-dock behavior lives in
// DockablePanel, this class only owns the fields below it.
class PropertiesPanel : public DockablePanel {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    void setAvailableTypes(const QVector<WidgetTypeInfo>& types);
    // Pushed to whichever WidgetConfigEditor is currently hosted (if any),
    // and remembered for the next one ensureConfigEditor() builds -- see
    // WidgetConfigEditor::setAvailableDevices().
    void setAvailableDevices(const QVector<DeviceOption>& devices);

    // Populates the fields (and swaps in the right config editor, if any)
    // from the current selection; hasSelection=false clears and disables
    // them. Never emits the request signals below — this is a one-way sync
    // from DashboardGrid state into the UI.
    void setSelection(bool hasSelection, const QString& typeId, const QString& name,
                      const QString& key, const QJsonObject& config);

    int preferredThickness() const override {
        return kPropertiesPanelWidth;
    }

signals:
    void typeChangeRequested(const QString& typeId);
    void nameChangeRequested(const QString& name);
    void keyChangeRequested(const QString& key);
    void configChangeRequested(const QJsonObject& config);

private:
    void onTypeActivated(int index);
    void onNameEditingFinished();
    void onKeyEditingFinished();
    void onConfigEditorChanged();

    // Swaps m_configEditor for whatever `typeId` registers (or none), if it
    // isn't already showing that type's editor. Called with an empty
    // typeId to clear it (no selection, or the type has no config).
    void ensureConfigEditor(const QString& typeId);

    QVector<WidgetTypeInfo> m_types;
    QVector<DeviceOption> m_availableDevices;
    // Last values pushed by setSelection(), used to tell an actual edit
    // apart from editingFinished()/configChanged() firing on plain sync.
    QString m_currentTypeId;
    QString m_currentName;
    QString m_currentKey;
    QJsonObject m_currentConfig;

    // Everything setSelection() enables/disables based on hasSelection --
    // kept separate from `this` so the pin button in the header stays
    // clickable even with no selection.
    QWidget* m_content = nullptr;

    QComboBox* m_typeCombo = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_keyEdit = nullptr;

    QFrame* m_divider = nullptr;
    QScrollArea* m_configScrollArea = nullptr;
    QWidget* m_configContainer = nullptr;
    QVBoxLayout* m_configLayout = nullptr;
    // Type the currently-hosted editor was built for; empty means none is
    // hosted. Tracked separately from m_currentTypeId so a config-only
    // change (no type change) doesn't tear down and rebuild the editor.
    QString m_configEditorTypeId;
    WidgetConfigEditor* m_configEditor = nullptr;
};

}  // namespace traceview
