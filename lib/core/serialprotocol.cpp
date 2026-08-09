#include "serialprotocol.h"

namespace traceview {

namespace {

bool isValidIdByte(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

} // namespace

SerialFrame decodeFrame(const QByteArray& line) {
    SerialFrame frame;

    // `[<time>]`
    if (line.isEmpty() || line[0] != '[') {
        return frame;
    }
    const int timeEnd = line.indexOf(']', 1);
    if (timeEnd <= 1) {
        return frame;
    }
    bool timeOk = false;
    const qint64 time = line.mid(1, timeEnd - 1).toLongLong(&timeOk);
    if (!timeOk || time < 0) {
        return frame;
    }

    // `[<id>]`
    if (timeEnd + 1 >= line.size() || line[timeEnd + 1] != '[') {
        return frame;
    }
    const int idStart = timeEnd + 2;
    const int idEnd = line.indexOf(']', idStart);
    if (idEnd < 0) {
        return frame;
    }
    const QByteArray idBytes = line.mid(idStart, idEnd - idStart);
    if (idBytes.isEmpty() || idBytes.size() > 64) {
        return frame;
    }
    for (const char c : idBytes) {
        if (!isValidIdByte(c)) {
            return frame;
        }
    }

    // ` <payload>`
    const int payloadStart = idEnd + 2;
    if (idEnd + 1 >= line.size() || line[idEnd + 1] != ' ') {
        return frame;
    }

    frame.time = time;
    frame.id = QString::fromLatin1(idBytes);
    frame.payload = line.mid(payloadStart);
    frame.ok = true;
    return frame;
}

QList<QByteArray> SerialLineAssembler::feed(const QByteArray& data) {
    m_buffer.append(data);

    QList<QByteArray> lines;
    int start = 0;
    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n', start)) >= 0) {
        int lineEnd = newlineIndex;
        if (lineEnd > start && m_buffer[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        lines.append(m_buffer.mid(start, lineEnd - start));
        start = newlineIndex + 1;
    }
    m_buffer = m_buffer.mid(start);

    return lines;
}

void SerialLineAssembler::reset() {
    m_buffer.clear();
}

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

} // namespace traceview
