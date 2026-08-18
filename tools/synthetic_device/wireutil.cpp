#include "wireutil.h"

#include <cstring>

namespace wireutil {

void appendLe(QByteArray& out, quint32 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void appendF64(QByteArray& out, double value) {
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        out.append(static_cast<char>((bits >> (8 * i)) & 0xFF));
    }
}

void appendF32(QByteArray& out, float value) {
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; ++i) {
        out.append(static_cast<char>((bits >> (8 * i)) & 0xFF));
    }
}

void appendUtf8U16(QByteArray& out, const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    appendLe(out, quint32(utf8.size()), 2);
    out.append(utf8);
}

quint16 readLe16(const QByteArray& data, int offset) {
    return quint16(quint8(data.at(offset))) | (quint16(quint8(data.at(offset + 1))) << 8);
}

quint32 readLe32(const QByteArray& data, int offset) {
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= quint32(quint8(data.at(offset + i))) << (8 * i);
    }
    return value;
}

}  // namespace wireutil
