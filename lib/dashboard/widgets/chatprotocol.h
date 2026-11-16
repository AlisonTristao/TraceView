#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// The text protocol ChatWidget speaks with a chat device (an ESP32 radio
// node, see docs/DASHBOARD.md "Chat"). Kept free of any widget so it can be
// unit-tested on its own (tests/test_chatprotocol.cpp).
//
// Sending is a TinyShell command line, "<sendCommand> <token>, <user>, <text>".
// Receiving is a UTF8 topic whose every sample is the device's recent event
// log: up to 8 lines, oldest first, each
// "<event_id>\t<kind>\t<token>\t<author>\t<text>".

struct ChatEvent {
    enum class Kind { Incoming, Sent, Failed, Error, Unknown };

    quint32 id = 0;
    Kind kind = Kind::Unknown;
    QString kindText;  // as received, for diagnostics
    QString token;
    QString author;
    QString text;
};

// Escapes one TinyShell argument: TinyShell splits arguments on unescaped
// commas and turns "\," into "," and "\\" into "\" (TinyShell.cpp,
// unescape_in_place()), so every backslash is doubled and every comma is
// preceded by a backslash. Tabs and newlines (which the device strips
// anyway, and which would break the line) become spaces.
QString escapeTinyShellArgument(const QString& text);

// "<sendCommand> <token>, <userName escaped>, <text escaped>".
QString buildChatSendCommand(const QString& sendCommand, quint32 token, const QString& userName,
                             const QString& text);

// Parses one event line. False for a line that is not an event (fewer than
// five tab-separated fields, or a non-numeric id). An unknown kind still
// parses, as Kind::Unknown, so the caller can advance past its id.
bool parseChatEventLine(const QString& line, ChatEvent* out);

// Every event line of one sample, in order; malformed lines are skipped.
QVector<ChatEvent> parseChatEventLog(const QString& text);

// Remembers the highest event id already handled for one device, so the
// same log -- re-sent with every sample -- yields each event once.
class ChatEventTracker {
public:
    // How far below the remembered id the first line of a sample may start
    // before it reads as a device reboot (ids restart at each boot).
    static constexpr quint32 kRebootSlack = 8;

    // The events of `events` (one sample, oldest first) not handled yet.
    // Lines with an id above the remembered one are new. When the sample's
    // first id is more than kRebootSlack below the remembered one the device
    // has rebooted: the memory is reset and every line is new.
    QVector<ChatEvent> takeNew(const QVector<ChatEvent>& events);

    void reset() {
        m_hasLast = false;
        m_lastId = 0;
    }
    bool hasLast() const {
        return m_hasLast;
    }
    quint32 lastId() const {
        return m_lastId;
    }

private:
    bool m_hasLast = false;
    quint32 m_lastId = 0;
};

}  // namespace traceview
