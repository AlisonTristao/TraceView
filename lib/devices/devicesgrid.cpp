#include "devicesgrid.h"

#include <QJsonArray>
#include <QResizeEvent>
#include <QStringList>
#include <QTimer>
#include <QUuid>

#include "devicecard.h"
#include "devicecommands.h"
#include "deviceconfigdialog.h"

namespace traceview {

namespace {
// Same spacing scale as DashboardGrid::gutter() (see kGutterFraction/
// kMinGutter/kMaxGutter in dashboard/dashboardgrid.cpp), kept as a separate
// copy since this widget doesn't depend on the dashboard library.
constexpr double kGutterFraction = 0.01;
constexpr int kMinGutter = 8;
constexpr int kMaxGutter = 32;
}  // namespace

DevicesGrid::DevicesGrid(QWidget* parent) : QWidget(parent), m_undoStack(new QUndoStack(this)) {}

QString DevicesGrid::addDevice(const Device& device) {
    Device toAdd = device;
    if (toAdd.id.isEmpty()) {
        toAdd.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_undoStack->push(new AddDeviceCommand(this, toAdd, m_devices.size()));
    return toAdd.id;
}

QStringList DevicesGrid::childDeviceIds(const QString& id) const {
    QStringList children;
    if (id.isEmpty()) {
        return children;
    }
    for (const Device& device : m_devices) {
        if (device.transportType == TransportType::HubChannel && device.parentDeviceId == id) {
            children.append(device.id);
        }
    }
    return children;
}

void DevicesGrid::removeDevice(const QString& id) {
    const int idx = indexOfDevice(id);
    if (idx < 0) {
        return;
    }

    // Deleting a hub that other devices ride is refused, and deliberately not
    // turned into a cascade. A cascade would be one undo step that silently
    // destroys several devices along with their charts' bindings -- and the
    // person clicking delete on the dongle is usually not asking to lose the
    // robots configured behind it. Refusing says what depends on it and lets
    // them decide; deleting the children first is one extra click and is
    // unambiguous.
    const QStringList children = childDeviceIds(id);
    if (!children.isEmpty()) {
        QStringList names;
        for (const QString& childId : children) {
            const int childIdx = indexOfDevice(childId);
            if (childIdx < 0) {
                continue;
            }
            const Device& child = m_devices.at(childIdx);
            names.append(child.name.isEmpty() ? childId : child.name);
        }
        emit removeBlockedByChildren(id, names);
        return;
    }

    m_undoStack->push(new RemoveDeviceCommand(this, m_devices.at(idx), idx));
}

void DevicesGrid::updateDevice(const Device& device) {
    const int idx = indexOfDevice(device.id);
    if (idx < 0) {
        return;
    }
    m_undoStack->push(new UpdateDeviceCommand(this, m_devices.at(idx), device));
}

void DevicesGrid::setDeviceConnected(const QString& id, bool connected) {
    const int idx = indexOfDevice(id);
    if (idx < 0 || m_devices[idx].connected == connected) {
        return;
    }
    m_devices[idx].connected = connected;
    m_cards[idx]->setDevice(m_devices[idx]);
    emit deviceUpdated(m_devices[idx]);
}

void DevicesGrid::setDeviceIdentity(const QString& id, const QString& btpVersion,
                                    const QString& btpId) {
    const int idx = indexOfDevice(id);
    if (idx < 0 || (m_devices[idx].btpVersion == btpVersion && m_devices[idx].btpId == btpId)) {
        return;
    }
    m_devices[idx].btpVersion = btpVersion;
    m_devices[idx].btpId = btpId;
    m_cards[idx]->setDevice(m_devices[idx]);
    emit deviceUpdated(m_devices[idx]);
}

void DevicesGrid::notifyCatalogChanged(const QString& id) {
    emit deviceCatalogChanged(id);
}

void DevicesGrid::applyInsertDevice(const Device& device, int index) {
    const int clampedIndex = qBound(0, index, m_devices.size());
    m_devices.insert(clampedIndex, device);

    auto* card = new DeviceCard(this);
    card->setDevice(device);
    connect(card, &DeviceCard::configRequested, this, &DevicesGrid::handleConfigRequested);
    connect(card, &DeviceCard::selectRequested, this, &DevicesGrid::handleCardSelectRequested);
    connect(card, &DeviceCard::connectToggleRequested, this, &DevicesGrid::connectToggleRequested);
    card->show();
    m_cards.insert(clampedIndex, card);

    relayout();
    emit deviceAdded(device);
}

void DevicesGrid::applyRemoveDeviceById(const QString& id) {
    const int idx = indexOfDevice(id);
    if (idx < 0) {
        return;
    }
    DeviceCard* card = m_cards.at(idx);
    m_devices.removeAt(idx);
    m_cards.removeAt(idx);
    card->deleteLater();

    if (m_selectedId == id) {
        m_selectedId.clear();
        emit selectionChanged();
    }

    relayout();  // re-flows every following card into the gap left behind
    emit deviceRemoved(id);
}

void DevicesGrid::applyUpdateDevice(const Device& device) {
    const int idx = indexOfDevice(device.id);
    if (idx < 0) {
        return;
    }
    m_devices[idx] = device;
    m_cards[idx]->setDevice(device);
    emit deviceUpdated(device);
}

void DevicesGrid::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayout();
}

int DevicesGrid::gutter() const {
    return qBound(kMinGutter, qRound(qMin(width(), height()) * kGutterFraction), kMaxGutter);
}

void DevicesGrid::relayout() {
    if (m_cards.isEmpty()) {
        return;
    }

    const int g = gutter();
    const int cardW = kDeviceCardSize.width();
    const int cardH = kDeviceCardSize.height();
    const int pitchW = cardW + g;
    const int pitchH = cardH + g;
    // Each row: a leading margin `g`, then N cards each taking cardW plus a
    // trailing gutter `g` (the last card's trailing gutter doubles as the
    // right margin, same "outer margin == gutter" convention as
    // DashboardGrid::usableRect()/itemRect()).
    const int available = qMax(0, width() - g);
    const int itemsPerRow = qMax(1, available / pitchW);

    for (int i = 0; i < m_cards.size(); ++i) {
        const int row = i / itemsPerRow;
        const int col = i % itemsPerRow;
        const int x = g + col * pitchW;
        const int y = g + row * pitchH;
        m_cards[i]->setGeometry(x, y, cardW, cardH);
    }
}

int DevicesGrid::indexOfDevice(const QString& id) const {
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].id == id) {
            return i;
        }
    }
    return -1;
}

void DevicesGrid::handleConfigRequested(const QString& deviceId) {
    const int idx = indexOfDevice(deviceId);
    if (idx < 0) {
        return;
    }
    DeviceConfigDialog dialog(m_devices[idx], this);
    if (m_portListProvider) {
        dialog.setAvailablePorts(m_portListProvider());
        connect(&dialog, &DeviceConfigDialog::refreshPortsRequested, &dialog,
                [this, &dialog]() { dialog.setAvailablePorts(m_portListProvider()); });
    }
    if (m_usbDeviceListProvider) {
        dialog.setAvailableUsbDevices(m_usbDeviceListProvider());
        connect(&dialog, &DeviceConfigDialog::refreshUsbDevicesRequested, &dialog,
                [this, &dialog]() { dialog.setAvailableUsbDevices(m_usbDeviceListProvider()); });
    }
    // Which devices this one could ride. Computed here rather than injected
    // like the port and USB lists, because unlike those two it is not a
    // question about the machine -- it is entirely answerable from the device
    // list this grid already owns.
    //
    // A hub channel is excluded from being a parent: a robot reached through
    // a dongle is an endpoint, not a hub, and nothing in this design nests
    // one channel inside another. Excluding them also makes a cycle
    // unrepresentable rather than something to detect afterwards.
    {
        QVector<QPair<QString, QString>> parents;
        for (const Device& candidate : m_devices) {
            if (candidate.id == m_devices[idx].id ||
                candidate.transportType == TransportType::HubChannel) {
                continue;
            }
            parents.append({candidate.id, candidate.name});
        }
        dialog.setAvailableParentDevices(parents);
    }
    if (m_topicCatalogProvider) {
        dialog.setCatalogTopics(m_topicCatalogProvider(m_devices[idx].id));
    }
    // Live "Robot source_id" picker: an initial pull right away, then
    // re-polled on a timer for as long as the dialog stays open. Unlike the
    // port/USB lists above (a one-shot OS query, refreshed only on a button
    // click) new peers or a status flip (online, last_seen) arrive
    // continuously over telemetry -- and "Through device" can itself change
    // while the dialog is open, so polling also picks that up within a
    // second rather than needing a dedicated change signal. Parented to
    // `dialog` so it's torn down with it automatically.
    if (m_hubPeerListProvider) {
        dialog.setAvailableHubPeers(m_hubPeerListProvider(dialog.currentParentDeviceId()));
        auto* hubPeerTimer = new QTimer(&dialog);
        hubPeerTimer->setInterval(1000);
        connect(hubPeerTimer, &QTimer::timeout, &dialog, [this, &dialog]() {
            dialog.setAvailableHubPeers(m_hubPeerListProvider(dialog.currentParentDeviceId()));
        });
        hubPeerTimer->start();
    }
    // Connect button: applies the dialog's current fields (same as OK) but
    // leaves the dialog open. The actual (re)connect happens asynchronously
    // over in MainWindow (listening to deviceUpdated), so its result is
    // pushed back into the still-open dialog by the deviceUpdated listener
    // right below, rather than read back here.
    connect(&dialog, &DeviceConfigDialog::applyRequested, &dialog,
            [this, &dialog]() { updateDevice(dialog.result()); });
    // Mirrors this device's live state into the still-open dialog as it
    // changes -- connection status and (once a handshake completes) its
    // reported BTP version/ID and catalog -- so clicking Connect above
    // shows its result in place instead of requiring OK-then-reopen to see
    // it.
    connect(this, &DevicesGrid::deviceUpdated, &dialog,
            [this, &dialog, deviceId](const Device& device) {
                if (device.id != deviceId) {
                    return;
                }
                dialog.setConnectionStatus(device.connected);
                dialog.setReportedIdentity(device.btpVersion, device.btpId);
                if (m_topicCatalogProvider) {
                    dialog.setCatalogTopics(m_topicCatalogProvider(deviceId));
                }
            });
    // MANIFEST_DATA arrives after the handshake that fires deviceUpdated
    // above, so that listener alone typically catches the catalog still
    // empty (see notifyCatalogChanged()'s doc comment). This is the actual
    // "catalog just arrived" signal, wired separately so it doesn't also
    // re-trigger MainWindow::onDeviceUpdated the way deviceUpdated does.
    connect(this, &DevicesGrid::deviceCatalogChanged, &dialog,
            [this, &dialog, deviceId](const QString& id) {
                if (id != deviceId || !m_topicCatalogProvider) {
                    return;
                }
                dialog.setCatalogTopics(m_topicCatalogProvider(deviceId));
            });
    if (dialog.exec() == QDialog::Accepted) {
        updateDevice(dialog.result());
    }
}

void DevicesGrid::handleCardSelectRequested(const QString& deviceId) {
    if (m_selectedId == deviceId) {
        return;
    }
    const int previousIdx = indexOfDevice(m_selectedId);
    if (previousIdx >= 0) {
        m_cards[previousIdx]->setSelected(false);
    }
    m_selectedId = deviceId;
    const int idx = indexOfDevice(deviceId);
    if (idx >= 0) {
        m_cards[idx]->setSelected(true);
    }
    emit selectionChanged();
}

void DevicesGrid::removeSelected() {
    if (m_selectedId.isEmpty()) {
        return;
    }
    removeDevice(m_selectedId);
}

QJsonObject DevicesGrid::toJson() const {
    QJsonObject object;
    QJsonArray devices;
    for (const Device& device : m_devices) {
        devices.append(deviceToJson(device));
    }
    object["devices"] = devices;
    return object;
}

void DevicesGrid::fromJson(const QJsonObject& object) {
    // Bulk load, not a user edit -- goes straight through the apply*()
    // mutators (same reasoning as DashboardGrid::fromJson()) so loading a
    // project doesn't fill undoStack() with N add/remove steps that would
    // then need clearing right after.
    while (!m_devices.isEmpty()) {
        applyRemoveDeviceById(m_devices.first().id);
    }

    const QJsonArray devices = object.value("devices").toArray();
    for (const QJsonValue& value : devices) {
        bool ok = false;
        Device device = deviceFromJson(value.toObject(), &ok);
        if (!ok) {
            continue;
        }
        applyInsertDevice(device, m_devices.size());
    }
}

}  // namespace traceview
