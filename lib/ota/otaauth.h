#pragma once

#include <QByteArray>

namespace traceview {

// The TraceKit recovery's upload authentication (TraceKit docs/PLANO.md
// D20-D21; same contract as its tools/btp_ota.py and its web page).
//
// Every POST is signed with key E -- the AES key TraceView already derives
// from the device's password for the BTP session (deriveChannelKey()) -- over
// a fresh nonce the recovery hands out in GET /status:
//
//   message    = "btp-ota-1" || nonce (16 octets) || action || sha256(image)
//   X-OTA-Auth = hex(HMAC-SHA256(key_E, message))
//
// `action` is 'U' (POST /update, with the image's digest) or 'B' (POST
// /reboot, back to the application, with 32 zero octets). The password never
// leaves this machine, and a captured request cannot be replayed: the nonce
// changes after every attempt.
namespace ota {

constexpr int kNonceSize = 16;
constexpr char kActionUpdate = 'U';
constexpr char kActionReboot = 'B';

// Lowercase hex, as the X-OTA-Auth header carries it. Empty when the inputs
// have the wrong size or the action is unknown -- a request signed with an
// empty header is refused by the device, never accepted by accident.
QByteArray authMac(const QByteArray& keyE, const QByteArray& nonce, char action,
                   const QByteArray& imageSha256);

}  // namespace ota
}  // namespace traceview
