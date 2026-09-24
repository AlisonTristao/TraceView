#include "core/dashboardgallerydialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "project/dashboardgallery.h"
#include "project/projectstore.h"
#include "theme/dialogpresenter.h"

namespace traceview {

namespace {
// Row data: the gallery name, or empty for the built-in example row.
constexpr int kNameRole = Qt::UserRole;
constexpr int kBuiltInRole = Qt::UserRole + 1;
}  // namespace

DashboardGalleryDialog::DashboardGalleryDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Dashboard Gallery"));
    setMinimumSize(360, 320);

    auto* hint =
        new QLabel(tr("Dashboards kept inside TraceView. The default one opens every time the app "
                      "starts; with none chosen, the built-in example opens."),
                   this);
    hint->setWordWrap(true);

    m_list = new QListWidget(this);
    connect(m_list, &QListWidget::currentRowChanged, this, &DashboardGalleryDialog::updateButtons);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DashboardGalleryDialog::onOpenClicked);

    m_openButton = new QPushButton(tr("Open"), this);
    m_defaultButton = new QPushButton(tr("Set as Default"), this);
    m_renameButton = new QPushButton(tr("Rename..."), this);
    m_removeButton = new QPushButton(tr("Delete"), this);
    m_exportButton = new QPushButton(tr("Export..."), this);
    connect(m_openButton, &QPushButton::clicked, this, &DashboardGalleryDialog::onOpenClicked);
    connect(m_defaultButton, &QPushButton::clicked, this,
            &DashboardGalleryDialog::onSetDefaultClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &DashboardGalleryDialog::onRenameClicked);
    connect(m_removeButton, &QPushButton::clicked, this, &DashboardGalleryDialog::onRemoveClicked);
    connect(m_exportButton, &QPushButton::clicked, this, &DashboardGalleryDialog::onExportClicked);

    // Two rows rather than one: five buttons side by side don't fit a phone.
    auto* primaryRow = new QHBoxLayout();
    primaryRow->addWidget(m_openButton);
    primaryRow->addWidget(m_defaultButton);
    primaryRow->addStretch();
    auto* secondaryRow = new QHBoxLayout();
    secondaryRow->addWidget(m_renameButton);
    secondaryRow->addWidget(m_removeButton);
    secondaryRow->addWidget(m_exportButton);
    secondaryRow->addStretch();

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_list);
    layout->addLayout(primaryRow);
    layout->addLayout(secondaryRow);
    layout->addWidget(closeButtons);

    // Start on whatever is open right now, so its row is the one acted on.
    refreshList(DashboardGallery::nameForPath(ProjectStore::instance().currentPath()));
}

void DashboardGalleryDialog::refreshList(const QString& selectName) {
    m_list->clear();
    const QString defaultName = DashboardGallery::defaultName();
    const bool builtInIsDefault = !DashboardGallery::contains(defaultName);

    auto addRow = [this](const QString& text, const QString& name, bool builtIn, bool isDefault) {
        auto* item = new QListWidgetItem(isDefault ? tr("%1 (default)").arg(text) : text, m_list);
        item->setData(kNameRole, name);
        item->setData(kBuiltInRole, builtIn);
        if (isDefault) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        return item;
    };

    QListWidgetItem* toSelect =
        addRow(tr("Built-in example"), QString(), /*builtIn=*/true, builtInIsDefault);
    for (const QString& name : DashboardGallery::names()) {
        QListWidgetItem* item = addRow(name, name, /*builtIn=*/false, name == defaultName);
        if (name == selectName) {
            toSelect = item;
        }
    }
    m_list->setCurrentItem(toSelect);
    updateButtons();
}

void DashboardGalleryDialog::updateButtons() {
    const QListWidgetItem* item = m_list->currentItem();
    const bool any = item != nullptr;
    const bool builtIn = builtInSelected();
    const bool isDefault =
        any && (builtIn ? !DashboardGallery::contains(DashboardGallery::defaultName())
                        : selectedName() == DashboardGallery::defaultName());
    m_openButton->setEnabled(any);
    m_exportButton->setEnabled(any);
    m_defaultButton->setEnabled(any && !isDefault);
    m_renameButton->setEnabled(any && !builtIn);
    m_removeButton->setEnabled(any && !builtIn);
}

QString DashboardGalleryDialog::selectedName() const {
    const QListWidgetItem* item = m_list->currentItem();
    return item != nullptr ? item->data(kNameRole).toString() : QString();
}

bool DashboardGalleryDialog::builtInSelected() const {
    const QListWidgetItem* item = m_list->currentItem();
    return item != nullptr && item->data(kBuiltInRole).toBool();
}

void DashboardGalleryDialog::onOpenClicked() {
    if (m_list->currentItem() == nullptr) {
        return;
    }
    const QString path = builtInSelected() ? DashboardGallery::kBuiltInExamplePath
                                           : DashboardGallery::pathFor(selectedName());
    emit openRequested(path);
    accept();
}

void DashboardGalleryDialog::onSetDefaultClicked() {
    if (m_list->currentItem() == nullptr) {
        return;
    }
    const QString name = builtInSelected() ? QString() : selectedName();
    DashboardGallery::setDefaultName(name);
    refreshList(name);
}

void DashboardGalleryDialog::onRenameClicked() {
    const QString oldName = selectedName();
    if (oldName.isEmpty()) {
        return;
    }
    bool ok = false;
    const QString newName = DialogPresenter::getText(this, tr("Rename Dashboard"), tr("New name:"),
                                                     QLineEdit::Normal, oldName, &ok)
                                .trimmed();
    if (!ok || newName == oldName) {
        return;
    }
    const QString oldPath = DashboardGallery::pathFor(oldName);
    QString error;
    if (!DashboardGallery::rename(oldName, newName, &error)) {
        DialogPresenter::warning(this, tr("Rename Dashboard"), error);
        return;
    }
    // Keep Save writing to the renamed file, not recreating the old one.
    if (ProjectStore::instance().currentPath() == oldPath) {
        ProjectStore::instance().setCurrentPath(DashboardGallery::pathFor(newName));
    }
    refreshList(newName);
}

void DashboardGalleryDialog::onRemoveClicked() {
    const QString name = selectedName();
    if (name.isEmpty()) {
        return;
    }
    if (DialogPresenter::question(
            this, tr("Delete Dashboard"),
            tr("Delete \"%1\" from the gallery? This can't be undone.").arg(name)) !=
        QMessageBox::Yes) {
        return;
    }
    const QString path = DashboardGallery::pathFor(name);
    QString error;
    if (!DashboardGallery::remove(name, &error)) {
        DialogPresenter::warning(this, tr("Delete Dashboard"), error);
        return;
    }
    // The dashboard stays on screen; its next Save asks where to go instead
    // of silently bringing the deleted entry back.
    if (ProjectStore::instance().currentPath() == path) {
        ProjectStore::instance().setCurrentPath({});
    }
    refreshList();
}

void DashboardGalleryDialog::onExportClicked() {
    if (m_list->currentItem() == nullptr) {
        return;
    }
    const bool builtIn = builtInSelected();
    const QString source =
        builtIn ? DashboardGallery::kBuiltInExamplePath : DashboardGallery::pathFor(selectedName());
    const QString suggested = (builtIn ? QStringLiteral("example") : selectedName()) + ".tvproj";
    const QString target = QFileDialog::getSaveFileName(this, tr("Export Dashboard"), suggested,
                                                        tr("TraceView Project (*.tvproj)"));
    if (target.isEmpty()) {
        return;
    }
    // QFile::copy() never overwrites; the save dialog already confirmed that.
    if (QFile::exists(target)) {
        QFile::remove(target);
    }
    if (!QFile::copy(source, target)) {
        DialogPresenter::warning(this, tr("Export Dashboard"),
                                 tr("Couldn't write \"%1\".").arg(target));
        return;
    }
    // A copy out of the Qt resource system comes out read-only.
    QFile::setPermissions(
        target, QFile::permissions(target) | QFileDevice::WriteOwner | QFileDevice::WriteUser);
}

}  // namespace traceview
