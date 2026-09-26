#include "devices/robotlinkspanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace traceview {

namespace {

// Where a newly ticked link goes: fastest and most reliable first.
int canonicalRank(TransportType type) {
    switch (type) {
        case TransportType::Serial:
            return 0;
        case TransportType::UsbHid:
            return 1;
        case TransportType::Tcp:
            return 2;
        case TransportType::Ble:
            return 3;
        case TransportType::HubChannel:
            return 4;
    }
    return 5;
}

QString rowLabel(TransportType type) {
    switch (type) {
        case TransportType::Serial:
            return RobotLinksPanel::tr("Serial (USB)");
        case TransportType::Tcp:
            return RobotLinksPanel::tr("Wi-Fi");
        case TransportType::Ble:
            return RobotLinksPanel::tr("Bluetooth");
        case TransportType::HubChannel:
            return RobotLinksPanel::tr("Hub");
        case TransportType::UsbHid:
            break;
    }
    return transportTypeLabel(type);
}

QString rowToolTip(TransportType type) {
    switch (type) {
        case TransportType::Serial:
            return RobotLinksPanel::tr(
                "Automatic: the USB port whose device reports the robot's name (or, if none "
                "does, the port last picked here). Pick or type a port to always use that one.");
        case TransportType::Tcp:
            return RobotLinksPanel::tr(
                "Automatic: <robot name>.local, found over mDNS. Type a host or IP to use "
                "that instead (e.g. 192.168.4.1).");
        case TransportType::Ble:
            return RobotLinksPanel::tr(
                "Automatic: the Bluetooth robot advertising the robot's name. Pick or type an "
                "address to always use that one.");
        case TransportType::HubChannel:
            return RobotLinksPanel::tr(
                "Automatic: the robot of this name behind any connected hub. Pick a hub to "
                "only look behind that one.");
        case TransportType::UsbHid:
            break;
    }
    return QString();
}

// Every row's target is the same kind of field -- an editable combo whose
// first entry is Automatic -- so the rows line up and read alike.
QComboBox* targetCombo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    // A long port label or BLE name must not widen the whole dialog.
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(18);
    return combo;
}

// What an editable target combo holds: the selected entry's data (""
// = Automatic), or, when the text was typed rather than picked, the text.
QString comboTarget(const QComboBox* combo) {
    const QString text = combo->currentText().trimmed();
    const int index = combo->currentIndex();
    if (index >= 0 && combo->itemText(index) == combo->currentText()) {
        return combo->itemData(index).toString();
    }
    const int match = combo->findText(text, Qt::MatchFixedString);
    if (match >= 0) {
        return combo->itemData(match).toString();
    }
    return text;
}

}  // namespace

RobotLinksPanel::RobotLinksPanel(const QString& robotName, const QVector<DeviceLink>& links,
                                 QWidget* parent)
    : QWidget(parent), m_links(links) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:"), this));
    m_nameCombo = new QComboBox(this);
    m_nameCombo->setEditable(true);
    m_nameCombo->setInsertPolicy(QComboBox::NoInsert);
    m_nameCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_nameCombo->setMinimumContentsLength(16);
    m_nameCombo->lineEdit()->setPlaceholderText(tr("robot name, e.g. BallyRobot"));
    m_nameCombo->setCurrentText(robotName);
    m_nameCombo->setToolTip(
        tr("The name the robot is configured with (\"settings -set identity name ...\"). "
           "Every link set to Automatic finds the robot by it: the USB port that reports "
           "it, <name>.local over Wi-Fi, the Bluetooth device advertising it, the robot of "
           "that name behind a hub."));
    connect(m_nameCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        if (m_refreshing) {
            return;
        }
        refreshRows();  // the Automatic entries show what the name resolves to
        emit robotNameChanged(text.trimmed());
    });
    nameRow->addWidget(m_nameCombo, 1);

    m_searchButton = new QToolButton(this);
    m_searchButton->setText(tr("Search"));
    m_searchButton->setCheckable(true);
    m_searchButton->setToolTip(
        tr("Look for robots nearby: refreshes the serial ports and scans for Bluetooth "
           "robots. Names found are offered in the Robot list."));
    connect(m_searchButton, &QToolButton::toggled, this, [this](bool on) {
        m_searchButton->setText(on ? tr("Stop") : tr("Search"));
        if (on) {
            emit refreshPortsRequested();
        }
#ifdef TRACEVIEW_ENABLE_BLE
        emit scanBleRequested(on);
#else
        if (on) {
            m_searchButton->setChecked(false);  // nothing keeps running
        }
#endif
    });
    nameRow->addWidget(m_searchButton);
    layout->addLayout(nameRow);

    // Only the links this build can dial; USB HID has no name to match, so
    // it lives in the advanced table only.
    const QVector<TransportType> types = {
#ifdef TRACEVIEW_ENABLE_SERIAL
        TransportType::Serial,
#endif
        TransportType::Tcp,
#ifdef TRACEVIEW_ENABLE_BLE
        TransportType::Ble,
#endif
        TransportType::HubChannel,
    };
    auto* grid = new QGridLayout();
    grid->setColumnStretch(1, 1);
    m_rows.reserve(types.size());
    for (TransportType type : types) {
        const int index = m_rows.size();
        Row row;
        row.type = type;
        row.check = new QCheckBox(rowLabel(type), this);
        row.check->setToolTip(tr("Ticked: the robot can be reached this way."));
        connect(row.check, &QCheckBox::toggled, this,
                [this, index](bool on) { onCheckToggled(m_rows[index], on); });
        row.combo = targetCombo(this);
        row.combo->setToolTip(rowToolTip(type));
        connect(row.combo, qOverload<int>(&QComboBox::activated), this,
                [this, index] { onTargetEdited(m_rows[index]); });
        connect(row.combo->lineEdit(), &QLineEdit::editingFinished, this,
                [this, index] { onTargetEdited(m_rows[index]); });
        grid->addWidget(row.check, index, 0);
        grid->addWidget(row.combo, index, 1);
        m_rows.append(row);
    }
    layout->addLayout(grid);
    refreshRows();
}

QString RobotLinksPanel::robotName() const {
    return m_nameCombo->currentText().trimmed();
}

void RobotLinksPanel::setLinks(const QVector<DeviceLink>& links) {
    m_links = links;
    refreshRows();
}

int RobotLinksPanel::indexOfType(TransportType type) const {
    for (int i = 0; i < m_links.size(); ++i) {
        if (m_links.at(i).transportType == type) {
            return i;
        }
    }
    return -1;
}

void RobotLinksPanel::refreshRows() {
    m_refreshing = true;
    for (Row& row : m_rows) {
        refreshRow(row);
    }
    m_refreshing = false;
}

void RobotLinksPanel::refreshRow(Row& row) {
    bool on = false;
    for (const DeviceLink& link : m_links) {
        if (link.transportType == row.type && link.enabled) on = true;
    }
    {
        const QSignalBlocker blocker(row.check);
        row.check->setChecked(on);
    }
    // Not while the user is typing into it: a port list or scan result
    // arriving mid-word would wipe what they typed.
    if (!row.combo->lineEdit()->hasFocus()) {
        fillCombo(row);
    }
    row.combo->setEnabled(on);
}

// (Re)fills a row's combo from the current lists and selects what its link
// holds. Entry 0 is always the automatic one (data ""), except that a hub
// row's entry 0 means "any hub".
void RobotLinksPanel::fillCombo(Row& row) {
    const QSignalBlocker blocker(row.combo);
    const int index = indexOfType(row.type);
    const DeviceLink link = index >= 0 ? m_links.at(index) : DeviceLink{};
    const bool automatic = index < 0 || link.autoTarget;
    const QString name = robotName();
    row.combo->clear();
    QString value;
    switch (row.type) {
        case TransportType::Serial: {
            QString match;
            int matches = 0;
            for (const SerialPortOption& port : m_ports) {
                if (!name.isEmpty() &&
                    port.productName.trimmed().compare(name, Qt::CaseInsensitive) == 0) {
                    match = port.name;
                    ++matches;
                }
            }
            row.combo->addItem(matches == 1 ? tr("Automatic (%1)").arg(match)
                                            : tr("Automatic (by robot name)"),
                               QString());
            for (const SerialPortOption& port : m_ports) {
                row.combo->addItem(port.label, port.name);
            }
            value = automatic ? QString() : link.portName;
            break;
        }
        case TransportType::Ble:
            row.combo->addItem(name.isEmpty() ? tr("Automatic (by robot name)")
                                              : tr("Automatic (%1)").arg(name),
                               QString());
            for (const auto& [bleName, address] : m_bleDevices) {
                row.combo->addItem(
                    bleName.isEmpty() ? address : tr("%1 (%2)").arg(bleName, address), address);
            }
            value = automatic ? QString() : link.bleAddress;
            break;
        case TransportType::Tcp: {
            const QString host = mdnsHostFromName(name);
            row.combo->addItem(host.isEmpty() ? tr("Automatic (<robot name>.local)")
                                              : tr("Automatic (%1.local)").arg(host),
                               QString());
            value = automatic ? QString() : link.tcpHost;
            break;
        }
        case TransportType::HubChannel:
            row.combo->addItem(automatic ? tr("Automatic (any hub)") : tr("(choose the hub)"),
                               QString());
            for (const auto& [id, parentName] : m_parents) {
                row.combo->addItem(parentName.isEmpty() ? id : parentName, id);
            }
            value = link.parentDeviceId;
            break;
        case TransportType::UsbHid:
            return;
    }
    // Configured by hand but not in the list right now (unplugged, not
    // scanned yet): keep it rather than silently showing something else.
    if (!value.isEmpty() && row.combo->findData(value) < 0) {
        row.combo->addItem(value, value);
    }
    row.combo->setCurrentIndex(qMax(0, row.combo->findData(value)));
}

void RobotLinksPanel::onCheckToggled(Row& row, bool on) {
    if (m_refreshing) {
        return;
    }
    const int index = indexOfType(row.type);
    if (on) {
        if (index >= 0) {
            m_links[index].enabled = true;
        } else {
            DeviceLink link;
            link.transportType = row.type;
            link.autoTarget = true;
            int at = m_links.size();
            for (int i = 0; i < m_links.size(); ++i) {
                if (canonicalRank(m_links.at(i).transportType) > canonicalRank(row.type)) {
                    at = i;
                    break;
                }
            }
            m_links.insert(at, link);
        }
    } else if (index >= 0) {
        // The checkbox governs the transport, including fallback rows added
        // in Advanced. Preserve their settings when temporarily disabled.
        for (DeviceLink& link : m_links) {
            if (link.transportType == row.type) link.enabled = false;
        }
    }
    refreshRows();
    emitEdited();
}

void RobotLinksPanel::onTargetEdited(Row& row) {
    if (m_refreshing) {
        return;
    }
    const int index = indexOfType(row.type);
    if (index < 0) {
        return;
    }
    DeviceLink& link = m_links[index];
    const QString target = comboTarget(row.combo);
    switch (row.type) {
        case TransportType::Tcp:
            link.autoTarget = target.isEmpty();
            if (!target.isEmpty()) {
                link.tcpHost = target;
            }
            break;
        case TransportType::Serial:
            link.autoTarget = target.isEmpty();
            if (!target.isEmpty()) {
                link.portName = target;
            }
            break;
        case TransportType::Ble:
            link.autoTarget = target.isEmpty();
            if (!target.isEmpty()) {
                link.bleAddress = target;
            }
            break;
        case TransportType::HubChannel: {
            // Only a hub from the list can be picked; the robot behind it
            // stays found by name (or, on a link set up by hand, its saved id).
            const int hub = row.combo->findData(target);
            link.parentDeviceId = hub >= 0 ? target : QString();
            break;
        }
        case TransportType::UsbHid:
            return;
    }
    // Shows the entry for what was typed (and drops a hub that is not one).
    refreshRows();
    emitEdited();
}

void RobotLinksPanel::emitEdited() {
    emit linksEdited(m_links);
}

void RobotLinksPanel::addRobotNameCandidate(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_nameCombo->findText(trimmed, Qt::MatchFixedString) >= 0) {
        return;
    }
    // Adding to an editable combo can replace what is typed; put it back.
    const QSignalBlocker blocker(m_nameCombo);
    const QString typed = m_nameCombo->currentText();
    m_nameCombo->addItem(trimmed);
    m_nameCombo->setCurrentText(typed);
}

void RobotLinksPanel::setAvailablePorts(const QVector<SerialPortOption>& ports) {
    m_ports = ports;
    for (const SerialPortOption& port : ports) {
        if (port.productName != port.name) {
            addRobotNameCandidate(port.productName);
        }
    }
    refreshRows();
}

void RobotLinksPanel::addDiscoveredBleDevice(const QString& name, const QString& address) {
    if (address.isEmpty()) {
        return;
    }
    for (auto& seen : m_bleDevices) {
        if (seen.second == address) {
            if (name.isEmpty() || seen.first == name) {
                return;
            }
            seen.first = name;
            addRobotNameCandidate(name);
            refreshRows();
            return;
        }
    }
    m_bleDevices.append({name, address});
    addRobotNameCandidate(name);
    refreshRows();
}

void RobotLinksPanel::setAvailableParentDevices(const QVector<QPair<QString, QString>>& parents) {
    m_parents = parents;
    refreshRows();
}

void RobotLinksPanel::setAvailableHubPeers(const QVector<HubPeer>& peers) {
    for (const HubPeer& peer : peers) {
        addRobotNameCandidate(peer.name);
    }
}

void RobotLinksPanel::stopSearch() {
    if (m_searchButton->isChecked()) {
        m_searchButton->setChecked(false);
    }
}

}  // namespace traceview
