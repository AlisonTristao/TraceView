#include "serialwidgetbridge.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <btp/codec.hpp>

#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/controlwidgets.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"
#include "serialmanager.h"

namespace traceview {

namespace {

// bally_protocol/docs/COMMANDS_AND_ACTIONS.md section 3.3 -- object_id
// values within MessageType::Terminal. Mirrors t_dongle_develop's
// SerialSession::kTerminalInObjectId/kTerminalOutObjectId (lib/SerialSession/
// SerialSession.h); each side of the wire defines its own copy of these
// normatively-fixed constants, same as topico 13 did on the dongle.
constexpr quint16 kTerminalInObjectId = 0x0001;
constexpr quint16 kTerminalOutObjectId = 0x0002;

quint32 randomNonZero() {
    quint32 value = 0;
    while (value == 0) {
        value = QRandomGenerator::global()->generate();
    }
    return value;
}

} // namespace

SerialWidgetBridge::SerialWidgetBridge(SerialManager* serialManager, BtpSession* btpSession,
                                       ProtocolRouter* protocolRouter, DashboardGrid* grid, QObject* parent)
    : QObject(parent),
      m_serialManager(serialManager),
      m_btpSession(btpSession),
      m_protocolRouter(protocolRouter),
      m_terminalSourceId(randomNonZero()),
      m_terminalBootId(randomNonZero()) {
    connect(grid, &DashboardGrid::widgetCreated, this, &SerialWidgetBridge::wireWidget);
}

void SerialWidgetBridge::wireWidget(DashboardWidget* widget) {
    if (auto* button = qobject_cast<PushButtonWidget*>(widget)) {
        connect(button, &PushButtonWidget::sendRequested, m_serialManager, &SerialManager::writeCommand);
    } else if (auto* toggle = qobject_cast<ToggleSwitchWidget*>(widget)) {
        connect(toggle, &ToggleSwitchWidget::sendRequested, m_serialManager, &SerialManager::writeCommand);
    } else if (auto* slider = qobject_cast<SliderWidget*>(widget)) {
        connect(slider, &SliderWidget::sendRequested, m_serialManager, &SerialManager::writeCommand);
    } else if (auto* monitor = qobject_cast<SerialMonitorWidget*>(widget)) {
        connect(monitor, &SerialMonitorWidget::sendRequested, this, &SerialWidgetBridge::sendTerminalIn);
        connect(m_protocolRouter, &ProtocolRouter::terminalFrameReceived, monitor,
                [monitor](const BtpFrame& frame) {
                    // TERMINAL_IN frames from other TraceView instances/tools
                    // sharing this connection are not this widget's output;
                    // only object_id TERMINAL_OUT is ever rendered.
                    if (frame.objectId == kTerminalOutObjectId) {
                        monitor->appendData(frame.payload);
                    }
                });
    }
}

void SerialWidgetBridge::sendTerminalIn(const QByteArray& bytes) {
    if (bytes.isEmpty() || m_btpSession == nullptr) {
        return;
    }

    btp::Header header{};
    header.type = btp::MessageType::Terminal;
    header.flags = 0;
    header.source_id = m_terminalSourceId;
    header.boot_id = m_terminalBootId;
    header.sequence = ++m_terminalSequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kTerminalInObjectId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                            static_cast<std::size_t>(bytes.size())}};
    m_btpSession->sendFrame(frame);
}

} // namespace traceview
