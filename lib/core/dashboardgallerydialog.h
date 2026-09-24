#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;

namespace traceview {

// Developer-only management of the dashboards kept inside the app (see
// project/dashboardgallery.h): open, set as the startup dashboard, rename,
// delete, export as a .tvproj. Opened from File > Dashboard Gallery. The
// built-in example is always listed first and can only be opened, exported
// or chosen as the default.
//
// Adding an entry isn't done here -- that needs the live dashboard state,
// so it's MainWindow's File > Add to Gallery.
class DashboardGalleryDialog : public QDialog {
    Q_OBJECT

public:
    explicit DashboardGalleryDialog(QWidget* parent = nullptr);

signals:
    // A .tvproj path (gallery entry or the built-in example); the dialog
    // closes right after emitting it.
    void openRequested(const QString& path);

private:
    void refreshList(const QString& selectName = {});
    void updateButtons();
    // Empty for the built-in example row, or with nothing selected.
    QString selectedName() const;
    bool builtInSelected() const;

    void onOpenClicked();
    void onSetDefaultClicked();
    void onRenameClicked();
    void onRemoveClicked();
    void onExportClicked();

    QListWidget* m_list = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_defaultButton = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_exportButton = nullptr;
};

}  // namespace traceview
