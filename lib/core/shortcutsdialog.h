#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

namespace traceview {

// Read-only reference of the app's keyboard shortcuts, grouped into sections.
// The caller (MainWindow::onShowKeyboardShortcuts) supplies the rows -- most
// pulled straight off the live QActions so this can't drift from what's
// actually bound. Needs Q_OBJECT so tr() resolves against this class's own
// translation context (same reasoning as AboutDialog).
class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    struct Row {
        QString action;
        QString shortcut;
    };
    struct Section {
        QString title;
        QVector<Row> rows;
    };

    explicit ShortcutsDialog(const QVector<Section>& sections, QWidget* parent = nullptr);
};

}  // namespace traceview
