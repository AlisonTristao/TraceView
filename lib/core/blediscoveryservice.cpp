#include "blediscoveryservice.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>

#include "core/applog.h"
#include "core/blepermission.h"

namespace traceview {

QBluetoothUuid BleDiscoveryService::btpServiceUuid() {
    // BTP/docs/fragmentation-and-transports.md section 8 (TAREFAS_TCP_BLE_
    // ANDROID.txt T04's note): the one service UUID every BTP-capable
    // peripheral advertises.
    return QBluetoothUuid(QStringLiteral("547a1aae-676e-4b68-8e20-bace26cd0726"));
}

BleDiscoveryService::BleDiscoveryService(QObject* parent) : QObject(parent) {}

BleDiscoveryService::~BleDiscoveryService() {
    stop();
}

void BleDiscoveryService::start() {
    // Torn down and rebuilt rather than reused: this is also what makes
    // stop() an honest cancellation (see its own comment), and it means
    // start() while already scanning behaves as "restart" rather than
    // silently doing nothing or running two agents at once.
    stop();

    // The scan itself waits for the Bluetooth permission (see
    // blepermission.h) -- synchronous on desktop, a system prompt on
    // Android/macOS/iOS. `generation` is what lets a stop() or a newer
    // start() made while that prompt is up win over its late answer.
    m_awaitingPermission = true;
    const quint64 generation = ++m_generation;
    withBluetoothPermission(this, [this, generation](bool granted) {
        if (generation != m_generation) {
            return;
        }
        m_awaitingPermission = false;
        if (!granted) {
            qCWarning(lcConnection) << "BLE discovery: Bluetooth permission denied";
            emit errorOccurred(tr("Bluetooth permission denied. Allow TraceView to use "
                                  "Bluetooth in the system settings."));
            emit finished();
            return;
        }
        startAgent();
    });
}

void BleDiscoveryService::startAgent() {
    m_agent = new QBluetoothDeviceDiscoveryAgent(this);
    // The ESP32-S3's BTP peripheral is BLE-only (no Bluetooth classic/SPP --
    // TAREFAS_TCP_BLE_ANDROID.txt rule for all tasks). Restricting the
    // method avoids a classic-device inquiry that would just add latency and
    // irrelevant results on backends that support both.
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
            &BleDiscoveryService::onDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this,
            &BleDiscoveryService::onAgentFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            &BleDiscoveryService::onAgentError);
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleDiscoveryService::stop() {
    // Also cancels a start() still waiting on the permission prompt.
    ++m_generation;
    m_awaitingPermission = false;
    if (m_agent == nullptr) {
        return;
    }
    // deleteLater(), not delete: this may be called from one of m_agent's
    // own signal handlers (a caller reacting to deviceDiscovered() by
    // deciding to stop). Disconnecting first is what actually matters for
    // "no obsolete results" -- once disconnected, nothing this object still
    // does can reach deviceDiscovered()/finished() above, regardless of when
    // the underlying object is actually destroyed.
    m_agent->disconnect(this);
    m_agent->stop();
    m_agent->deleteLater();
    m_agent = nullptr;
}

void BleDiscoveryService::onDeviceDiscovered(const QBluetoothDeviceInfo& info) {
    if (!info.serviceUuids().contains(btpServiceUuid())) {
        return;
    }
    // CoreBluetooth (macOS/iOS) never exposes a peripheral's MAC: address()
    // is null there and deviceUuid() is the only handle -- which
    // BleTransport::open() already accepts in place of a MAC.
    const QString address = info.address().isNull() ? info.deviceUuid().toString()
                                                     : info.address().toString();
    emit deviceDiscovered(DiscoveredDevice{address, info.name()});
}

void BleDiscoveryService::onAgentFinished() {
    emit finished();
}

void BleDiscoveryService::onAgentError() {
    if (m_agent == nullptr) {
        return;
    }
    const QString message = m_agent->errorString();
    qCWarning(lcConnection) << "BLE discovery error:" << message;
    emit errorOccurred(message.isEmpty() ? tr("BLE discovery failed") : message);
}

}  // namespace traceview
