#include "iconpickerdialog.h"

#include <QAbstractListModel>
#include <QDialogButtonBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/iconlibrary.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kPickerIconSize = 28;
// Roomy enough to be a comfortable touch target on a phone, where this
// opens as a full-screen page.
constexpr int kPickerCellSize = 52;

}  // namespace

// Lazily-rendered list of IconLibrary ids: decoration is only produced for
// the rows the view actually paints, so opening the picker never rasterizes
// all ~2000 glyphs up front (IconLibrary caches what does get drawn).
class IconListModel : public QAbstractListModel {
public:
    explicit IconListModel(QObject* parent)
        : QAbstractListModel(parent), m_allIds(IconLibrary::instance().ids()), m_ids(m_allIds) {}

    int rowCount(const QModelIndex& parent) const override {
        return parent.isValid() ? 0 : int(m_ids.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= m_ids.size()) {
            return {};
        }
        const QString& id = m_ids[index.row()];
        switch (role) {
            case Qt::DecorationRole:
                return IconLibrary::instance().icon(
                    id, ThemeManager::instance().currentTheme().textPrimary, kPickerIconSize);
            case Qt::ToolTipRole:
                return IconLibrary::instance().displayName(id);
            case Qt::UserRole:
                return id;
            default:
                return {};
        }
    }

    void setFilter(const QString& query) {
        beginResetModel();
        if (query.trimmed().isEmpty()) {
            m_ids = m_allIds;
        } else {
            m_ids.clear();
            for (const QString& id : m_allIds) {
                if (IconLibrary::instance().matches(id, query)) {
                    m_ids.append(id);
                }
            }
        }
        endResetModel();
    }

    int rowOf(const QString& id) const {
        return int(m_ids.indexOf(id));
    }

private:
    const QStringList m_allIds;
    QStringList m_ids;
};

IconPickerDialog::IconPickerDialog(const QString& currentId, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Choose Icon"));
    resize(560, 480);

    auto* layout = new QVBoxLayout(this);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search icons (English names and tags)"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_model = new IconListModel(this);
    m_view = new QListView(this);
    m_view->setViewMode(QListView::IconMode);
    m_view->setMovement(QListView::Static);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setUniformItemSizes(true);
    m_view->setLayoutMode(QListView::Batched);
    m_view->setIconSize(QSize(kPickerIconSize, kPickerIconSize));
    m_view->setGridSize(QSize(kPickerCellSize, kPickerCellSize));
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setModel(m_model);
    layout->addWidget(m_view, /*stretch=*/1);

    m_selectedLabel = new QLabel(this);
    layout->addWidget(m_selectedLabel);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);

    connect(m_search, &QLineEdit::textChanged, this, &IconPickerDialog::onFilterChanged);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &IconPickerDialog::onSelectionChanged);
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            accept();
        }
    });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_selectedId = currentId;
    select(currentId);
    onSelectionChanged();
    m_search->setFocus();
}

QString IconPickerDialog::selectedId() const {
    return m_selectedId;
}

void IconPickerDialog::onFilterChanged(const QString& text) {
    m_model->setFilter(text);
    // The reset dropped the selection; restore it if the pick still matches
    // so filtering never silently changes what OK would accept.
    select(m_selectedId);
    onSelectionChanged();
}

void IconPickerDialog::onSelectionChanged() {
    const QModelIndexList selected = m_view->selectionModel()->selectedIndexes();
    if (!selected.isEmpty()) {
        m_selectedId = selected.first().data(Qt::UserRole).toString();
    }
    const bool known = IconLibrary::instance().contains(m_selectedId);
    m_selectedLabel->setText(known ? IconLibrary::instance().displayName(m_selectedId)
                                   : tr("No icon selected"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(known);
}

void IconPickerDialog::select(const QString& id) {
    const int row = m_model->rowOf(id);
    if (row < 0) {
        return;
    }
    const QModelIndex index = m_model->index(row);
    m_view->setCurrentIndex(index);
    m_view->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

}  // namespace traceview
