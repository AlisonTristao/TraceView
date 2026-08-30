#include "diagnostics/btpmonitortab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStringList>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <btp/codec.hpp>

#include "diagnostics/framelog.h"
#include "diagnostics/hexdump.h"
#include "protocol/channelseal.h"
#include "protocol/keyderivation.h"

namespace traceview {

namespace {

QString messageTypeLabel(btp::MessageType type) {
    switch (type) {
        case btp::MessageType::Telemetry:
            return QStringLiteral("TELEMETRY");
        case btp::MessageType::Log:
            return QStringLiteral("LOG");
        case btp::MessageType::Command:
            return QStringLiteral("COMMAND");
        case btp::MessageType::Terminal:
            return QStringLiteral("TERMINAL");
        case btp::MessageType::Control:
            return QStringLiteral("CONTROL");
        case btp::MessageType::Invalid:
            break;
    }
    return QStringLiteral("INVALID");
}

QString cipherLabel(quint16 flags) {
    switch (btp::cipher_id(flags)) {
        case btp::CipherId::AesGcm:
            return QStringLiteral("AES-128-GCM");
        case btp::CipherId::ChaCha20Poly1305:
            return QStringLiteral("ChaCha20-Poly1305");
    }
    return QStringLiteral("cipher %1").arg((flags & btp::kCipherIdMask) >> 2);
}

QString flagsLabel(quint16 flags) {
    QStringList parts;
    if (flags & btp::kFlagEncrypted) {
        parts << QStringLiteral("ENC");
    }
    if (flags & btp::kFlagFragmented) {
        parts << QStringLiteral("FRAG");
    }
    return parts.join(QLatin1Char('+'));
}

QString hex16(quint16 v) {
    return QStringLiteral("0x%1").arg(v, 4, 16, QChar('0'));
}
QString hex32(quint32 v) {
    return QStringLiteral("0x%1").arg(v, 8, 16, QChar('0'));
}

// The payload's printable content -- exactly what a hex dump shows inside the
// `|...|` column: an ASCII byte as itself, anything else as '.'. No offsets,
// no hex, no escaping. This is the default view, in the table and in the
// detail panel; the full hex + header numbers are behind the "Raw view" toggle.
QString asciiView(const QByteArray& data) {
    QString out;
    out.reserve(data.size());
    for (char c : data) {
        const auto u = static_cast<unsigned char>(c);
        out += (u >= 0x20 && u < 0x7F) ? QChar(ushort(u)) : QChar(u'.');
    }
    return out;
}

// What the table's Payload column shows: the ascii view, capped; a short note
// for a sealed payload (no readable form until opened) or a decode error.
QString payloadColumnText(const FrameLogEntry& e) {
    if (e.decodeError) {
        return e.errorText;
    }
    const BtpFrame& f = e.frame;
    if (f.flags & btp::kFlagEncrypted) {
        return QObject::tr("(encrypted, %1 octets)").arg(f.payload.size());
    }
    if (f.payload.isEmpty()) {
        return QString();
    }
    const QString text = asciiView(f.payload);
    return text.size() > 96 ? text.left(96) + QStringLiteral("…") : text;
}

QString composeSearchText(const FrameLogEntry& e) {
    if (e.decodeError) {
        return e.errorText;
    }
    return messageTypeLabel(e.frame.type) + QLatin1Char(' ') + hex16(e.frame.objectId) +
           QLatin1Char(' ') + hex32(e.frame.sourceId) + QLatin1Char(' ') +
           hexInline(e.frame.payload, 64);
}

// Detail panel, default: just the payload's printable content.
QString frameContentView(const FrameLogEntry& e) {
    if (e.decodeError) {
        return QObject::tr("decode error: %1").arg(e.errorText);
    }
    const BtpFrame& f = e.frame;
    if (f.flags & btp::kFlagEncrypted) {
        return QObject::tr("(encrypted, %1 octets — open it with the Decrypt panel →)")
            .arg(f.payload.size());
    }
    if (f.payload.isEmpty()) {
        return QObject::tr("(no payload)");
    }
    return asciiView(f.payload);
}

// Detail panel, "Raw view" on: the header fields and the full hex dump.
QString frameRawView(const FrameLogEntry& e) {
    if (e.decodeError) {
        return QObject::tr("decode error: %1").arg(e.errorText);
    }
    const BtpFrame& f = e.frame;
    QString out;
    out += QObject::tr("type          %1\n").arg(messageTypeLabel(f.type));
    out += QObject::tr("direction     %1\n")
               .arg(e.direction == FrameDirection::Outbound ? QStringLiteral("TX (sent)")
                                                            : QStringLiteral("RX (received)"));
    out += QObject::tr("flags         0x%1  %2\n")
               .arg(f.flags, 4, 16, QChar('0'))
               .arg(flagsLabel(f.flags).isEmpty() ? QStringLiteral("-") : flagsLabel(f.flags));
    if (f.flags & btp::kFlagEncrypted) {
        out += QObject::tr("cipher        %1\n").arg(cipherLabel(f.flags));
    }
    out += QObject::tr("source_id     %1\n").arg(hex32(f.sourceId));
    out += QObject::tr("boot_id       %1\n").arg(hex32(f.bootId));
    out += QObject::tr("sequence      %1\n").arg(f.sequence);
    out += QObject::tr("timestamp_us  %1\n").arg(f.timestampUs);
    out += QObject::tr("object_id     %1\n").arg(hex16(f.objectId));
    out += QObject::tr("fragment      %1 / %2\n").arg(f.fragmentIndex).arg(f.fragmentCount);
    out += QObject::tr("payload       %1 octet(s)\n\n").arg(f.payload.size());
    out += hexDump(f.payload);
    return out;
}

btp::Header headerFromFrame(const BtpFrame& f) {
    btp::Header header{};
    header.type = f.type;
    header.flags = f.flags;
    header.source_id = f.sourceId;
    header.boot_id = f.bootId;
    header.sequence = f.sequence;
    header.timestamp_us = f.timestampUs;
    header.object_id = f.objectId;
    header.fragment_index = f.fragmentIndex;
    header.fragment_count = f.fragmentCount;
    return header;
}

}  // namespace

// ---------------------------------------------------------------------------
// FrameTableModel
// ---------------------------------------------------------------------------

FrameTableModel::FrameTableModel(FrameLog* log, QObject* parent)
    : QAbstractTableModel(parent), m_log(log) {
    m_all = m_log->entries();
    rebuildVisible();
    for (const FrameLogEntry& e : m_all) {
        m_seenDevices.insert(e.deviceName);
    }
    connect(m_log, &FrameLog::entryAdded, this, &FrameTableModel::onEntryAdded);
    connect(m_log, &FrameLog::cleared, this, [this] {
        beginResetModel();
        m_all.clear();
        m_visible.clear();
        endResetModel();
    });
}

int FrameTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_visible.size();
}

int FrameTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

const FrameLogEntry* FrameTableModel::entryForRow(int row) const {
    if (row < 0 || row >= m_visible.size()) {
        return nullptr;
    }
    return &m_all.at(m_visible.at(row));
}

QVariant FrameTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole)) {
        return {};
    }
    const FrameLogEntry* entry = entryForRow(index.row());
    if (entry == nullptr) {
        return {};
    }
    const FrameLogEntry& e = *entry;

    if (role == Qt::ToolTipRole) {
        return index.column() == PayloadColumn && !e.decodeError ? hexInline(e.frame.payload, 512)
                                                                 : QVariant();
    }

    switch (index.column()) {
        case SeqColumn:
            return QString::number(e.seq);
        case TimeColumn:
            return e.wallClock.toString(QStringLiteral("HH:mm:ss.zzz"));
        case DirectionColumn:
            if (e.decodeError) {
                return QStringLiteral("✖ ERR");
            }
            return e.direction == FrameDirection::Outbound ? QStringLiteral("▲ TX")
                                                           : QStringLiteral("▼ RX");
        case DeviceColumn:
            return e.deviceName;
        case TypeColumn:
            return e.decodeError ? tr("decode error") : messageTypeLabel(e.frame.type);
        case ObjectColumn:
            return e.decodeError ? QString() : hex16(e.frame.objectId);
        case SequenceColumn:
            return e.decodeError ? QString() : QString::number(e.frame.sequence);
        case FlagsColumn:
            return e.decodeError ? QString() : flagsLabel(e.frame.flags);
        case LengthColumn:
            return e.decodeError ? QString() : QString::number(e.frame.payload.size());
        case PayloadColumn:
            return payloadColumnText(e);
        default:
            return {};
    }
}

QVariant FrameTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
        case SeqColumn:
            return QStringLiteral("#");
        case TimeColumn:
            return tr("Time");
        case DirectionColumn:
            return tr("Dir");
        case DeviceColumn:
            return tr("Device");
        case TypeColumn:
            return tr("Type");
        case ObjectColumn:
            return tr("Object");
        case SequenceColumn:
            return tr("Seq");
        case FlagsColumn:
            return tr("Flags");
        case LengthColumn:
            return tr("Len");
        case PayloadColumn:
            return tr("Payload");
        default:
            return {};
    }
}

bool FrameTableModel::rowPasses(const FrameLogEntry& e) const {
    if (m_telemetryHidden && !e.decodeError && e.frame.type == btp::MessageType::Telemetry) {
        return false;
    }
    if (!m_deviceFilter.isEmpty() && e.deviceName != m_deviceFilter) {
        return false;
    }
    if (!m_textFilter.isEmpty() &&
        !composeSearchText(e).contains(m_textFilter, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

void FrameTableModel::rebuildVisible() {
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (rowPasses(m_all.at(i))) {
            m_visible.append(i);
        }
    }
    endResetModel();
}

void FrameTableModel::syncFromLog() {
    m_all = m_log->entries();
    for (const FrameLogEntry& e : m_all) {
        if (!m_seenDevices.contains(e.deviceName)) {
            m_seenDevices.insert(e.deviceName);
            emit deviceSeen(e.deviceName);
        }
    }
    rebuildVisible();
}

void FrameTableModel::onEntryAdded(const FrameLogEntry& entry, bool droppedOldest) {
    if (!m_seenDevices.contains(entry.deviceName)) {
        m_seenDevices.insert(entry.deviceName);
        emit deviceSeen(entry.deviceName);
    }
    if (m_paused) {
        return;
    }

    if (droppedOldest && !m_all.isEmpty()) {
        m_all.removeFirst();
        if (!m_visible.isEmpty() && m_visible.first() == 0) {
            beginRemoveRows({}, 0, 0);
            m_visible.removeFirst();
            for (int& idx : m_visible) {
                --idx;
            }
            endRemoveRows();
        } else {
            for (int& idx : m_visible) {
                --idx;
            }
        }
    }

    m_all.append(entry);
    if (rowPasses(entry)) {
        const int newRow = m_visible.size();
        beginInsertRows({}, newRow, newRow);
        m_visible.append(m_all.size() - 1);
        endInsertRows();
    }
}

void FrameTableModel::setTelemetryHidden(bool hidden) {
    if (hidden == m_telemetryHidden) {
        return;
    }
    m_telemetryHidden = hidden;
    rebuildVisible();
}

void FrameTableModel::setDeviceFilter(const QString& deviceName) {
    if (deviceName == m_deviceFilter) {
        return;
    }
    m_deviceFilter = deviceName;
    rebuildVisible();
}

void FrameTableModel::setTextFilter(const QString& text) {
    if (text == m_textFilter) {
        return;
    }
    m_textFilter = text;
    rebuildVisible();
}

void FrameTableModel::setPaused(bool paused) {
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (!paused) {
        syncFromLog();
    }
}

// ---------------------------------------------------------------------------
// BtpMonitorTab
// ---------------------------------------------------------------------------

BtpMonitorTab::BtpMonitorTab(FrameLog* log, PasswordProvider passwordProvider, QWidget* parent)
    : QWidget(parent), m_log(log), m_passwordProvider(std::move(passwordProvider)) {
    auto* layout = new QVBoxLayout(this);

    // --- toolbar -----------------------------------------------------------
    auto* toolbar = new QHBoxLayout;
    m_pauseButton = new QPushButton(tr("Pause"), this);
    m_pauseButton->setCheckable(true);
    toolbar->addWidget(m_pauseButton);

    auto* clearButton = new QPushButton(tr("Clear"), this);
    toolbar->addWidget(clearButton);

    m_hideTelemetry = new QCheckBox(tr("Hide TELEMETRY"), this);
    toolbar->addWidget(m_hideTelemetry);

    // Off by default: the plain view drops the raw protocol numbers (table
    // columns and the detail panel's header + hex), leaving just the readable
    // payload content.
    m_rawView = new QCheckBox(tr("Raw view"), this);
    toolbar->addWidget(m_rawView);

    toolbar->addWidget(new QLabel(tr("Device:"), this));
    m_deviceFilter = new QComboBox(this);
    m_deviceFilter->addItem(tr("All"), QString());
    toolbar->addWidget(m_deviceFilter);

    m_textFilter = new QLineEdit(this);
    m_textFilter->setPlaceholderText(tr("Filter (type, id, hex)…"));
    m_textFilter->setClearButtonEnabled(true);
    toolbar->addWidget(m_textFilter, /*stretch=*/1);

    auto* exportButton = new QPushButton(tr("Export…"), this);
    toolbar->addWidget(exportButton);
    layout->addLayout(toolbar);

    // --- table + detail --------------------------------------------------
    auto* splitter = new QSplitter(Qt::Vertical, this);

    m_model = new FrameTableModel(m_log, this);
    m_table = new QTableView(splitter);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setWordWrap(false);
    // The raw-number columns start hidden -- see the "Show IDs" toggle.
    m_table->setColumnHidden(FrameTableModel::SeqColumn, true);
    m_table->setColumnHidden(FrameTableModel::ObjectColumn, true);
    m_table->setColumnHidden(FrameTableModel::SequenceColumn, true);
    splitter->addWidget(m_table);

    // Bottom half, split 50/50: the message read normally on the left, the
    // Decrypt panel on the right.
    auto* detailSplit = new QSplitter(Qt::Horizontal, splitter);

    m_detail = new QPlainTextEdit(detailSplit);
    m_detail->setReadOnly(true);
    m_detail->setLineWrapMode(QPlainTextEdit::WidgetWidth);  // flipped to NoWrap in Raw view
    m_detail->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    detailSplit->addWidget(m_detail);

    auto* decryptBox = new QGroupBox(tr("Decrypt (channel B / key E)"), detailSplit);
    auto* decryptLayout = new QVBoxLayout(decryptBox);

    auto* pwRow = new QHBoxLayout;
    m_password = new QLineEdit(decryptBox);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("robot channel-B password"));
    pwRow->addWidget(m_password, /*stretch=*/1);
    m_revealPassword = new QToolButton(decryptBox);
    m_revealPassword->setText(tr("Show"));
    m_revealPassword->setCheckable(true);
    pwRow->addWidget(m_revealPassword);
    decryptLayout->addLayout(pwRow);

    m_decryptButton = new QPushButton(tr("Decrypt selected frame"), decryptBox);
    decryptLayout->addWidget(m_decryptButton);

    m_decryptResult = new QPlainTextEdit(decryptBox);
    m_decryptResult->setReadOnly(true);
    m_decryptResult->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_decryptResult->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    decryptLayout->addWidget(m_decryptResult, /*stretch=*/1);

    detailSplit->addWidget(decryptBox);
    detailSplit->setStretchFactor(0, 1);
    detailSplit->setStretchFactor(1, 1);
    detailSplit->setSizes({10000, 10000});  // 50/50 to start; draggable after

    splitter->addWidget(detailSplit);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, /*stretch=*/1);

    // --- wiring ----------------------------------------------------------
    connect(m_pauseButton, &QPushButton::toggled, this, [this](bool paused) {
        m_model->setPaused(paused);
        m_pauseButton->setText(paused ? tr("Resume") : tr("Pause"));
    });
    connect(clearButton, &QPushButton::clicked, m_log, &FrameLog::clear);
    connect(m_hideTelemetry, &QCheckBox::toggled, m_model,
            &FrameTableModel::setTelemetryHidden);
    connect(m_rawView, &QCheckBox::toggled, this, [this](bool on) {
        m_table->setColumnHidden(FrameTableModel::SeqColumn, !on);
        m_table->setColumnHidden(FrameTableModel::ObjectColumn, !on);
        m_table->setColumnHidden(FrameTableModel::SequenceColumn, !on);
        m_detail->setLineWrapMode(on ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
        onSelectionChanged();  // re-render the current frame in the new mode
    });
    connect(m_deviceFilter, &QComboBox::currentIndexChanged, this, [this] {
        m_model->setDeviceFilter(m_deviceFilter->currentData().toString());
    });
    connect(m_textFilter, &QLineEdit::textChanged, m_model, &FrameTableModel::setTextFilter);
    connect(m_model, &FrameTableModel::deviceSeen, this, &BtpMonitorTab::addDeviceToFilter);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &BtpMonitorTab::onSelectionChanged);
    connect(m_revealPassword, &QToolButton::toggled, this, [this](bool shown) {
        m_password->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(m_decryptButton, &QPushButton::clicked, this, &BtpMonitorTab::onDecryptClicked);
    connect(exportButton, &QPushButton::clicked, this, &BtpMonitorTab::onExportClicked);

    onSelectionChanged();
}

void BtpMonitorTab::addDeviceToFilter(const QString& deviceName) {
    if (deviceName.isEmpty() || m_deviceFilter->findData(deviceName) != -1) {
        return;
    }
    m_deviceFilter->addItem(deviceName, deviceName);
}

void BtpMonitorTab::onSelectionChanged() {
    const QModelIndex current = m_table->selectionModel()->currentIndex();
    const FrameLogEntry* entry = m_model->entryForRow(current.row());
    if (entry == nullptr) {
        m_detail->setPlainText(tr("Select a frame to inspect it."));
        m_decryptButton->setEnabled(false);
        m_decryptResult->clear();
        return;
    }
    m_detail->setPlainText(m_rawView->isChecked() ? frameRawView(*entry)
                                                  : frameContentView(*entry));
    m_decryptResult->clear();

    const bool sealed = !entry->decodeError && (entry->frame.flags & btp::kFlagEncrypted);
    m_decryptButton->setEnabled(!entry->decodeError);

    // Prefill the password from the device's configured channel-B password the
    // first time a frame from that device is selected, unless the user has
    // already typed something.
    if (m_passwordProvider && m_password->text().isEmpty()) {
        const QString configured = m_passwordProvider(entry->deviceId);
        if (!configured.isEmpty()) {
            m_password->setText(configured);
        }
    }
    if (!sealed && !entry->decodeError) {
        m_decryptResult->setPlainText(tr("This frame is not sealed (ENCRYPTED flag not set) — "
                                         "the payload above is already in the clear."));
    }
}

QByteArray BtpMonitorTab::keyForPassword(const QString& password) {
    const auto cached = m_keyCache.constFind(password);
    if (cached != m_keyCache.constEnd()) {
        return cached.value();
    }
    const QByteArray key = deriveChannelKey(password);
    m_keyCache.insert(password, key);
    return key;
}

void BtpMonitorTab::onDecryptClicked() {
    const QModelIndex current = m_table->selectionModel()->currentIndex();
    const FrameLogEntry* entry = m_model->entryForRow(current.row());
    if (entry == nullptr || entry->decodeError) {
        return;
    }
    const QString password = m_password->text();
    if (password.isEmpty()) {
        m_decryptResult->setPlainText(tr("Enter a password first."));
        return;
    }
    const BtpFrame& frame = entry->frame;
    if ((frame.flags & btp::kFlagEncrypted) == 0) {
        m_decryptResult->setPlainText(tr("This frame is not sealed; nothing to decrypt."));
        return;
    }

    const QByteArray key = keyForPassword(password);
    if (key.isEmpty()) {
        m_decryptResult->setPlainText(tr("Could not derive a key from that password."));
        return;
    }

    const btp::Header header = headerFromFrame(frame);
    const std::optional<QByteArray> plaintext = ChannelSeal::open(key, header, frame.payload);

    QString out = tr("verify tag  %1\n\n").arg(QString::fromLatin1(endpointKeyVerifyTag(key).toHex()));
    if (plaintext.has_value()) {
        out += tr("OK — %1 octet(s) of plaintext:\n\n").arg(plaintext->size());
        out += hexDump(*plaintext);
        const QString asText = QString::fromUtf8(*plaintext);
        if (!asText.contains(QChar(QChar::ReplacementCharacter))) {
            out += QLatin1Char('\n') + tr("as text: ") + asText;
        }
    } else {
        out += tr("Authentication failed — wrong password, or this is not channel-B "
                  "(key E) traffic. Compare the verify tag above with the robot's key file.");
    }
    m_decryptResult->setPlainText(out);
}

void BtpMonitorTab::onExportClicked() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Captured Frames"), QStringLiteral("btp_frames.jsonl"),
        tr("JSON Lines (*.jsonl);;CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    const bool csv = path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive);
    if (csv) {
        file.write("seq,time,direction,device,type,object_id,sequence,flags,length,payload_hex\n");
    }

    const auto csvField = [](const QString& value) {
        QString escaped = value;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    };

    for (const FrameLogEntry& e : m_log->entries()) {
        const QString time = e.wallClock.toString(Qt::ISODateWithMs);
        const QString dir = e.decodeError
                                ? QStringLiteral("error")
                                : (e.direction == FrameDirection::Outbound ? QStringLiteral("tx")
                                                                           : QStringLiteral("rx"));
        if (csv) {
            QStringList cells;
            cells << QString::number(e.seq) << csvField(time) << dir << csvField(e.deviceName);
            if (e.decodeError) {
                cells << QStringLiteral("decode_error") << QString() << QString() << QString()
                      << QString() << csvField(e.errorText);
            } else {
                cells << messageTypeLabel(e.frame.type) << hex16(e.frame.objectId)
                      << QString::number(e.frame.sequence) << flagsLabel(e.frame.flags)
                      << QString::number(e.frame.payload.size())
                      << QString::fromLatin1(e.frame.payload.toHex());
            }
            file.write(cells.join(QLatin1Char(',')).toUtf8());
            file.write("\n");
        } else {
            QJsonObject obj;
            obj["seq"] = double(e.seq);
            obj["time"] = time;
            obj["direction"] = dir;
            obj["device"] = e.deviceName;
            if (e.decodeError) {
                obj["error"] = e.errorText;
            } else {
                const BtpFrame& f = e.frame;
                obj["type"] = messageTypeLabel(f.type);
                obj["flags"] = int(f.flags);
                obj["encrypted"] = bool(f.flags & btp::kFlagEncrypted);
                obj["source_id"] = double(f.sourceId);
                obj["boot_id"] = double(f.bootId);
                obj["sequence"] = double(f.sequence);
                obj["timestamp_us"] = double(f.timestampUs);
                obj["object_id"] = int(f.objectId);
                obj["fragment_index"] = int(f.fragmentIndex);
                obj["fragment_count"] = int(f.fragmentCount);
                obj["payload_hex"] = QString::fromLatin1(f.payload.toHex());
            }
            file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            file.write("\n");
        }
    }
    file.commit();
}

}  // namespace traceview
