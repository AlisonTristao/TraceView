#include "shortcutsdialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace traceview {

ShortcutsDialog::ShortcutsDialog(const QVector<Section>& sections, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    setMinimumSize(440, 500);

    auto* tree = new QTreeWidget(this);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Action"), tr("Shortcut")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setFocusPolicy(Qt::NoFocus);
    tree->setUniformRowHeights(true);

    for (const Section& section : sections) {
        auto* header = new QTreeWidgetItem(tree);
        header->setFirstColumnSpanned(true);
        header->setText(0, section.title);
        header->setFlags(Qt::ItemIsEnabled);
        QFont headerFont = header->font(0);
        headerFont.setBold(true);
        header->setFont(0, headerFont);

        for (const Row& row : section.rows) {
            auto* item = new QTreeWidgetItem(header);
            item->setText(0, row.action);
            item->setText(1, row.shortcut);
            item->setFlags(Qt::ItemIsEnabled);
        }
    }

    tree->expandAll();
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tree);
    layout->addWidget(buttons);
}

}  // namespace traceview
