#include "devicesgrid.h"

#include <QJsonArray>
#include <QResizeEvent>
#include <QUuid>

#include "devicecard.h"
#include "deviceconfigdialog.h"

namespace traceview {

namespace {
// Same spacing scale as DashboardGrid::gutter() (see kGutterFraction/
// kMinGutter/kMaxGutter in dashboard/dashboardgrid.cpp), kept as a separate
// copy since this widget doesn't depend on the dashboard library.
constexpr double kGutterFraction = 0.01;
constexpr int kMinGutter = 8;
constexpr int kMaxGutter = 32;
} // namespace

DevicesGrid::DevicesGrid(QWidget* parent) : QWidget(parent) {}

QString DevicesGrid::addDevice(const Device& device) {
    Device toAdd = device;
    if (toAdd.id.isEmpty()) {
        toAdd.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_devices.append(toAdd);

    auto* card = new DeviceCard(this);
    card->setDevice(toAdd);
    connect(card, &DeviceCard::configRequested, this, &DevicesGrid::handleConfigRequested);
    connect(card, &DeviceCard::selectRequested, this, &DevicesGrid::handleCardSelectRequested);
    connect(card, &DeviceCard::connectToggleRequested, this, &DevicesGrid::connectToggleRequested);
    card->show();
    m_cards.append(card);

    relayout();
    emit deviceAdded(toAdd);
    return toAdd.id;
}

void DevicesGrid::removeDevice(const QString& id) {
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

    relayout(); // re-flows every following card into the gap left behind
    emit deviceRemoved(id);
}

void DevicesGrid::updateDevice(const Device& device) {
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
    while (!m_devices.isEmpty()) {
        removeDevice(m_devices.first().id);
    }

    const QJsonArray devices = object.value("devices").toArray();
    for (const QJsonValue& value : devices) {
        bool ok = false;
        Device device = deviceFromJson(value.toObject(), &ok);
        if (!ok) {
            continue;
        }
        addDevice(device);
    }
}

} // namespace traceview
