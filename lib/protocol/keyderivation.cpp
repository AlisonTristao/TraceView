#include "protocol/keyderivation.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QPasswordDigestor>

namespace traceview {

namespace {

// The derivation contract, and the only place these values appear in this
// project. Their canonical definition is bally_OS/scripts/provision_key.py;
// changing one here without changing it there and in the hub's own console
// command silently stops this application from talking to any robot already
// provisioned.
const char kKdfSalt[] = "bally-kdf-salt-1";  // 16 octets, no NUL
constexpr int kKdfIterations = 200000;
constexpr int kKeyLength = 16;  // AES-128-GCM, BTP CIPHER_ID 0
constexpr int kVerifyTagLength = 8;

const char kEndpointLabel[] = "bally-canal-b";
const char kLinkLabel[] = "bally-canal-c";

QByteArray verifyTag(const QByteArray& key, const char* label) {
    if (key.isEmpty()) {
        return QByteArray();
    }
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
    mac.addData(QByteArray::fromRawData(label, int(qstrlen(label))));
    return mac.result().left(kVerifyTagLength);
}

} // namespace

QByteArray deriveChannelKey(const QString& password) {
    if (password.isEmpty()) {
        return QByteArray();
    }
    // Qt's own PBKDF2 rather than a loop written here. The failure this avoids
    // is the one the whole three-implementation problem is about: an
    // implementation that is almost right produces a key that is entirely
    // wrong, and the symptom appears at the far end as an unverifiable tag.
    //
    // It lives in QtNetwork, not QtCore -- which is worth knowing, because a
    // desktop application with no networking otherwise has no reason to link
    // that module and someone may later wonder why it does.
    return QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, password.toUtf8(),
        QByteArray::fromRawData(kKdfSalt, int(sizeof(kKdfSalt) - 1)), kKdfIterations, kKeyLength);
}

QByteArray endpointKeyVerifyTag(const QByteArray& key) {
    return verifyTag(key, kEndpointLabel);
}

QByteArray linkKeyVerifyTag(const QByteArray& key) {
    return verifyTag(key, kLinkLabel);
}

} // namespace traceview
