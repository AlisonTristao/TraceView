#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include "protocol/btpframe.h"

namespace traceview {

// One captured BTP frame (or one decode failure) for the traffic monitor.
// Produced by FrameLog from a BtpBackend::frameObserved / frameDecodeFailed
// signal, tagged with which device's connection it belonged to.
//
// `frame` holds the frame exactly as BtpSession saw it: for an Inbound
// channel-B frame `frame.payload` is still the AEAD ciphertext+tag, and
// `frame.flags & btp::kFlagEncrypted` is set -- the monitor's Decrypt box is
// what opens it. When `decodeError` is true the frame slot is meaningless and
// `errorText` carries the reason (CRC mismatch, COBS error, reassembly drop).
struct FrameLogEntry {
    quint64 seq = 0;  // monotonic, assigned by FrameLog; never reused
    QDateTime wallClock;
    FrameDirection direction = FrameDirection::Inbound;
    QString deviceId;
    QString deviceName;
    BtpFrame frame;
    bool decodeError = false;
    QString errorText;
};

}  // namespace traceview

Q_DECLARE_METATYPE(traceview::FrameLogEntry)
