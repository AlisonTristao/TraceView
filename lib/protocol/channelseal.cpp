#include "protocol/channelseal.h"

#include <btp/aead.hpp>

namespace traceview {
namespace ChannelSeal {

QByteArray seal(const QByteArray& key, btp::Header& header, const QByteArray& plaintext) {
    if (static_cast<std::size_t>(key.size()) != btp::kAesGcmKeySize) {
        return QByteArray();
    }
    if (plaintext.size() > 0xFFFF - 16) {
        return QByteArray();  // could not fit payload_size (uint16_le) once tagged
    }

    header.flags &= ~static_cast<quint16>(btp::kFlagEncrypted | btp::kCipherIdMask);
    header.flags |= btp::kFlagEncrypted;  // CIPHER_ID stays 0 == AesGcm

    const btp::AeadKey aeadKey{reinterpret_cast<const std::uint8_t*>(key.constData()),
                               static_cast<std::size_t>(key.size())};
    const std::uint16_t plaintextSize = static_cast<std::uint16_t>(plaintext.size());

    QByteArray sealed(plaintext.size() + 16, Qt::Uninitialized);
    const btp::AeadError result = btp::aead_seal_aes_gcm(
        aeadKey, header, plaintextSize,
        reinterpret_cast<const std::uint8_t*>(plaintext.constData()),
        reinterpret_cast<std::uint8_t*>(sealed.data()));
    if (result != btp::AeadError::Ok) {
        return QByteArray();
    }
    return sealed;
}

std::optional<QByteArray> open(const QByteArray& key, const btp::Header& header,
                               const QByteArray& ciphertextAndTag) {
    if (static_cast<std::size_t>(key.size()) != btp::kAesGcmKeySize) {
        return std::nullopt;
    }
    if ((header.flags & btp::kFlagEncrypted) == 0U) {
        return std::nullopt;  // never fall back to reading unsealed bytes as plaintext
    }
    if (btp::cipher_id(header.flags) != btp::CipherId::AesGcm) {
        return std::nullopt;
    }
    if (ciphertextAndTag.size() < 16) {
        return std::nullopt;
    }

    const btp::AeadKey aeadKey{reinterpret_cast<const std::uint8_t*>(key.constData()),
                               static_cast<std::size_t>(key.size())};
    const std::uint16_t ciphertextSize = static_cast<std::uint16_t>(ciphertextAndTag.size());

    QByteArray plaintext(ciphertextAndTag.size() - 16, Qt::Uninitialized);
    const btp::AeadError result = btp::aead_open_aes_gcm(
        aeadKey, header, ciphertextSize,
        reinterpret_cast<const std::uint8_t*>(ciphertextAndTag.constData()),
        reinterpret_cast<std::uint8_t*>(plaintext.data()));
    if (result != btp::AeadError::Ok) {
        return std::nullopt;
    }
    return plaintext;
}

}  // namespace ChannelSeal
}  // namespace traceview
