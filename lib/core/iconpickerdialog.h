#pragma once

#include <QDialog>
#include <QString>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListView;

namespace traceview {

class IconListModel;

// Searchable grid over IconLibrary (the vendored Lucide set) for picking a
// workspace button's icon. Search matches the icon's name and Lucide's own
// tags (English). Double-click or OK accepts; selectedId() is then the
// chosen "lucide:<name>" id. Opened through DialogPresenter::exec(...,
// Style::Page), so on Android it covers the screen like the other big
// dialogs.
class IconPickerDialog : public QDialog {
    Q_OBJECT

public:
    // `currentId` starts selected and scrolled into view when known.
    explicit IconPickerDialog(const QString& currentId, QWidget* parent = nullptr);

    QString selectedId() const;

private:
    void onFilterChanged(const QString& text);
    void onSelectionChanged();
    void select(const QString& id);

    IconListModel* m_model;
    QLineEdit* m_search;
    QListView* m_view;
    QLabel* m_selectedLabel;
    QDialogButtonBox* m_buttons;
    QString m_selectedId;
};

}  // namespace traceview
