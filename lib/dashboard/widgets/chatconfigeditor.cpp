#include "chatconfigeditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>

namespace traceview {

namespace {

const QLatin1String kDefaultTopicName("chat");
const QLatin1String kDefaultSendCommand("chat -send");
const QLatin1String kUnboundSourceId("0x00000000");
const QLatin1String kUnboundTopicId("0x0000");

}  // namespace

ChatConfigEditor::ChatConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(
        tr("The radio device this chat sends and receives through. (No device) keeps the chat "
           "visual only."));

    m_topicCombo = new QComboBox(this);
    m_topicCombo->setEditable(true);
    m_topicCombo->setInsertPolicy(QComboBox::NoInsert);
    m_topicCombo->lineEdit()->setPlaceholderText(kDefaultTopicName);
    m_topicCombo->setToolTip(tr("Name of the device's text topic carrying its chat event log."));

    m_sendCommandEdit = new QLineEdit(this);
    m_sendCommandEdit->setPlaceholderText(kDefaultSendCommand);
    m_sendCommandEdit->setToolTip(
        tr("Command a message is sent with, as \"<command> <token>, <user>, <text>\"."));

    m_userNameEdit = new QLineEdit(this);
    m_userNameEdit->setPlaceholderText(tr("You"));

    m_ownOnRightCheck = new QCheckBox(tr("Show my messages on the right"), this);
    m_ownOnRightCheck->setChecked(true);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(tr("Device"), m_deviceCombo);
    layout->addRow(tr("Topic name"), m_topicCombo);
    layout->addRow(tr("Send command"), m_sendCommandEdit);
    layout->addRow(tr("User name"), m_userNameEdit);
    layout->addRow(QString(), m_ownOnRightCheck);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating) {
            return;
        }
        dropResolvedIds();
        populateTopics();
        emitChanged();
    });
    const auto topicEdited = [this] {
        const QString name = m_topicCombo->currentText().trimmed();
        if (name == m_boundTopicName) {
            return;  // focus left the field, nothing changed
        }
        m_boundTopicName = name;
        dropResolvedIds();
        emitChanged();
    };
    connect(m_topicCombo->lineEdit(), &QLineEdit::editingFinished, this, topicEdited);
    connect(m_topicCombo, &QComboBox::activated, this, [topicEdited](int) { topicEdited(); });
    connect(m_sendCommandEdit, &QLineEdit::editingFinished, this, &ChatConfigEditor::emitChanged);
    connect(m_userNameEdit, &QLineEdit::editingFinished, this, &ChatConfigEditor::emitChanged);
    connect(m_ownOnRightCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
}

void ChatConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    populateTopics();
    m_boundTopicName = config.value("topicName").toString().trimmed();
    m_topicCombo->setEditText(m_boundTopicName);
    m_sendCommandEdit->setText(config.value("sendCommand").toString());
    m_userNameEdit->setText(config.value("userName").toString());
    m_ownOnRightCheck->setChecked(config.value("ownMessagesOnRight").toBool(true));
    m_sourceIdHex = config.value("sourceId").toString();
    m_topicIdHex = config.value("topicId").toString();
    m_sampleTimeMs = config.value("sampleTimeMs");
    m_updating = false;
}

QJsonObject ChatConfigEditor::config() const {
    QJsonObject config;
    config["userName"] = m_userNameEdit->text().trimmed();
    config["ownMessagesOnRight"] = m_ownOnRightCheck->isChecked();
    const QString deviceId = m_deviceCombo->currentData().toString();
    if (!deviceId.isEmpty()) {
        const QString topicName = m_topicCombo->currentText().trimmed();
        const QString sendCommand = m_sendCommandEdit->text().trimmed();
        config["deviceId"] = deviceId;
        config["topicName"] = topicName.isEmpty() ? QString(kDefaultTopicName) : topicName;
        config["sendCommand"] = sendCommand.isEmpty() ? QString(kDefaultSendCommand) : sendCommand;
        // The ids are a cache the binding resolver fills from topicName
        // (dashboard/bindingresolver.h); "topicId" must be present for it to.
        config["sourceId"] = m_sourceIdHex.isEmpty() ? QString(kUnboundSourceId) : m_sourceIdHex;
        config["topicId"] = m_topicIdHex.isEmpty() ? QString(kUnboundTopicId) : m_topicIdHex;
        if (!m_sampleTimeMs.isUndefined()) {
            config["sampleTimeMs"] = m_sampleTimeMs;
        }
    }
    return config;
}

void ChatConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_devices = devices;
    populateDeviceCombo(m_deviceCombo, devices);
    populateTopics();
    m_updating = wasUpdating;
}

void ChatConfigEditor::populateTopics() {
    // Only text topics carry an event log; the field stays editable for a
    // device whose manifest hasn't arrived yet.
    const QString text = m_topicCombo->currentText();
    const QString deviceId = m_deviceCombo->currentData().toString();
    QStringList names;
    for (const DeviceOption& device : std::as_const(m_devices)) {
        if (device.id != deviceId) {
            continue;
        }
        for (const CatalogTopicInfo& topic : device.catalogTopics) {
            if (topic.encoding.compare(QStringLiteral("UTF8"), Qt::CaseInsensitive) == 0 &&
                !topic.name.isEmpty() && !names.contains(topic.name)) {
                names.append(topic.name);
            }
        }
    }
    const QSignalBlocker blocker(m_topicCombo);
    m_topicCombo->clear();
    m_topicCombo->addItems(names);
    m_topicCombo->setEditText(text);
}

void ChatConfigEditor::dropResolvedIds() {
    m_sourceIdHex.clear();
    m_topicIdHex.clear();
}

void ChatConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

}  // namespace traceview
