#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

// Small library of BTP v1 wire-encoding primitives shared by the generic
// device engine (syntheticdevicesession.cpp) and every concrete device
// profile (solarpaneldevice.cpp, weatherstationdevice.cpp, ...): little-endian
// integer/float append, the utf8_u16 string encoding (COMMANDS_AND_ACTIONS.md
// section 2), and the TELEMETRY.md section 5 field type codes. Kept as free
// functions/constants rather than members of any one class, the same way
// lib/protocol/*.cpp each define their own local appendLe() -- this is just
// one shared copy instead of three.
namespace wireutil {

void appendLe(QByteArray& out, quint32 value, int width);
void appendF64(QByteArray& out, double value);
void appendF32(QByteArray& out, float value);
void appendUtf8U16(QByteArray& out, const QString& text);

quint16 readLe16(const QByteArray& data, int offset);
quint32 readLe32(const QByteArray& data, int offset);

// TELEMETRY.md section 5 type codes -- only the ones any device profile here
// actually uses.
constexpr quint8 kFieldTypeUInt8 = 0x01;
constexpr quint8 kFieldTypeUInt16 = 0x02;
constexpr quint8 kFieldTypeUInt32 = 0x03;
constexpr quint8 kFieldTypeFloat32 = 0x09;
constexpr quint8 kFieldTypeEnum8 = 0x0C;

}  // namespace wireutil
