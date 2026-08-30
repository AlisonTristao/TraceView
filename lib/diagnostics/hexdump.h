#pragma once

#include <QByteArray>
#include <QString>

namespace traceview {

// Formatting helpers for the BTP traffic monitor's payload views. No such
// utility existed in this codebase before -- payloads were always handed to a
// channel decoder, never shown as bytes.

// Classic `xxd`-style dump: one line per 16 octets, "OFFSET  HH HH ... HH  |ascii|"
// with non-printable bytes shown as '.'. Empty input returns an empty string.
QString hexDump(const QByteArray& data);

// A single-line, space-separated hex string ("a1 b2 c3 ..."), truncated to
// `maxBytes` with a trailing "… (+N)" when longer -- for the payload column of
// the frame table, where a full dump would not fit.
QString hexInline(const QByteArray& data, int maxBytes = 24);

}  // namespace traceview
