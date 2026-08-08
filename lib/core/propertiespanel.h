#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

#include "dashboard/widgetregistry.h"

class QComboBox;
class QLineEdit;

namespace traceview {

// Fixed width the panel is shown at, embedded to the right of the
// dashboard canvas (see MainWindow) — narrow enough to not eat into the
// canvas, wide enough for the combo/line-edit fields and their labels.
inline constexpr int kPropertiesPanelWidth = 220;

// Editable properties of the currently selected dashboard widget: type,
// display name, and a user-defined key (a future handle for binding
// external data to this specific widget instance). Dumb like Ribbon —
// MainWindow feeds it state via setAvailableTypes()/setSelection() and
// reacts to the *ChangeRequested signals by calling into DashboardGrid;
// this widget never touches DashboardGrid directly.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    void setAvailableTypes(const QVector<WidgetTypeInfo>& types);

    // Populates the fields from the current selection; hasSelection=false
    // clears and disables them. Never emits the request signals below —
    // this is a one-way sync from DashboardGrid state into the UI.
    void setSelection(bool hasSelection, const QString& typeId, const QString& name, const QString& key);

signals:
    void typeChangeRequested(const QString& typeId);
    void nameChangeRequested(const QString& name);
    void keyChangeRequested(const QString& key);

private:
    void onTypeActivated(int index);
    void onNameEditingFinished();
    void onKeyEditingFinished();

    QVector<WidgetTypeInfo> m_types;
    // Last values pushed by setSelection(), used to tell an actual edit
    // apart from editingFinished() firing on plain focus loss.
    QString m_currentTypeId;
    QString m_currentName;
    QString m_currentKey;

    QComboBox* m_typeCombo = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_keyEdit = nullptr;
};

} // namespace traceview
