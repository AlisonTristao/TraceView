#include "ota/otatab.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "ota/otaclient.h"

namespace traceview {

namespace {
constexpr int kNameColumn = 0;
constexpr int kAddressColumn = 1;
constexpr int kStatusColumn = 2;
constexpr int kFirmwareColumn = 3;
constexpr int kPasswordColumn = 4;
constexpr int kUploadColumn = 5;
constexpr int kColumnCount = 6;

// 1 Hz -- there's no manual refresh affordance, so this is the only thing
// that keeps the OTA Online column current; OtaTab::setActive(true)
// additionally fires one poll immediately so switching to the tab doesn't
// sit on stale "Checking..." text for a full second first.
//
// Deliberately shorter than OtaClient's own kStatusTimeoutMs (4s): a tick
// that lands while a row's request is still running is skipped by
// OtaClient::statusRequestInFlight() rather than restarting it, so the fast
// interval only costs anything for rows that answer quickly.
constexpr int kPollIntervalMs = 1000;

// Fixed footprint for the Upload cell's button/progress bar -- both live in
// the same QStackedWidget (see RowWidgets::uploadStack) and swap back and
// forth as an upload starts/finishes. QStackedWidget's own sizeHint() is
// just whichever page is currently showing, not the max of all of them, so
// without an explicit fixed size the column visibly resizes every time the
// cell swaps pages. Sized for the progress bar's "Uploading... 100%" text,
// the widest content either page ever shows.
constexpr int kUploadCellWidth = 120;
constexpr int kUploadCellHeight = 24;
}  // namespace

OtaTab::OtaTab(QWidget* parent) : QWidget(parent) {
    m_client = new OtaClient(this);
    connect(m_client, &OtaClient::statusChecked, this, &OtaTab::onStatusChecked);
    connect(m_client, &OtaClient::uploadProgress, this, &OtaTab::onUploadProgress);
    connect(m_client, &OtaClient::uploadFinished, this, &OtaTab::onUploadFinished);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &OtaTab::refreshNow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("OTA Address"), tr("OTA Online"), tr("Firmware"), tr("Password"), tr("Upload")});
    // Name (the most variable-length field) absorbs whatever space the
    // fixed-width Upload column doesn't need, instead of Upload itself
    // stretching to fill leftover space -- setStretchLastSection(true)
    // (the default convention elsewhere in this app, see LogViewer) would
    // do exactly that here since Upload is the last column, which is the
    // other half of why that cell's width used to wander.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kUploadColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(kUploadColumn, kUploadCellWidth + 12);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // A row is selected by clicking its Name/Address/Status cells (the
    // Password/Upload cells are cell widgets, so a click there is consumed
    // by the QLineEdit/QPushButton instead -- same as clicking a DeviceCard's
    // gear button doesn't also select the card). Drives the shared Remove
    // Device action via selectedDeviceId()/selectionChanged().
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &OtaTab::selectionChanged);
    m_table->setWordWrap(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_table);
}

QString OtaTab::selectedDeviceId() const {
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) return QString();
    const int row = selected.first()->row();
    if (row < 0 || row >= m_devices.size()) return QString();
    return m_devices.at(row).id;
}

void OtaTab::setDevices(const QVector<Device>& devices) {
    // Rebuilt from scratch below, so the table's own selection is lost even
    // when the selected device is still present -- re-applied at the end by
    // row, once the new rows exist.
    const QString previouslySelectedId = selectedDeviceId();

    // Carry session-only state forward for ids that survive the rebuild;
    // drop it for ids that don't (a removed device has nothing left to
    // remember it by).
    QHash<QString, QString> survivingPasswords;
    QHash<QString, bool> survivingUploads;
    for (const Device& device : devices) {
        if (m_sessionPasswords.contains(device.id)) {
            survivingPasswords.insert(device.id, m_sessionPasswords.value(device.id));
        }
        if (m_uploading.value(device.id, false)) {
            survivingUploads.insert(device.id, true);
        }
    }
    m_sessionPasswords = survivingPasswords;
    m_uploading = survivingUploads;

    m_devices = devices;
    m_rowByDeviceId.clear();
    m_rowWidgets.clear();
    m_table->setRowCount(devices.size());

    for (int row = 0; row < devices.size(); ++row) {
        const Device& device = devices.at(row);
        m_rowByDeviceId.insert(device.id, row);

        m_table->setItem(row, kNameColumn, new QTableWidgetItem(device.name));
        m_table->setItem(row, kAddressColumn,
                          new QTableWidgetItem(device.otaAddress.isEmpty() ? tr("(not configured)")
                                                                            : device.otaAddress));

        auto* statusItem = new QTableWidgetItem(device.otaAddress.isEmpty() ? tr("\xE2\x80\x94") : tr("Checking\xE2\x80\xA6"));
        m_table->setItem(row, kStatusColumn, statusItem);

        // The firmware string the robot reports from GET /status
        // (OTAUpdater.cpp's handle_status_get). Worth its own column
        // because it is the one field that says *what* is on the device
        // right now -- without it, deciding whether a .bin is worth
        // uploading means flashing it and seeing.
        m_table->setItem(row, kFirmwareColumn, new QTableWidgetItem(tr("\xE2\x80\x94")));

        RowWidgets widgets;

        auto* passwordContainer = new QWidget(m_table);
        auto* passwordLayout = new QHBoxLayout(passwordContainer);
        passwordLayout->setContentsMargins(4, 0, 4, 0);
        widgets.passwordEdit = new QLineEdit(passwordContainer);
        widgets.passwordEdit->setEchoMode(QLineEdit::Password);
        widgets.passwordEdit->setPlaceholderText(tr("OTA password"));
        const QString initialPassword =
            device.cacheOtaPassword ? device.otaPassword : m_sessionPasswords.value(device.id);
        widgets.passwordEdit->setText(initialPassword);
        widgets.rememberCheck = new QCheckBox(tr("Remember"), passwordContainer);
        widgets.rememberCheck->setChecked(device.cacheOtaPassword);
        widgets.rememberCheck->setToolTip(tr("Save this password in the project file."));
        passwordLayout->addWidget(widgets.passwordEdit, /*stretch=*/1);
        passwordLayout->addWidget(widgets.rememberCheck);
        m_table->setCellWidget(row, kPasswordColumn, passwordContainer);

        const QString deviceId = device.id;
        connect(widgets.passwordEdit, &QLineEdit::textEdited, this,
                [this, deviceId](const QString& text) { m_sessionPasswords.insert(deviceId, text); });
        connect(widgets.rememberCheck, &QCheckBox::toggled, this, [this, deviceId](bool checked) {
            const RowWidgets& rowWidgets = m_rowWidgets.value(deviceId);
            if (rowWidgets.passwordEdit == nullptr) return;
            emit passwordCacheChanged(deviceId, rowWidgets.passwordEdit->text(), checked);
        });

        auto* uploadContainer = new QWidget(m_table);
        auto* uploadLayout = new QHBoxLayout(uploadContainer);
        uploadLayout->setContentsMargins(4, 0, 4, 0);
        widgets.uploadStack = new QStackedWidget(uploadContainer);
        // Fixed, not just sized-to-content: QStackedWidget::sizeHint() is
        // whichever page is current, not the max of every page, so leaving
        // this to size itself makes the whole column visibly jump every
        // time an upload starts/finishes and the stack swaps pages.
        widgets.uploadStack->setFixedSize(kUploadCellWidth, kUploadCellHeight);
        widgets.uploadButton = new QPushButton(tr("Upload\xE2\x80\xA6"), widgets.uploadStack);
        widgets.uploadButton->setEnabled(!device.otaAddress.isEmpty());
        if (device.otaAddress.isEmpty()) {
            widgets.uploadButton->setToolTip(tr("Set this device's OTA address in Device Settings first."));
        }
        widgets.progressBar = new QProgressBar(widgets.uploadStack);
        widgets.progressBar->setRange(0, 100);
        widgets.progressBar->setValue(0);
        widgets.progressBar->setFormat(tr("Uploading\xE2\x80\xA6 %p%"));
        widgets.uploadStack->addWidget(widgets.uploadButton);
        widgets.uploadStack->addWidget(widgets.progressBar);
        widgets.uploadStack->setCurrentWidget(widgets.uploadButton);
        uploadLayout->addWidget(widgets.uploadStack);
        m_table->setCellWidget(row, kUploadColumn, uploadContainer);

        connect(widgets.uploadButton, &QPushButton::clicked, this, [this, deviceId]() { startUpload(deviceId); });

        m_rowWidgets.insert(device.id, widgets);

        if (m_uploading.value(device.id, false)) {
            widgets.uploadStack->setCurrentWidget(widgets.progressBar);
            widgets.uploadButton->setEnabled(false);
            widgets.passwordEdit->setEnabled(false);
        }
    }

    if (!previouslySelectedId.isEmpty()) {
        const int row = m_rowByDeviceId.value(previouslySelectedId, -1);
        if (row >= 0) {
            m_table->selectRow(row);
        }
    }

    m_table->resizeColumnsToContents();
    // resizeColumnsToContents() above can override a Fixed section's width
    // with its own content-based measurement (Qt doesn't exempt Fixed
    // sections from that call) -- reassert it so the Upload column stays
    // exactly kUploadCellWidth regardless.
    m_table->horizontalHeader()->resizeSection(kUploadColumn, kUploadCellWidth + 12);
}

void OtaTab::setActive(bool active) {
    if (active) {
        refreshNow();
        m_pollTimer->start();
    } else {
        m_pollTimer->stop();
    }
}

void OtaTab::refreshNow() {
    for (const Device& device : m_devices) {
        if (device.otaAddress.isEmpty() || m_uploading.value(device.id, false)) {
            continue;
        }
        // A row whose previous request hasn't answered yet is left alone.
        // OtaClient coalesces this anyway; checking here as well keeps the
        // "one poll, one request" reading of this loop honest.
        if (m_client->statusRequestInFlight(device.id)) {
            continue;
        }
        m_client->checkStatus(device.id, device.otaAddress);
    }
}

void OtaTab::onStatusChecked(const QString& deviceId, bool reachable, bool otaReady,
                              const QString& firmwareVersion, const QString& errorMessage) {
    const int row = m_rowByDeviceId.value(deviceId, -1);
    if (row < 0) return;  // device removed since the request was sent

    if (QTableWidgetItem* firmwareItem = m_table->item(row, kFirmwareColumn)) {
        // Blanked back to the em dash when unreachable rather than left
        // showing the last version seen: this column answers "what is on
        // the device", and a device nothing can reach right now has no
        // answer to that.
        firmwareItem->setText(reachable && !firmwareVersion.isEmpty() ? firmwareVersion : tr("\xE2\x80\x94"));
    }

    QTableWidgetItem* item = m_table->item(row, kStatusColumn);
    if (item == nullptr) return;

    if (!reachable) {
        item->setText(tr("Offline"));
        item->setForeground(QColor(Qt::gray));
        // "Offline" alone doesn't say why -- a name that failed to resolve
        // ("Host not found", the common case for a *.local mDNS name on
        // Windows without Bonjour/iTunes installed) looks identical to a
        // robot that's simply not in OTA mode yet unless this is visible
        // somewhere. Hover the cell to see it.
        item->setToolTip(errorMessage.isEmpty() ? tr("Not reachable") : errorMessage);
    } else if (!otaReady) {
        item->setText(tr("Online (busy)"));
        item->setForeground(QColor(Qt::darkYellow));
        item->setToolTip(tr("A firmware write is already in progress on the robot."));
    } else {
        item->setText(tr("Online"));
        item->setForeground(QColor(Qt::darkGreen));
        item->setToolTip(QString());
    }
}

void OtaTab::onUploadProgress(const QString& deviceId, qint64 sent, qint64 total) {
    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    if (widgets.progressBar == nullptr) return;

    if (total > 0) {
        widgets.progressBar->setRange(0, 100);
        widgets.progressBar->setValue(static_cast<int>((sent * 100) / total));
    } else {
        widgets.progressBar->setRange(0, 0);  // indeterminate until the server reports a total
    }
}

void OtaTab::onUploadFinished(const QString& deviceId, bool success, const QString& message) {
    m_uploading.remove(deviceId);

    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    if (widgets.uploadButton == nullptr) return;

    widgets.uploadButton->setEnabled(true);
    widgets.passwordEdit->setEnabled(true);
    widgets.uploadButton->setToolTip(message);
    widgets.uploadStack->setCurrentWidget(widgets.uploadButton);

    if (success && widgets.rememberCheck->isChecked()) {
        emit passwordCacheChanged(deviceId, widgets.passwordEdit->text(), true);
    }

    if (!success) {
        QMessageBox::warning(this, tr("Firmware Upload"), message);
    }
}

void OtaTab::startUpload(const QString& deviceId) {
    const int row = m_rowByDeviceId.value(deviceId, -1);
    if (row < 0) return;
    const Device& device = m_devices.at(row);

    if (device.otaAddress.isEmpty()) {
        QMessageBox::warning(this, tr("Firmware Upload"),
                              tr("Set this device's OTA address in Device Settings first."));
        return;
    }

    const QString filePath =
        QFileDialog::getOpenFileName(this, tr("Select Firmware"), QString(), tr("Firmware Binary (*.bin);;All Files (*)"));
    if (filePath.isEmpty()) return;

    RowWidgets& widgets = m_rowWidgets[deviceId];
    widgets.uploadButton->setEnabled(false);
    widgets.passwordEdit->setEnabled(false);
    widgets.progressBar->setRange(0, 100);
    widgets.progressBar->setValue(0);
    widgets.uploadStack->setCurrentWidget(widgets.progressBar);

    m_uploading.insert(deviceId, true);
    m_client->uploadFirmware(deviceId, device.otaAddress, widgets.passwordEdit->text(), filePath);
}

}  // namespace traceview
