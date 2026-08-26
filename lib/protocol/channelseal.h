#pragma once

#include <QByteArray>
#include <btp/codec.hpp>
#include <optional>

namespace traceview {

// AES-128-GCM seal/open for BTP v2 channel B (endpoint key E, TraceView <->
// robot -- bally_channels.h). Mirrors bally_OS's RadioSeal::seal_e/open_e
// (lib/RadioSeal/RadioSeal.cpp) byte for byte: same cipher, same nonce
// (source_id || boot_id || sequence, encryption.md section 5), same
// fail-closed contract. The dongle in between never gets this key -- it only
// relays the sealed octets (see docs/DEVICES.md "Hub channels").
namespace ChannelSeal {

// Seals `plaintext` under `key` (must be exactly 16 octets, AES-128-GCM --
// deriveChannelKey()'s output). Sets ENCRYPTED and clears CIPHER_ID (=0,
// AesGcm) on `header.flags` before sealing, so the header the caller goes on
// to encode() is already correct for the wire. Returns the ciphertext + the
// 16-octet tag; empty on any failure (wrong key size, encode/AEAD error) --
// success is never empty, since the tag alone is 16 octets, so an empty
// result is an unambiguous failure signal for callers to check.
QByteArray seal(const QByteArray& key, btp::Header& header, const QByteArray& plaintext);

// Opens `ciphertextAndTag` -- a complete, already-reassembled logical
// payload -- under `key` against `header` (the canonical logical header
// exactly as decoded off the wire). Returns the plaintext, or std::nullopt
// when: `header.flags` does not have ENCRYPTED set, the cipher is not
// AES-128-GCM, the key is the wrong size, or the authentication tag does not
// verify. Never returns a partial or unauthenticated result -- the caller
// must drop the message on nullopt, exactly as RadioSeal::open_e's contract
// requires on the firmware side.
std::optional<QByteArray> open(const QByteArray& key, const btp::Header& header,
                               const QByteArray& ciphertextAndTag);

}  // namespace ChannelSeal
}  // namespace traceview
