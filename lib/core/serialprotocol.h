#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace traceview {

// Result of matching a single line (no trailing EOL -- SerialLineAssembler
// strips that before handing lines to decodeFrame) against the envelope
// grammar in docs/PROTOCOL.md: `[<time>][<id>] <payload>`. `ok == false`
// means the line didn't match (missing/misplaced brackets or space,
// non-numeric or negative time, id empty/oversized/outside
// [A-Za-z0-9_-]{1,64}) -- `time`/`id`/`payload` are unspecified in that case.
struct SerialFrame {
    qint64 time = 0;
    QString id;
    QByteArray payload;
    bool ok = false;
};

// Pure function, no knowledge of CSV vs Bytes payload encoding -- that's
// decoded later by the consuming widget (ChartConfigEditor/GaugeConfigEditor
// Format, Tasks 7/8); this only splits the envelope from its opaque payload.
SerialFrame decodeFrame(const QByteArray& line);

// Accumulates raw bytes handed in from SerialManager::dataReceived() (which
// can fragment a single line across multiple emissions) and splits them into
// complete lines on \n or \r\n -- a lone \r is not a terminator (see
// docs/PROTOCOL.md) and stays as a literal byte in whichever line it falls
// in. Stateful but UI-free: safe to unit test with synthetic byte chunks, no
// QObject/event loop involved.
class SerialLineAssembler {
public:
    // Feeds a chunk of raw bytes; returns zero or more complete lines
    // extracted from the accumulated buffer (EOL stripped, in arrival
    // order). Any trailing partial line stays buffered for the next feed().
    QList<QByteArray> feed(const QByteArray& data);

    // Discards any partially-accumulated line (e.g. on a fresh connection).
    void reset();

private:
    QByteArray m_buffer;
};

// Line terminator appended to outbound control-widget commands (docs/
// PROTOCOL.md "Outbound: control commands") -- a single global setting tied
// to the port connection (Run ribbon tab, SerialManager), not per-widget.
// Does not apply to SerialTerminalWidget's raw per-keystroke passthrough,
// which stays unterminated (Task 10).
enum class LineTerminator { None, Lf, Cr, CrLf };

// Byte sequence for `terminator` -- empty for None.
QByteArray lineTerminatorBytes(LineTerminator terminator);

} // namespace traceview
