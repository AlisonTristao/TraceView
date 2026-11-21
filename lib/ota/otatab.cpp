#include "ota/otatab.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "theme/dialogpresenter.h"

namespace traceview {

namespace {
constexpr int kNameColumn = 0;
constexpr int kAddressColumn = 1;
constexpr int kStateColumn = 2;
constexpr int kAppColumn = 3;
constexpr int kRecoveryColumn = 4;
constexpr int kActionColumn = 5;
constexpr int kColumnCount = 6;

// Versions arrive after the first resizeColumnsToContents() pass, which only
// saw an em dash: reserve room for a typical "project 1.2.3".
constexpr int kAppColumnWidth = 200;

// 1 Hz, shorter than OtaClient's 4 s status timeout on purpose: a tick that
// lands while a row's request is still running is skipped (coalesced), so
// the fast interval only costs anything for rows that answer quickly.
constexpr int kPollIntervalMs = 1000;

// The Action cell holds the upload button/progress bar (swapped in one
// QStackedWidget, fixed size so the column does not jump) and "To app".
constexpr int kUploadCellWidth = 130;
constexpr int kCellHeight = 24;

const QString kDash = QStringLiteral("—");

QString reportedValue(const Device& device, const char* key) {
    for (const DeviceInfoRecord& entry : device.reportedInfo) {
        if (entry.key == QLatin1String(key)) {
            return entry.value;
        }
    }
    return QString();
}

QString reasonText(const QString& reason) {
    if (reason == QLatin1String("command")) return OtaTab::tr("asked for (btp -recovery)");
    if (reason == QLatin1String("crash_loop")) return OtaTab::tr("the application kept crashing");
    if (reason.startsWith(QLatin1String("factory_reset"))) return OtaTab::tr("factory reset");
    return reason.isEmpty() || reason == QLatin1String("none")
               ? OtaTab::tr("no valid application, or the boot chose it")
               : reason;
}
}  // namespace

OtaTab::OtaTab(QWidget* parent) : QWidget(parent) {
    m_client = new OtaClient(this);
    connect(m_client, &OtaClient::statusChecked, this, &OtaTab::onStatusChecked);
    connect(m_client, &OtaClient::uploadProgress, this, &OtaTab::onUploadProgress);
    connect(m_client, &OtaClient::uploadFinished, this, &OtaTab::onUploadFinished);
    connect(m_client, &OtaClient::rebootFinished, this, &OtaTab::onRebootFinished);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &OtaTab::refreshNow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Address"), tr("State"), tr("Application"), tr("Recovery"), QString()});
    // Name absorbs the spare width; the fixed Action column must not.
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &OtaTab::selectionChanged);
    m_table->setWordWrap(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_table);
}

void OtaTab::setKeyProvider(std::function<QByteArray(const QString&)> provider) {
    m_keyProvider = std::move(provider);
}

QString OtaTab::selectedDeviceId() const {
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) return QString();
    const int row = selected.first()->row();
    if (row < 0 || row >= m_devices.size()) return QString();
    return m_devices.at(row).id;
}

const Device* OtaTab::deviceById(const QString& deviceId) const {
    const int row = m_rowByDeviceId.value(deviceId, -1);
    return row >= 0 && row < m_devices.size() ? &m_devices.at(row) : nullptr;
}

void OtaTab::setDevices(const QVector<Device>& devices) {
    const QString previouslySelectedId = selectedDeviceId();

    // Session state survives for ids still present; a removed device has
    // nothing left to remember it by.
    QSet<QString> ids;
    for (const Device& device : devices) ids.insert(device.id);
    auto keepPresent = [&ids](auto& hash) {
        for (auto it = hash.begin(); it != hash.end();) {
            it = ids.contains(it.key()) ? std::next(it) : hash.erase(it);
        }
    };
    keepPresent(m_sessionPasswords);
    keepPresent(m_inRecovery);
    keepPresent(m_uploading);
    keepPresent(m_waiting);

    m_devices = devices;
    m_rowByDeviceId.clear();
    m_rowWidgets.clear();
    m_table->setRowCount(devices.size());

    for (int row = 0; row < devices.size(); ++row) {
        const Device& device = devices.at(row);
        const QString deviceId = device.id;
        m_rowByDeviceId.insert(deviceId, row);
        const QString address = otaAddressFor(device);

        m_table->setItem(row, kNameColumn, new QTableWidgetItem(device.name));
        auto* addressItem = new QTableWidgetItem(address.isEmpty() ? tr("(none)") : address);
        addressItem->setToolTip(
            address.isEmpty()
                ? tr("Connect the device over TCP (or once, so it reports its address), or set "
                     "its OTA address in Device Settings.")
                : device.otaAddress.trimmed().isEmpty()
                      ? tr("From the device itself (its TCP host or the address it reported).")
                      : tr("Set in Device Settings."));
        m_table->setItem(row, kAddressColumn, addressItem);
        m_table->setItem(row, kStateColumn,
                         new QTableWidgetItem(address.isEmpty() ? kDash : tr("Checking\xE2\x80\xA6")));
        m_table->setItem(row, kAppColumn, new QTableWidgetItem(kDash));
        m_table->setItem(row, kRecoveryColumn, new QTableWidgetItem(kDash));

        RowWidgets widgets;
        auto* container = new QWidget(m_table);
        auto* containerLayout = new QHBoxLayout(container);
        containerLayout->setContentsMargins(4, 0, 4, 0);
        widgets.uploadStack = new QStackedWidget(container);
        widgets.uploadStack->setFixedSize(kUploadCellWidth, kCellHeight);
        widgets.uploadButton = new QPushButton(tr("Upload\xE2\x80\xA6"), widgets.uploadStack);
        widgets.uploadButton->setEnabled(!address.isEmpty());
        widgets.uploadButton->setToolTip(
            tr("Send a firmware .bin. If the device is running its application and connected, "
               "it is restarted into the recovery first."));
        widgets.progressBar = new QProgressBar(widgets.uploadStack);
        widgets.progressBar->setRange(0, 100);
        widgets.uploadStack->addWidget(widgets.uploadButton);
        widgets.uploadStack->addWidget(widgets.progressBar);
        widgets.uploadStack->setCurrentWidget(widgets.uploadButton);
        widgets.toAppButton = new QPushButton(tr("To app"), container);
        widgets.toAppButton->setFixedHeight(kCellHeight);
        widgets.toAppButton->setToolTip(tr("Leave the recovery for the application already there."));
        widgets.toAppButton->setEnabled(false);
        containerLayout->addWidget(widgets.uploadStack);
        containerLayout->addWidget(widgets.toAppButton);
        m_table->setCellWidget(row, kActionColumn, container);

        connect(widgets.uploadButton, &QPushButton::clicked, this,
                [this, deviceId]() { startUpload(deviceId); });
        connect(widgets.toAppButton, &QPushButton::clicked, this,
                [this, deviceId]() { rebootToApp(deviceId); });
        m_rowWidgets.insert(deviceId, widgets);

        if (m_uploading.value(deviceId, false) || m_waiting.contains(deviceId)) {
            widgets.uploadStack->setCurrentWidget(widgets.progressBar);
            if (m_waiting.contains(deviceId)) {
                widgets.progressBar->setRange(0, 0);
                widgets.progressBar->setFormat(tr("Restarting\xE2\x80\xA6"));
            }
        }
        if (m_inRecovery.contains(deviceId)) {
            onStatusChecked(deviceId, true, m_inRecovery.value(deviceId), QString());
        }
    }

    if (!previouslySelectedId.isEmpty()) {
        const int row = m_rowByDeviceId.value(previouslySelectedId, -1);
        if (row >= 0) m_table->selectRow(row);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->resizeSection(kAppColumn, kAppColumnWidth);
}

void OtaTab::updateDevice(const Device& device) {
    const int row = m_rowByDeviceId.value(device.id, -1);
    if (row < 0) return;
    m_devices[row] = device;
    const QString address = otaAddressFor(device);
    if (QTableWidgetItem* item = m_table->item(row, kAddressColumn)) {
        item->setText(address.isEmpty() ? tr("(none)") : address);
    }
    const RowWidgets& widgets = m_rowWidgets.value(device.id);
    if (widgets.uploadButton != nullptr) widgets.uploadButton->setEnabled(!address.isEmpty());
}

void OtaTab::setActive(bool active) {
    if (active) {
        refreshNow();
        m_pollTimer->start();
    } else if (m_waiting.isEmpty()) {
        m_pollTimer->stop();  // an upload waiting for its recovery keeps polling
    }
}

void OtaTab::refreshNow() {
    for (const Device& device : m_devices) {
        if (m_uploading.value(device.id, false) || m_client->statusRequestInFlight(device.id)) {
            continue;
        }
        const QString address = otaAddressFor(device);
        if (!address.isEmpty()) {
            m_client->checkStatus(device.id, address);
        }
    }
    if (m_waiting.isEmpty() && !isVisible()) {
        m_pollTimer->stop();
    }
}

void OtaTab::onStatusChecked(const QString& deviceId, bool reachable, const RecoveryStatus& status,
                             const QString& errorMessage) {
    const int row = m_rowByDeviceId.value(deviceId, -1);
    const Device* device = deviceById(deviceId);
    if (row < 0 || device == nullptr) return;  // removed since the request went out

    if (reachable) {
        m_inRecovery.insert(deviceId, status);
    } else {
        m_inRecovery.remove(deviceId);
    }

    // An upload waiting for this device: go as soon as the recovery answers.
    if (m_waiting.contains(deviceId)) {
        if (reachable && status.otaReady) {
            const Waiting waiting = m_waiting.take(deviceId);
            beginUpload(deviceId, waiting.filePath, waiting.keyE);
        } else if (QDateTime::currentMSecsSinceEpoch() > m_waiting.value(deviceId).deadlineMs) {
            m_waiting.remove(deviceId);
            finishRow(deviceId);
            DialogPresenter::warning(
                this, tr("Firmware Upload"),
                tr("%1 did not answer as a recovery in %2 s. Is it on a Wi-Fi network this "
                   "computer can reach? Without a known network, the recovery raises its own "
                   "(\"btp-<name>\").")
                    .arg(device->name)
                    .arg(kRecoveryWaitMs / 1000));
        }
    }

    QTableWidgetItem* state = m_table->item(row, kStateColumn);
    QTableWidgetItem* app = m_table->item(row, kAppColumn);
    QTableWidgetItem* recovery = m_table->item(row, kRecoveryColumn);
    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    if (state == nullptr || app == nullptr || recovery == nullptr) return;

    if (reachable) {
        state->setText(status.otaReady ? tr("In recovery") : tr("Writing\xE2\x80\xA6"));
        state->setForeground(QColor(Qt::darkGreen));
        QString tip = tr("Why: %1").arg(reasonText(status.reason));
        if (!status.ownNetwork.isEmpty()) {
            tip += QLatin1Char('\n') + tr("Own network: %1").arg(status.ownNetwork);
        }
        state->setToolTip(tip);
        app->setText(status.appValid ? QStringLiteral("%1 %2").arg(status.appProject, status.appVersion)
                                     : tr("(none)"));
        app->setToolTip(QString());
        recovery->setText(status.recoveryVersion);
        recovery->setToolTip(tr("%1, up to %2 KB").arg(status.chip).arg(status.maxSize / 1024));
        if (widgets.toAppButton != nullptr) {
            widgets.toAppButton->setEnabled(status.appValid && !m_uploading.value(deviceId, false));
        }
        return;
    }

    // Not a recovery: either the application is running (connected, or the
    // connection refused -- nothing listens on port 80 in the application)
    // or the device is out of reach.
    if (device->connected) {
        state->setText(tr("Running the app"));
        state->setForeground(QColor(Qt::darkBlue));
        state->setToolTip(tr("Upload restarts it into the recovery first."));
    } else {
        state->setText(tr("Offline"));
        state->setForeground(QColor(Qt::gray));
        state->setToolTip(errorMessage.isEmpty() ? tr("Not reachable") : errorMessage);
    }
    const QString running = reportedValue(*device, "fw_version");
    app->setText(device->connected && !running.isEmpty() ? running : kDash);
    app->setToolTip(device->connected ? tr("Reported by the running application.") : QString());
    recovery->setText(kDash);
    recovery->setToolTip(QString());
    if (widgets.toAppButton != nullptr) widgets.toAppButton->setEnabled(false);
}

QByteArray OtaTab::keyFor(const Device& device, bool* cancelled) {
    *cancelled = false;
    QString password = device.peerPassword;
    if (password.isEmpty()) password = m_sessionPasswords.value(device.id);
    if (password.isEmpty()) {
        bool ok = false;
        password = DialogPresenter::getText(
            this, tr("Firmware Upload"),
            tr("Password E of %1 (the one its key was provisioned with). It stays on this "
               "computer: the upload is signed with it.")
                .arg(device.name),
            QLineEdit::Password, QString(), &ok);
        if (!ok || password.isEmpty()) {
            *cancelled = true;
            return QByteArray();
        }
        m_sessionPasswords.insert(device.id, password);
    }
    return m_keyProvider ? m_keyProvider(password) : QByteArray();
}

void OtaTab::startUpload(const QString& deviceId) {
    const Device* device = deviceById(deviceId);
    if (device == nullptr) return;
    const QString address = otaAddressFor(*device);
    const bool inRecovery = m_inRecovery.contains(deviceId);

    if (!inRecovery && !device->connected) {
        DialogPresenter::warning(
            this, tr("Firmware Upload"),
            tr("%1 is not in the recovery, and TraceView is not connected to it to restart it "
               "there. Connect it, or send \"btp -recovery\" from its terminal.")
                .arg(device->name));
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Select Firmware"), QString(), tr("Firmware Binary (*.bin);;All Files (*)"));
    if (filePath.isEmpty()) return;

    // A DEVELOPMENT recovery (auth "none") needs no password.
    QByteArray keyE;
    if (!inRecovery || m_inRecovery.value(deviceId).authRequired) {
        bool cancelled = false;
        keyE = keyFor(*device, &cancelled);
        if (cancelled) return;
    }

    if (inRecovery) {
        beginUpload(deviceId, filePath, keyE);
        return;
    }

    if (!DialogPresenter::confirm(
            this, tr("Firmware Upload"),
            tr("%1 will restart into its recovery and receive %2. Then it starts the new "
               "firmware by itself.")
                .arg(device->name, QFileInfo(filePath).fileName()),
            tr("Restart and upload"), tr("Cancel"))) {
        return;
    }
    m_waiting.insert(deviceId,
                     {filePath, keyE, QDateTime::currentMSecsSinceEpoch() + kRecoveryWaitMs});
    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    widgets.progressBar->setRange(0, 0);
    widgets.progressBar->setFormat(tr("Restarting\xE2\x80\xA6"));
    widgets.uploadStack->setCurrentWidget(widgets.progressBar);
    widgets.toAppButton->setEnabled(false);
    emit recoveryRequested(deviceId);
    m_pollTimer->start();
}

void OtaTab::beginUpload(const QString& deviceId, const QString& filePath, const QByteArray& keyE) {
    const Device* device = deviceById(deviceId);
    if (device == nullptr) return;
    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    widgets.progressBar->setRange(0, 100);
    widgets.progressBar->setValue(0);
    widgets.progressBar->setFormat(tr("Uploading\xE2\x80\xA6 %p%"));
    widgets.uploadStack->setCurrentWidget(widgets.progressBar);
    widgets.toAppButton->setEnabled(false);
    m_uploading.insert(deviceId, true);
    m_client->uploadFirmware(deviceId, otaAddressFor(*device), keyE, filePath);
}

void OtaTab::rebootToApp(const QString& deviceId) {
    const Device* device = deviceById(deviceId);
    if (device == nullptr || !m_inRecovery.contains(deviceId)) return;
    QByteArray keyE;
    if (m_inRecovery.value(deviceId).authRequired) {
        bool cancelled = false;
        keyE = keyFor(*device, &cancelled);
        if (cancelled) return;
    }
    m_uploading.insert(deviceId, true);
    m_rowWidgets.value(deviceId).toAppButton->setEnabled(false);
    m_client->rebootToApp(deviceId, otaAddressFor(*device), keyE);
}

void OtaTab::onUploadProgress(const QString& deviceId, qint64 sent, qint64 total) {
    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    if (widgets.progressBar == nullptr) return;
    if (total > 0) {
        widgets.progressBar->setRange(0, 100);
        widgets.progressBar->setValue(static_cast<int>((sent * 100) / total));
    } else {
        widgets.progressBar->setRange(0, 0);
    }
}

void OtaTab::finishRow(const QString& deviceId) {
    m_uploading.remove(deviceId);
    const RowWidgets& widgets = m_rowWidgets.value(deviceId);
    if (widgets.uploadStack == nullptr) return;
    widgets.uploadStack->setCurrentWidget(widgets.uploadButton);
}

void OtaTab::onUploadFinished(const QString& deviceId, bool success, const QString& message) {
    finishRow(deviceId);
    const Device* device = deviceById(deviceId);
    const QString name = device != nullptr ? device->name : deviceId;
    if (success) {
        // The recovery restarts into the new application now: it stops
        // answering, and the row turns back to "Running the app" once the
        // connection comes back.
        m_inRecovery.remove(deviceId);
        emit statusMessage(tr("%1: %2").arg(name, message));
        return;
    }
    if (message.contains(QLatin1String("wrong password"), Qt::CaseInsensitive)) {
        m_sessionPasswords.remove(deviceId);  // ask again next time
    }
    DialogPresenter::warning(this, tr("Firmware Upload"), tr("%1: %2").arg(name, message));
}

void OtaTab::onRebootFinished(const QString& deviceId, bool success, const QString& message) {
    finishRow(deviceId);
    const Device* device = deviceById(deviceId);
    const QString name = device != nullptr ? device->name : deviceId;
    if (success) {
        m_inRecovery.remove(deviceId);
        emit statusMessage(tr("%1: %2").arg(name, message));
    } else {
        DialogPresenter::warning(this, tr("Firmware Upload"), tr("%1: %2").arg(name, message));
    }
}

}  // namespace traceview
