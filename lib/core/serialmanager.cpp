#include "serialmanager.h"

#include <QElapsedTimer>
#include <QSerialPortInfo>

namespace traceview {

QByteArray lineTerminatorBytes(LineTerminator terminator) {
    switch (terminator) {
        case LineTerminator::None:
            return QByteArray();
        case LineTerminator::Lf:
            return QByteArray("\n");
        case LineTerminator::Cr:
            return QByteArray("\r");
        case LineTerminator::CrLf:
            return QByteArray("\r\n");
    }
    return QByteArray();
}

SerialManager::SerialManager(QObject* parent) : Transport(parent), m_port(new QSerialPort(this)) {
    connect(m_port, &QSerialPort::readyRead, this, &SerialManager::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialManager::onErrorOccurred);
}

QStringList SerialManager::availablePorts() const {
    QStringList names;
    const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
    names.reserve(infos.size());
    for (const QSerialPortInfo& info : infos) {
        names.append(info.portName());
    }
    return names;
}

bool SerialManager::open(const QString& portName, qint32 baudRate) {
    close();

    // 1200 baud is not a data rate on the dongle's native USB-CDC -- it is the
    // shortcut esptool uses to drop the ESP32-S3 into its ROM bootloader
    // (arduino-esp32 USBCDC::_onLineCoding reboots on bit_rate == 1200). A
    // device configured at 1200 would bootloader-loop on every connect, so
    // fold it up to a working rate. The rate is otherwise cosmetic on a CDC
    // ACM link (the USB stack ignores it), which is why silently substituting
    // one is safe here.
    qint32 effectiveBaud = baudRate;
    if (effectiveBaud == 1200) {
        effectiveBaud = 115200;
        emit errorOccurred(
            tr("1200 baud resets the ESP32-S3 into its bootloader; using 115200"));
    }

    m_port->setPortName(portName);
    m_port->setBaudRate(effectiveBaud);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        // QSerialPort::open() failure already raised its own errorOccurred
        // signal internally (forwarded by onErrorOccurred below) -- emitting
        // here too would double-report the same failure.
        return false;
    }

    // The dongle's native USB-CDC (ARDUINO_USB_MODE=0) only transmits while the
    // host asserts DTR: TinyUSB's tud_cdc_n_connected() tests the DTR bit
    // alone, and arduino-esp32's USBCDC::write() returns 0 -- silently dropping
    // every byte, "BTP/1 READY" included -- until it is set. QSerialPort's
    // default DTR/RTS state after open() varies by platform and Qt version, so
    // a dongle that "sometimes answers and sometimes stays mute" is this being
    // left to chance. Assert both explicitly, once, and never toggle them
    // afterwards. (This board has no USB-serial bridge and so no DTR/RTS-driven
    // reset -- driving these lines here is safe; the only line-driven reset it
    // has is the 1200-baud touch, ruled out just above.) Both-true is a no-op
    // when the platform already asserted them.
    m_port->setDataTerminalReady(true);
    m_port->setRequestToSend(true);

    emit connectionStateChanged(true);
    return true;
}

void SerialManager::close() {
    if (!m_port->isOpen() || m_closing) {
        return;
    }
    m_closing = true;
    // Drop both control lines before closing so the dongle's CDC registers a
    // clean disconnect now (its write() gates on DTR) instead of inferring one
    // later from silence, and so the next open() starts from a known
    // both-low state.
    //
    // Order matters: RTS first, then DTR. The CDC's line-state machine only
    // leaves idle on the `!dtr && rts` transition -- dropping DTR while RTS is
    // still high would step it toward the reset sequence and, if the driver
    // then fails to drop RTS on close, strand it a step in (which shows up as
    // a later open() that can never mark the port connected). Lowering RTS
    // first keeps every transition here as `!rts`, which the machine ignores.
    m_port->setRequestToSend(false);
    m_port->setDataTerminalReady(false);
    m_port->close();
    m_closing = false;
    emit connectionStateChanged(false);
}

bool SerialManager::isConnected() const {
    return m_port->isOpen();
}

QString SerialManager::portName() const {
    return m_port->portName();
}

qint32 SerialManager::baudRate() const {
    return m_port->baudRate();
}

bool SerialManager::write(const QByteArray& data) {
    if (!m_port->isOpen()) {
        return false;
    }
    return m_port->write(data) != -1;
}

bool SerialManager::drainWrites(int timeoutMs) {
    if (!m_port->isOpen() || timeoutMs < 0 || !m_port->flush()) {
        return false;
    }

    QElapsedTimer deadline;
    deadline.start();
    while (m_port->bytesToWrite() > 0) {
        const int remainingMs = timeoutMs - int(deadline.elapsed());
        if (remainingMs <= 0 || !m_port->waitForBytesWritten(remainingMs)) {
            return m_port->isOpen() && m_port->bytesToWrite() == 0;
        }
    }
    return true;
}

bool SerialManager::writeCommand(const QByteArray& command) {
    return write(command + lineTerminatorBytes(m_lineTerminator));
}

void SerialManager::onReadyRead() {
    emit dataReceived(m_port->readAll());
}

void SerialManager::onErrorOccurred(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError) {
        return;
    }
    const QString message = m_port->errorString();
    // A device yanked mid-session (unplugged, driver reset) surfaces as
    // ResourceError without QSerialPort closing itself -- do that here so
    // isConnected()/connectionStateChanged() stay truthful.
    if (error == QSerialPort::ResourceError && m_port->isOpen() && !m_closing) {
        // Use the normal close path so RTS/DTR are lowered in the safe order;
        // bypassing it could leave the native CDC line-state machine halfway
        // through its reset sequence and poison the next open().
        close();
    }
    emit errorOccurred(message);
}

}  // namespace traceview
