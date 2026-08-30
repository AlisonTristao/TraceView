#include "diagnostics/hexdump.h"

namespace traceview {

namespace {
constexpr int kBytesPerLine = 16;

QChar printableOrDot(char byte) {
    const auto value = static_cast<unsigned char>(byte);
    return (value >= 0x20 && value < 0x7F) ? QChar(value) : QChar('.');
}
}  // namespace

QString hexDump(const QByteArray& data) {
    if (data.isEmpty()) {
        return QString();
    }

    QString out;
    // 8 (offset) + 2 + 3*16 (hex) + 2 + 1 + 16 + 1 (ascii) + newline per line.
    out.reserve((data.size() / kBytesPerLine + 1) * 78);

    for (int lineStart = 0; lineStart < data.size(); lineStart += kBytesPerLine) {
        out += QStringLiteral("%1  ").arg(lineStart, 8, 16, QChar('0'));

        QString ascii;
        for (int col = 0; col < kBytesPerLine; ++col) {
            const int index = lineStart + col;
            if (index < data.size()) {
                out += QStringLiteral("%1 ").arg(static_cast<unsigned char>(data.at(index)), 2, 16,
                                                 QChar('0'));
                ascii += printableOrDot(data.at(index));
            } else {
                out += QStringLiteral("   ");
            }
            if (col == kBytesPerLine / 2 - 1) {
                out += QChar(' ');
            }
        }
        out += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    return out;
}

QString hexInline(const QByteArray& data, int maxBytes) {
    if (data.isEmpty()) {
        return QString();
    }
    const int shown = maxBytes > 0 ? qMin(maxBytes, data.size()) : data.size();

    QString out;
    out.reserve(shown * 3 + 12);
    for (int i = 0; i < shown; ++i) {
        if (i > 0) {
            out += QChar(' ');
        }
        out += QStringLiteral("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QChar('0'));
    }
    if (shown < data.size()) {
        out += QStringLiteral(" … (+%1)").arg(data.size() - shown);
    }
    return out;
}

}  // namespace traceview
