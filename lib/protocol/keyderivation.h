#pragma once

#include <QByteArray>
#include <QString>

namespace traceview {

// Turns a typed password into the key that opens one BTP channel.
//
// There are two channels with keys and they are deliberately independent: an
// endpoint key E, one per robot, that protects that robot's data and commands
// end to end; and a link key L, one per fleet, that only administers the radio
// link between a hub and the robots behind it. Two passwords, not one password
// split by domain -- because whoever held a single password would hold both
// keys, and the two channels would collapse into one.
//
// ---------------------------------------------------------------------------
// This file is one of THREE implementations of the same contract
// ---------------------------------------------------------------------------
// The robot never runs a KDF: nobody types a password into it, so it reads
// keys that are already derived, from a file a provisioning script wrote. The
// hub and this application do have a keyboard in front of them, and derive at
// runtime from what was typed. All three have to produce the SAME octets from
// the same password or a channel simply does not work, with a symptom -- a tag
// that will not verify -- that says nothing about which end is wrong.
//
// The canonical definition of the constants below is
// bally_OS/scripts/provision_key.py. Nothing here may drift from it, and
// tests/test_keyderivation.cpp is what enforces that: it checks these
// functions against a vector that script actually produced, rather than
// against a restatement of the same arithmetic.
//
// The salt is fixed rather than random, which is what lets three ends agree
// with no provisioning handshake at all. It costs the usual property of a
// per-install salt (two installs with the same password get the same key),
// which is acceptable because these are per-fleet and per-robot secrets, not
// per-user credentials.

// PBKDF2-HMAC-SHA256, 200000 iterations, over the 16 ASCII octets of
// "bally-kdf-salt-1", producing a 16-octet AES-128-GCM key (BTP CIPHER_ID 0).
// The password is encoded UTF-8 before hashing.
//
// Deliberately slow: 200000 iterations is a fraction of a second on a desktop
// and the call is made ONCE per connection, never per frame. A caller that
// finds itself invoking this inside a send path has a bug, not a performance
// problem.
//
// Returns an empty QByteArray for an empty password rather than deriving from
// nothing -- an empty password is a configuration mistake, and a key derived
// from it would work, which is the wrong kind of forgiving.
QByteArray deriveChannelKey(const QString& password);

// The first 8 octets of HMAC-SHA256(key, label), where the label is
// "bally-canal-b" for an endpoint key and "bally-canal-c" for a link key.
//
// These exist so a wrong password fails EARLY and says WHICH key is wrong,
// instead of leaving one channel mysteriously silent. They are public by
// construction -- they are stored in the clear in the robot's key file and are
// safe to print, unlike the keys themselves, which must never be logged.
QByteArray endpointKeyVerifyTag(const QByteArray& key);
QByteArray linkKeyVerifyTag(const QByteArray& key);

}  // namespace traceview
