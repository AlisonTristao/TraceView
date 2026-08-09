#include "serialmanager.h"

#include <QSerialPortInfo>

namespace traceview {

SerialManager::SerialManager(QObject* parent) : QObject(parent), m_port(new QSerialPort(this)) {
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

    m_port->setPortName(portName);
    m_port->setBaudRate(baudRate);
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

    emit connectionStateChanged(true);
    return true;
}

void SerialManager::close() {
    if (!m_port->isOpen()) {
        return;
    }
    m_port->close();
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
    if (error == QSerialPort::ResourceError && m_port->isOpen()) {
        m_port->close();
        emit connectionStateChanged(false);
    }
    emit errorOccurred(message);
}

} // namespace traceview
