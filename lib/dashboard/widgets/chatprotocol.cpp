#include "chatprotocol.h"

#include <QStringList>

namespace traceview {

QString escapeTinyShellArgument(const QString& text) {
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        if (c == QLatin1Char('\\')) {
            out += QLatin1String("\\\\");
        } else if (c == QLatin1Char(',')) {
            out += QLatin1String("\\,");
        } else if (c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            out += QLatin1Char(' ');
        } else {
            out += c;
        }
    }
    return out;
}

QString buildChatSendCommand(const QString& sendCommand, quint32 token, const QString& userName,
                             const QString& text) {
    return QStringLiteral("%1 %2, %3, %4")
        .arg(sendCommand.trimmed(), QString::number(token), escapeTinyShellArgument(userName),
             escapeTinyShellArgument(text));
}

bool parseChatEventLine(const QString& rawLine, ChatEvent* out) {
    QString line = rawLine;
    while (line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    const QStringList parts = line.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
    if (parts.size() < 5) {
        return false;
    }
    bool ok = false;
    const quint32 id = parts.at(0).trimmed().toUInt(&ok, 10);
    if (!ok) {
        return false;
    }
    ChatEvent event;
    event.id = id;
    event.kindText = parts.at(1).trimmed();
    const QString kind = event.kindText.toUpper();
    if (kind == QLatin1String("IN")) {
        event.kind = ChatEvent::Kind::Incoming;
    } else if (kind == QLatin1String("SENT")) {
        event.kind = ChatEvent::Kind::Sent;
    } else if (kind == QLatin1String("FAIL")) {
        event.kind = ChatEvent::Kind::Failed;
    } else if (kind == QLatin1String("ERR")) {
        event.kind = ChatEvent::Kind::Error;
    }
    event.token = parts.at(2).trimmed();
    event.author = parts.at(3);
    // The device strips tabs from text, but should one slip through, keep
    // the rest of the line rather than dropping it.
    event.text = parts.mid(4).join(QLatin1Char(' '));
    *out = event;
    return true;
}

QVector<ChatEvent> parseChatEventLog(const QString& text) {
    QVector<ChatEvent> events;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        ChatEvent event;
        if (parseChatEventLine(line, &event)) {
            events.append(event);
        }
    }
    return events;
}

QVector<ChatEvent> ChatEventTracker::takeNew(const QVector<ChatEvent>& events) {
    if (events.isEmpty()) {
        return {};
    }
    if (m_hasLast && events.first().id + kRebootSlack < m_lastId) {
        reset();
    }
    QVector<ChatEvent> fresh;
    for (const ChatEvent& event : events) {
        if (m_hasLast && event.id <= m_lastId) {
            continue;
        }
        fresh.append(event);
        m_lastId = m_hasLast ? qMax(m_lastId, event.id) : event.id;
        m_hasLast = true;
    }
    return fresh;
}

}  // namespace traceview
