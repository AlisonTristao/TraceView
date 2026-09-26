#include "devices/devicelinkstable.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace traceview {

namespace {

enum Column { EnabledColumn, TypeColumn, TargetColumn, OptionColumn, ColumnCount };

// Only the transports this build can actually dial -- see
// deviceconnection.cpp's constructor for what exists without each option.
QVector<TransportType> offeredTransports() {
    return {
#ifdef TRACEVIEW_ENABLE_SERIAL
        TransportType::Serial,
#endif
#ifdef TRACEVIEW_ENABLE_USB_HID
        TransportType::UsbHid,
#endif
        TransportType::Tcp,
#ifdef TRACEVIEW_ENABLE_BLE
        TransportType::Ble,
#endif
        TransportType::HubChannel,
    };
}

// An editable combo's text is either a listed item's label (store its data,
// which can differ from the label -- Android port keys, "name (address)" BLE
// labels) or something typed by hand (store it verbatim).
QString comboValue(const QComboBox* combo) {
    const QString text = combo->currentText().trimmed();
    const int index = combo->findText(text);
    const QString data = index >= 0 ? combo->itemData(index).toString() : QString();
    return data.isEmpty() ? text : data;
}

// Puts `value` in a combo, showing its listed label when it has one.
void selectComboValue(QComboBox* combo, const QString& value) {
    const int index = combo->findData(value);
    if (index >= 0) {
        combo->setCurrentIndex(index);
    } else if (combo->isEditable()) {
        combo->setCurrentText(value);
    }
}

QString hexSourceId(quint32 id) {
    return id == 0 ? QString()
                   : QStringLiteral("0x") +
                         QStringLiteral("%1").arg(id, 8, 16, QChar('0')).toUpper();
}

// A source_id typed by hand: "0x..." hex or plain decimal. 0 = not set.
quint32 parseSourceId(const QString& text) {
    bool ok = false;
    const qulonglong value = text.trimmed().toULongLong(&ok, 0);
    return ok && value <= 0xFFFFFFFFull ? quint32(value) : 0;
}

QComboBox* boundedCombo(QWidget* parent, bool editable) {
    auto* combo = new QComboBox(parent);
    combo->setEditable(editable);
    // A long port label or BLE name must not widen the whole dialog.
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(14);
    return combo;
}

}  // namespace

DeviceLinksTable::DeviceLinksTable(const QVector<DeviceLink>& links, QWidget* parent)
    : QWidget(parent), m_links(links) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels({tr("On"), tr("Type"), tr("Target"), tr("Option")});
    QHeaderView* header = m_table->horizontalHeader();
    header->setSectionResizeMode(EnabledColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(TypeColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(TargetColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(OptionColumn, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setToolTip(
        tr("Every way to reach this device. Row 1 is tried first; when the connection in use "
           "stops working, TraceView tries the next enabled row, and starts over after the "
           "last."));
    m_table->setMinimumHeight(140);
    // Square editors inside the grid: the theme's rounded corners
    // (theme/stylesheet.cpp) read as floating pills between the cell lines.
    // Scoped to this table, so every other combo in the app keeps its look.
    m_table->setStyleSheet(QStringLiteral(
        "QComboBox, QLineEdit, QSpinBox { border-radius: 0px; }"
        "QComboBox::drop-down { border-top-right-radius: 0px;"
        " border-bottom-right-radius: 0px; }"));
    layout->addWidget(m_table, 1);

    auto* rowButtons = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_upButton = new QPushButton(tr("Up"), this);
    m_downButton = new QPushButton(tr("Down"), this);
    rowButtons->addWidget(addButton);
    rowButtons->addWidget(m_removeButton);
    rowButtons->addWidget(m_upButton);
    rowButtons->addWidget(m_downButton);
    rowButtons->addStretch();
    layout->addLayout(rowButtons);

    // The OS / radio lists the target combos offer: refreshed on demand. A
    // line of their own, so the button rows never set the dialog's width.
    auto* listButtons = new QHBoxLayout();
    auto* refreshPorts = new QToolButton(this);
    refreshPorts->setText(tr("Ports") + QString::fromUtf8(" \xE2\x9F\xB3"));  // ⟳
    refreshPorts->setToolTip(tr("Refresh the serial port list"));
    connect(refreshPorts, &QToolButton::clicked, this, &DeviceLinksTable::refreshPortsRequested);
#ifdef TRACEVIEW_ENABLE_SERIAL
    listButtons->addWidget(refreshPorts);
#else
    refreshPorts->hide();
#endif
    auto* refreshUsb = new QToolButton(this);
    refreshUsb->setText(tr("USB") + QString::fromUtf8(" \xE2\x9F\xB3"));
    refreshUsb->setToolTip(tr("Refresh the USB HID device list"));
    connect(refreshUsb, &QToolButton::clicked, this, &DeviceLinksTable::refreshUsbDevicesRequested);
#ifdef TRACEVIEW_ENABLE_USB_HID
    listButtons->addWidget(refreshUsb);
#else
    refreshUsb->hide();
#endif
    m_scanBleButton = new QToolButton(this);
    m_scanBleButton->setText(tr("Scan BLE"));
    m_scanBleButton->setCheckable(true);
    m_scanBleButton->setToolTip(tr("Scan for nearby BTP-capable BLE robots."));
    connect(m_scanBleButton, &QToolButton::toggled, this, [this](bool on) {
        m_scanBleButton->setText(on ? tr("Stop scan") : tr("Scan BLE"));
        emit scanBleRequested(on);
    });
#ifdef TRACEVIEW_ENABLE_BLE
    listButtons->addWidget(m_scanBleButton);
#else
    m_scanBleButton->hide();
#endif
    listButtons->addStretch();
    layout->addLayout(listButtons);

    // "Add" asks which kind first, so every transport is one click away
    // instead of hidden behind a default the row then has to be changed from.
    auto* addMenu = new QMenu(addButton);
    for (TransportType offered : offeredTransports()) {
        addMenu->addAction(transportTypeLabel(offered), this, [this, offered] {
            DeviceLink link;
            link.transportType = offered;
            m_links.append(link);
            rebuildTable();
            m_table->selectRow(m_links.size() - 1);
            emit linksChanged();
        });
    }
    addButton->setMenu(addMenu);
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        if (row < 0) {
            return;
        }
        m_links.removeAt(row);
        rebuildTable();
        if (!m_links.isEmpty()) {
            m_table->selectRow(qMin(row, m_links.size() - 1));
        }
        emit linksChanged();
    });
    connect(m_upButton, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        if (row <= 0) {
            return;
        }
        m_links.swapItemsAt(row, row - 1);
        rebuildTable();
        m_table->selectRow(row - 1);
        emit linksChanged();
    });
    connect(m_downButton, &QPushButton::clicked, this, [this] {
        const int row = currentRow();
        if (row < 0 || row >= m_links.size() - 1) {
            return;
        }
        m_links.swapItemsAt(row, row + 1);
        rebuildTable();
        m_table->selectRow(row + 1);
        emit linksChanged();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DeviceLinksTable::updateButtons);

    rebuildTable();
    if (!m_links.isEmpty()) {
        m_table->selectRow(0);
    }
}

bool DeviceLinksTable::hasLinkOfType(TransportType type) const {
    for (const DeviceLink& link : m_links) {
        if (link.transportType == type) {
            return true;
        }
    }
    return false;
}

int DeviceLinksTable::currentRow() const {
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void DeviceLinksTable::updateButtons() {
    const int row = currentRow();
    m_removeButton->setEnabled(row >= 0);
    m_upButton->setEnabled(row > 0);
    m_downButton->setEnabled(row >= 0 && row < m_links.size() - 1);
}

void DeviceLinksTable::rebuildTable() {
    m_table->setRowCount(m_links.size());
    for (int row = 0; row < m_links.size(); ++row) {
        buildRow(row);
    }
    updateButtons();
}

void DeviceLinksTable::buildRow(int row) {
    const DeviceLink& link = m_links.at(row);
    // A placeholder item per cell, so the row can be selected by clicking
    // anywhere on it (cell widgets alone do not select).
    for (int column = 0; column < ColumnCount; ++column) {
        m_table->setItem(row, column, new QTableWidgetItem());
    }

    auto* enabled = new QCheckBox(m_table);
    enabled->setChecked(link.enabled);
    enabled->setToolTip(tr("Unticked: kept, but skipped when looking for a working connection."));
    connect(enabled, &QCheckBox::toggled, this, [this, row](bool on) {
        m_links[row].enabled = on;
        emit linksChanged();
    });
    m_table->setCellWidget(row, EnabledColumn, enabled);

    auto* type = new QComboBox(m_table);
    for (TransportType offered : offeredTransports()) {
        type->addItem(transportTypeLabel(offered), int(offered));
    }
    type->setCurrentIndex(qMax(0, type->findData(int(link.transportType))));
    // A new type needs different target/option widgets. Rebuilt deferred, so
    // the combo emitting this signal is not deleted underneath itself.
    connect(type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row, type] {
        m_links[row].transportType = TransportType(type->currentData().toInt());
        QMetaObject::invokeMethod(
            this,
            [this, row] {
                if (row < m_links.size()) {
                    buildRow(row);
                }
            },
            Qt::QueuedConnection);
        emit linksChanged();
    });
    m_table->setCellWidget(row, TypeColumn, type);

    QWidget* target = nullptr;
    QWidget* option = nullptr;
    switch (link.transportType) {
        case TransportType::Serial: {
            auto* port = boundedCombo(m_table, true);
            port->lineEdit()->setPlaceholderText(tr("port, e.g. COM5"));
            connect(port, &QComboBox::currentTextChanged, this, [this, row, port] {
                m_links[row].portName = comboValue(port);
                emit linksChanged();
            });
            target = port;

            auto* baud = new QComboBox(m_table);
            baud->setEditable(true);
            baud->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800",
                            "921600", "1000000", "2000000", "3000000", "5000000"});
            baud->setCurrentText(QString::number(link.baudRate));
            baud->setToolTip(tr("Baud rate (type a custom value if yours isn't listed)"));
            connect(baud, &QComboBox::currentTextChanged, this, [this, row](const QString& text) {
                bool ok = false;
                const int rate = text.toInt(&ok);
                if (ok && rate > 0) {
                    m_links[row].baudRate = rate;
                }
            });
            option = baud;
            break;
        }
        case TransportType::UsbHid: {
            // hidapi paths are not typed by hand -- pick from the list.
            auto* usb = boundedCombo(m_table, false);
            connect(usb, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row, usb] {
                m_links[row].usbPath = usb->currentData().toString();
                emit linksChanged();
            });
            target = usb;
            break;
        }
        case TransportType::Tcp: {
            auto* host = new QLineEdit(link.tcpHost, m_table);
            host->setPlaceholderText(tr("host or IP, e.g. 192.168.4.1"));
            connect(host, &QLineEdit::textChanged, this, [this, row](const QString& text) {
                m_links[row].tcpHost = text.trimmed();
                emit linksChanged();
            });
            target = host;

            auto* port = new QSpinBox(m_table);
            port->setRange(1, 65535);
            port->setValue(link.tcpPort);
            port->setToolTip(tr("TCP server port"));
            connect(port, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, row](int value) { m_links[row].tcpPort = quint16(value); });
            option = port;
            break;
        }
        case TransportType::Ble: {
            auto* address = boundedCombo(m_table, true);
            address->lineEdit()->setPlaceholderText(tr("robot name, e.g. BallyRobot"));
            address->setToolTip(
                tr("The name the robot advertises, like a hostname: TraceView scans for it on "
                   "every connection, so it keeps working if the robot's address changes. A "
                   "MAC address is also accepted. Scan to pick a robot nearby."));
            connect(address, &QComboBox::currentTextChanged, this, [this, row, address] {
                m_links[row].bleAddress = comboValue(address);
                emit linksChanged();
            });
            target = address;
            break;
        }
        case TransportType::HubChannel: {
            auto* hub = boundedCombo(m_table, false);
            hub->setToolTip(tr("The hub whose connection carries this one."));
            connect(hub, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row, hub] {
                m_links[row].parentDeviceId = hub->currentData().toString();
                emit linksChanged();
            });
            target = hub;

            auto* robot = boundedCombo(m_table, true);
            robot->setMinimumContentsLength(12);
            robot->lineEdit()->setPlaceholderText(tr("robot name or id"));
            robot->setToolTip(
                tr("The robot behind the hub. Pick one the hub has heard (listed by name), or "
                   "type its name or its BTP source_id (hex or decimal). What is saved is the "
                   "source_id -- its permanent address, not the channel number the hub shows."));
            connect(robot, &QComboBox::currentTextChanged, this, [this, row, robot] {
                const QString text = robot->currentText();
                const int index = robot->findText(text);
                m_links[row].peerSourceId = index >= 0 ? robot->itemData(index).toUInt()
                                                       : hubPeerIdForText(text);
                emit linksChanged();
            });
            option = robot;
            break;
        }
    }
    // An automatic link has no target to type: it is found by the robot's
    // name (DeviceLink::autoTarget). A hub channel keeps its hub picker --
    // choosing one narrows the search, "(any hub)" searches them all -- and
    // loses the robot id instead. Pick a specific target in the simple view.
    if (link.autoTarget && link.transportType != TransportType::UsbHid) {
        auto* automatic = new QLabel(tr("Automatic (robot name)"), m_table);
        automatic->setToolTip(
            tr("Found by the robot name above each time it connects. To use a specific "
               "target instead, pick one in the list above the table."));
        if (link.transportType == TransportType::HubChannel) {
            delete option;
            option = automatic;
        } else {
            delete target;
            target = automatic;
        }
    }
    m_table->setCellWidget(row, TargetColumn, target);
    m_table->setCellWidget(row, OptionColumn, option != nullptr ? option : new QLabel(m_table));
    fillTargetCombo(row);
    fillHubPeerCombo(row);
}

// (Re)fills row `row`'s target combo from the current option list, keeping
// the value the row holds. Signals blocked: this is not an edit.
void DeviceLinksTable::fillTargetCombo(int row) {
    auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, TargetColumn));
    if (combo == nullptr) {
        return;
    }
    const DeviceLink& link = m_links.at(row);
    const QSignalBlocker blocker(combo);
    const QString typed = combo->isEditable() ? combo->currentText() : QString();
    combo->clear();
    QString value;
    switch (link.transportType) {
        case TransportType::Serial:
            for (const SerialPortOption& port : m_ports) {
                combo->addItem(port.label, port.name);
            }
            value = link.portName;
            break;
        case TransportType::UsbHid:
            for (const UsbDeviceOption& device : m_usbDevices) {
                combo->addItem(device.label, device.path);
            }
            value = link.usbPath;
            if (!value.isEmpty() && combo->findData(value) < 0) {
                // Configured but not plugged in right now: keep it.
                combo->insertItem(0, value, value);
            }
            break;
        case TransportType::Ble:
            // A robot picked from the scan is stored by its name, the same
            // way it would be typed -- unless another robot in the list has
            // that name too, where the name could not tell them apart and
            // only the address can.
            for (const auto& [name, address] : m_bleDevices) {
                int sameName = 0;
                for (const auto& other : m_bleDevices) {
                    sameName += other.first.compare(name, Qt::CaseInsensitive) == 0 ? 1 : 0;
                }
                const bool byName = !name.isEmpty() && sameName == 1;
                combo->addItem(name.isEmpty() ? address : tr("%1 (%2)").arg(name, address),
                               byName ? name : address);
            }
            value = link.bleAddress;
            break;
        case TransportType::HubChannel:
            combo->addItem(link.autoTarget ? tr("(any hub)") : tr("(choose the hub)"), QString());
            for (const auto& [id, name] : m_parents) {
                combo->addItem(name.isEmpty() ? id : name, id);
            }
            value = link.parentDeviceId;
            if (!value.isEmpty() && combo->findData(value) < 0) {
                combo->addItem(tr("%1 (unavailable)").arg(value), value);
            }
            break;
        case TransportType::Tcp:
            return;
    }
    // While being typed into, keep the text as typed; otherwise show the
    // row's value (its label, when the list has it).
    if (combo->isEditable() && combo->lineEdit()->hasFocus()) {
        combo->setCurrentText(typed);
    } else {
        selectComboValue(combo, value);
    }
}

void DeviceLinksTable::fillHubPeerCombo(int row) {
    if (m_links.at(row).transportType != TransportType::HubChannel) {
        return;
    }
    auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, OptionColumn));
    if (combo == nullptr) {
        return;
    }
    const DeviceLink& link = m_links.at(row);
    const QSignalBlocker blocker(combo);
    const QString typed = combo->currentText();
    combo->clear();
    if (!link.parentDeviceId.isEmpty() && link.parentDeviceId == m_peersParent) {
        for (const HubPeer& peer : m_peers) {
            const QString status =
                peer.online ? tr("online") : tr("offline %1s").arg(peer.lastSeenAgeMs / 1000);
            const QString label =
                peer.name.isEmpty()
                    ? tr("%1 (ch %2, %3)").arg(hexSourceId(peer.sourceId)).arg(peer.channel)
                          .arg(status)
                    : tr("%1 (%2, %3)").arg(peer.name, hexSourceId(peer.sourceId), status);
            combo->addItem(label, peer.sourceId);
            const QString tip = peer.mac.isEmpty()
                                    ? tr("channel %1").arg(peer.channel)
                                    : tr("channel %1, %2").arg(peer.channel).arg(peer.mac);
            combo->setItemData(combo->count() - 1, tip, Qt::ToolTipRole);
        }
    }
    if (combo->lineEdit()->hasFocus()) {
        combo->setCurrentText(typed);
        return;
    }
    // A name typed before the hub had reported that robot resolves now that
    // it has; one that still matches nothing stays on screen, not wiped.
    quint32 peerSourceId = link.peerSourceId;
    if (peerSourceId == 0 && !typed.trimmed().isEmpty()) {
        peerSourceId = hubPeerIdForText(typed);
        if (peerSourceId == 0) {
            combo->setCurrentText(typed);
            return;
        }
        m_links[row].peerSourceId = peerSourceId;
        emit linksChanged();
    }
    const int index = combo->findData(peerSourceId);
    if (peerSourceId != 0 && index >= 0) {
        combo->setCurrentIndex(index);
    } else {
        combo->setCurrentText(hexSourceId(peerSourceId));
    }
}

quint32 DeviceLinksTable::hubPeerIdForText(const QString& text) const {
    const quint32 typedId = parseSourceId(text);
    if (typedId != 0) {
        return typedId;
    }
    // A typed name: resolved against the robots the hub has reported, and
    // only when exactly one of them has it -- guessing between two would save
    // the wrong robot's id.
    const QString wanted = text.trimmed();
    quint32 found = 0;
    for (const HubPeer& peer : m_peers) {
        if (wanted.isEmpty() || peer.name.compare(wanted, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (found != 0 && found != peer.sourceId) {
            return 0;
        }
        found = peer.sourceId;
    }
    return found;
}

void DeviceLinksTable::setAvailablePorts(const QVector<SerialPortOption>& ports) {
    m_ports = ports;
    for (int row = 0; row < m_links.size(); ++row) {
        if (m_links.at(row).transportType == TransportType::Serial) {
            fillTargetCombo(row);
        }
    }
}

void DeviceLinksTable::setAvailableUsbDevices(const QVector<UsbDeviceOption>& devices) {
    m_usbDevices = devices;
    for (int row = 0; row < m_links.size(); ++row) {
        if (m_links.at(row).transportType == TransportType::UsbHid) {
            fillTargetCombo(row);
        }
    }
}

void DeviceLinksTable::setAvailableParentDevices(const QVector<QPair<QString, QString>>& parents) {
    m_parents = parents;
    for (int row = 0; row < m_links.size(); ++row) {
        if (m_links.at(row).transportType == TransportType::HubChannel) {
            fillTargetCombo(row);
        }
    }
}

void DeviceLinksTable::addDiscoveredBleDevice(const QString& name, const QString& address) {
    if (address.isEmpty()) {
        return;
    }
    bool known = false;
    for (auto& seen : m_bleDevices) {
        if (seen.second != address) {
            continue;
        }
        // Most backends re-emit per advertisement: only a newly learned name
        // (it can arrive on a later advertisement) is worth a refill.
        if (name.isEmpty() || seen.first == name) {
            return;
        }
        seen.first = name;
        known = true;
        break;
    }
    if (!known) {
        m_bleDevices.append({name, address});
    }
    for (int row = 0; row < m_links.size(); ++row) {
        if (m_links.at(row).transportType == TransportType::Ble) {
            fillTargetCombo(row);
        }
    }
}

QString DeviceLinksTable::hubParentForPeers() const {
    const int selected = currentRow();
    if (selected >= 0 && m_links.at(selected).transportType == TransportType::HubChannel &&
        !m_links.at(selected).parentDeviceId.isEmpty()) {
        return m_links.at(selected).parentDeviceId;
    }
    for (const DeviceLink& link : m_links) {
        if (link.transportType == TransportType::HubChannel && !link.parentDeviceId.isEmpty()) {
            return link.parentDeviceId;
        }
    }
    // An automatic hub link with no hub chosen searches them all; list the
    // first one's robots so their names can be offered.
    for (const DeviceLink& link : m_links) {
        if (link.transportType == TransportType::HubChannel && !m_parents.isEmpty()) {
            return m_parents.first().first;
        }
    }
    return QString();
}

void DeviceLinksTable::setLinks(const QVector<DeviceLink>& links) {
    const int row = currentRow();
    m_links = links;
    rebuildTable();
    if (row >= 0 && row < m_links.size()) {
        m_table->selectRow(row);
    }
}

void DeviceLinksTable::setAvailableHubPeers(const QVector<HubPeer>& peers) {
    m_peersParent = hubParentForPeers();
    m_peers = peers;
    for (int row = 0; row < m_links.size(); ++row) {
        fillHubPeerCombo(row);
    }
}

void DeviceLinksTable::stopBleScan() {
    if (m_scanBleButton->isChecked()) {
        m_scanBleButton->setChecked(false);
    }
}

}  // namespace traceview
