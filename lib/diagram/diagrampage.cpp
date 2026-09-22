#include "diagrampage.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QVBoxLayout>

#include "diagramblockconfigdialog.h"
#include "diagramblockitem.h"
#include "diagramconnectionitem.h"
#include "diagramport.h"
#include "diagramportitem.h"
#include "diagramscene.h"
#include "diagramscriptruntime.h"
#include "diagramview.h"
#include "theme/dialogpresenter.h"

namespace traceview {

DiagramPage::DiagramPage(QWidget* parent) : QWidget(parent) {
    m_scene = new DiagramScene(this);
    m_view = new DiagramView(m_scene, this);
    connect(m_scene, &DiagramScene::blockActivated, this, &DiagramPage::onBlockActivated);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view, /*stretch=*/1);
}

void DiagramPage::setDevices(const QVector<Device>& devices) {
    m_devices = devices;

    QSet<QString> currentIds;
    for (const Device& device : devices) {
        currentIds.insert(device.id);
    }
    for (DiagramBlockItem* block : m_scene->blocks()) {
        if (!currentIds.contains(block->deviceId())) {
            m_scene->removeBlock(block->deviceId());
            m_scripts.remove(block->deviceId());
            delete m_runtimes.take(block->deviceId());
        }
    }
    for (const Device& device : devices) {
        if (DiagramBlockItem* block = m_scene->blockFor(device.id)) {
            block->setLabel(device.name);
            block->setLinkState(deviceLinkState(device));
        } else {
            addBlockForDevice(device);
        }
    }
}

void DiagramPage::addBlockForDevice(const Device& device) {
    // Simple staggered grid so successive blocks don't stack on top of each
    // other; the operator is free to drag them anywhere afterward.
    constexpr qreal kColumnWidth = 220.0;
    constexpr qreal kRowHeight = 150.0;
    constexpr int kColumns = 4;
    const QPointF pos((m_placedCount % kColumns) * kColumnWidth,
                      (m_placedCount / kColumns) * kRowHeight);
    ++m_placedCount;

    DiagramBlockItem* block = m_scene->addBlock(device.id, device.name, pos);
    block->setLinkState(deviceLinkState(device));
    m_view->ensureVisible(block);

    auto* runtime = new DiagramScriptRuntime(this);
    m_runtimes.insert(device.id, runtime);
    const QString deviceId = device.id;
    connect(runtime, &DiagramScriptRuntime::sendCommandRequested, this,
            [this, deviceId](const QString& text) { emit commandRequested(deviceId, text); });
    connect(runtime, &DiagramScriptRuntime::sendTerminalRequested, this,
            [this, deviceId](const QString& text) { emit terminalInRequested(deviceId, text); });
}

void DiagramPage::feedTelemetry(const QString& deviceId, quint16 topicId, quint16 fieldId,
                                quint16 elementIndex, double value, quint64 timestampUs) {
    if (DiagramScriptRuntime* runtime = m_runtimes.value(deviceId)) {
        runtime->handleTelemetry(topicId, fieldId, elementIndex, value, timestampUs);
    }
}

void DiagramPage::feedTerminal(const QString& deviceId, const QString& text) {
    if (DiagramScriptRuntime* runtime = m_runtimes.value(deviceId)) {
        runtime->handleTerminal(text);
    }
}

QJsonObject DiagramPage::toJson() const {
    QJsonObject object;

    QJsonArray blocks;
    for (DiagramBlockItem* block : m_scene->blocks()) {
        QJsonObject b;
        b["deviceId"] = block->deviceId();
        b["x"] = block->pos().x();
        b["y"] = block->pos().y();
        b["script"] = m_scripts.value(block->deviceId());
        blocks.append(b);
    }
    object["blocks"] = blocks;

    QJsonArray connections;
    for (DiagramConnectionItem* connection : m_scene->connections()) {
        auto* fromBlock = dynamic_cast<DiagramBlockItem*>(connection->fromPort()->parentItem());
        auto* toBlock = dynamic_cast<DiagramBlockItem*>(connection->toPort()->parentItem());
        if (fromBlock == nullptr || toBlock == nullptr) {
            continue;
        }
        QJsonObject c;
        c["fromDeviceId"] = fromBlock->deviceId();
        c["toDeviceId"] = toBlock->deviceId();
        c["kind"] = QString::fromLatin1(diagramPortKindLabel(connection->fromPort()->kind()));
        connections.append(c);
    }
    object["connections"] = connections;

    return object;
}

void DiagramPage::fromJson(const QJsonObject& object) {
    const QJsonArray blocks = object.value("blocks").toArray();
    for (const QJsonValue& value : blocks) {
        const QJsonObject b = value.toObject();
        const QString deviceId = b.value("deviceId").toString();
        DiagramBlockItem* block = m_scene->blockFor(deviceId);
        if (block == nullptr) {
            continue;
        }
        block->setPos(b.value("x").toDouble(), b.value("y").toDouble());

        const QString script = b.value("script").toString();
        m_scripts.insert(deviceId, script);
        if (DiagramScriptRuntime* runtime = m_runtimes.value(deviceId)) {
            runtime->setScript(script);
        }
    }

    const QJsonArray connections = object.value("connections").toArray();
    for (const QJsonValue& value : connections) {
        const QJsonObject c = value.toObject();
        DiagramPortKind kind;
        if (!diagramPortKindFromLabel(c.value("kind").toString(), &kind)) {
            continue;
        }
        m_scene->addConnection(c.value("fromDeviceId").toString(), kind,
                              c.value("toDeviceId").toString());
    }
}

void DiagramPage::onBlockActivated(const QString& deviceId) {
    DiagramBlockItem* block = m_scene->blockFor(deviceId);
    DiagramScriptRuntime* runtime = m_runtimes.value(deviceId);
    if (block == nullptr || runtime == nullptr) {
        return;
    }
    DiagramBlockConfigDialog dialog(block->label(), m_scripts.value(deviceId), runtime, this);
    if (DialogPresenter::exec(dialog, DialogPresenter::Style::Page) == QDialog::Accepted) {
        m_scripts.insert(deviceId, dialog.script());
    }
}

}  // namespace traceview
