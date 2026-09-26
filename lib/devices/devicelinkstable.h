#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

#include "devices/device.h"

class QPushButton;
class QTableWidget;
class QToolButton;

namespace traceview {

// Every way to reach one device, as an editable table -- the whole
// "Connection" section of DeviceConfigDialog. Row 1 is the primary link (the
// Device's own transport fields), the rest its extraLinks, in the order
// MainWindow's DeviceLinkCycler tries them whenever the link in use is not
// live. Up/Down reorder them, so any row can become the primary one.
//
// Each row: on/off, type (Serial / USB / TCP / BLE / Hub), target, and the one
// type-specific option (baud, TCP port, the robot's source id behind a hub).
// Every cell widget writes straight back into m_links, so links() is always
// what the table shows. The option lists (ports, USB devices, BLE scan
// results, hub peers) arrive on timers while the dialog is open, so they are
// merged into the existing combos in place -- never by rebuilding the table,
// which would throw away whatever is being typed.
class DeviceLinksTable : public QWidget {
    Q_OBJECT

public:
    explicit DeviceLinksTable(const QVector<DeviceLink>& links, QWidget* parent = nullptr);

    QVector<DeviceLink> links() const {
        return m_links;
    }
    bool hasLinkOfType(TransportType type) const;
    // Replaces every row (the simple view above the table edited the links).
    // Does not emit linksChanged().
    void setLinks(const QVector<DeviceLink>& links);
    // The (id, name) hubs offered, and the robots last reported -- shared with
    // the simple view so it offers the same lists.
    const QVector<QPair<QString, QString>>& availableParents() const {
        return m_parents;
    }

    void setAvailablePorts(const QVector<SerialPortOption>& ports);
    void setAvailableUsbDevices(const QVector<UsbDeviceOption>& devices);
    // (id, name) of the devices that can carry a hub channel.
    void setAvailableParentDevices(const QVector<QPair<QString, QString>>& parents);
    void addDiscoveredBleDevice(const QString& name, const QString& address);
    // The hub whose peer list is wanted (the selected hub row's, else the
    // first hub row's) and that list, offered in those rows' robot-id combo.
    QString hubParentForPeers() const;
    void setAvailableHubPeers(const QVector<HubPeer>& peers);
    // Stops a running BLE scan (the dialog is closing).
    void stopBleScan();

signals:
    void linksChanged();
    void refreshPortsRequested();
    void refreshUsbDevicesRequested();
    void scanBleRequested(bool start);

private:
    void rebuildTable();
    void buildRow(int row);
    void updateButtons();
    int currentRow() const;
    void fillTargetCombo(int row);
    void fillHubPeerCombo(int row);
    // The source_id a hub row's robot combo text stands for: a typed hex or
    // decimal id, or the name of exactly one peer in m_peers. 0 = none.
    quint32 hubPeerIdForText(const QString& text) const;

    QVector<DeviceLink> m_links;
    QVector<SerialPortOption> m_ports;
    QVector<UsbDeviceOption> m_usbDevices;
    QVector<QPair<QString, QString>> m_parents;
    QVector<QPair<QString, QString>> m_bleDevices;  // (name, address)
    QString m_peersParent;
    QVector<HubPeer> m_peers;

    QTableWidget* m_table = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
    QToolButton* m_scanBleButton = nullptr;
};

}  // namespace traceview
