#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

#include "devices/device.h"

class QCheckBox;
class QComboBox;
class QToolButton;

namespace traceview {

// The simple half of DeviceConfigDialog's "Connection" section: one robot
// name, and one row per kind of link -- a checkbox saying "this robot has
// it" and a target that defaults to "Automatic", i.e. found by that name
// (DeviceLink::autoTarget, resolveLinkByName()). DeviceLinksTable underneath
// stays the advanced view of the very same list (order, baud, TCP port,
// hand-typed targets); the dialog keeps the two in step through setLinks()
// and linksEdited().
//
// Each row edits the FIRST link of its type. Ticking a row with none adds
// one, set to automatic, in the canonical order (Serial, USB, TCP, BLE, Hub
// -- fastest and most reliable first); unticking removes it.
class RobotLinksPanel : public QWidget {
    Q_OBJECT

public:
    RobotLinksPanel(const QString& robotName, const QVector<DeviceLink>& links,
                    QWidget* parent = nullptr);

    QString robotName() const;
    // Shows `links` (the advanced table was edited). Does not emit.
    void setLinks(const QVector<DeviceLink>& links);

    // A name the robot combo offers -- a USB product name, a BLE
    // advertisement, a robot a hub reported, the name the device itself
    // reported. Duplicates are ignored.
    void addRobotNameCandidate(const QString& name);
    void setAvailablePorts(const QVector<SerialPortOption>& ports);
    void addDiscoveredBleDevice(const QString& name, const QString& address);
    void setAvailableParentDevices(const QVector<QPair<QString, QString>>& parents);
    void setAvailableHubPeers(const QVector<HubPeer>& peers);
    // Stops a running search (the dialog is closing).
    void stopSearch();

signals:
    void linksEdited(const QVector<DeviceLink>& links);
    void robotNameChanged(const QString& name);
    void refreshPortsRequested();
    void scanBleRequested(bool start);

private:
    struct Row {
        TransportType type = TransportType::Serial;
        QCheckBox* check = nullptr;
        QComboBox* combo = nullptr;  // editable; entry 0 = Automatic
    };

    int indexOfType(TransportType type) const;
    void refreshRows();
    void refreshRow(Row& row);
    void fillCombo(Row& row);
    void onCheckToggled(Row& row, bool on);
    void onTargetEdited(Row& row);
    void emitEdited();

    QVector<DeviceLink> m_links;
    QVector<Row> m_rows;
    QVector<SerialPortOption> m_ports;
    QVector<QPair<QString, QString>> m_bleDevices;  // (name, address)
    QVector<QPair<QString, QString>> m_parents;     // (id, name)

    QComboBox* m_nameCombo = nullptr;
    QToolButton* m_searchButton = nullptr;
    bool m_refreshing = false;
};

}  // namespace traceview
