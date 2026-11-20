#include "ota/otaauth.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

namespace traceview::ota {

QByteArray authMac(const QByteArray& keyE, const QByteArray& nonce, char action,
                   const QByteArray& imageSha256) {
    if (keyE.isEmpty() || nonce.size() != kNonceSize || imageSha256.size() != 32 ||
        (action != kActionUpdate && action != kActionReboot)) {
        return QByteArray();
    }
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, keyE);
    mac.addData(QByteArrayLiteral("btp-ota-1"));
    mac.addData(nonce);
    mac.addData(QByteArray(1, action));
    mac.addData(imageSha256);
    return mac.result().toHex();
}

}  // namespace traceview::ota
