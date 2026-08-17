#include "donatedialog.h"

#include <QByteArray>
#include <QDialogButtonBox>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QVBoxLayout>

#include "vendor/qrcodegen/qrcodegen.hpp"

namespace traceview {

namespace {

// Static Pix key (mobile phone type). The country-code-prefixed form is what
// goes into the BR Code payload; the parenthesized form is only for display.
constexpr const char* kPixKeyPayload = "+5548999668743";
constexpr const char* kPixKeyDisplay = "(48) 99966-8743";
// EMV Merchant Account/City fields are capped at 25/15 chars and must be
// ASCII, per the Central Bank's Pix BR Code spec -- no accents here.
constexpr const char* kMerchantName = "ALISON TRISTAO";
constexpr const char* kMerchantCity = "FLORIANOPOLIS";

// TLV-encodes one EMV field: 2-digit id, 2-digit length, then the value.
QString emvField(const QString& id, const QString& value) {
    return id + QString("%1").arg(value.length(), 2, 10, QLatin1Char('0')) + value;
}

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) -- the checksum algorithm
// the BR Code spec requires for its trailing field 63.
quint16 crc16Ccitt(const QByteArray& data) {
    quint16 crc = 0xFFFF;
    for (unsigned char byte : data) {
        crc ^= static_cast<quint16>(byte) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021) : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

// Builds the static Pix "BR Code" payload (EMV QR Code spec, as profiled by
// the Central Bank of Brazil) that a banking app decodes into a pre-filled
// transfer to kPixKeyPayload. Reference: Banco Central do Brasil, "Manual de
// Padroes para Iniciacao do Pix".
QString buildPixPayload() {
    const QString merchantAccount =
        emvField("00", "BR.GOV.BCB.PIX") + emvField("01", QString::fromLatin1(kPixKeyPayload));
    const QString additionalData = emvField("05", "***"); // no fixed reference/txid

    QString payload;
    payload += emvField("00", "01");   // Payload Format Indicator
    payload += emvField("01", "11");   // Point of Initiation Method: static/reusable
    payload += emvField("26", merchantAccount);
    payload += emvField("52", "0000"); // Merchant Category Code (unclassified)
    payload += emvField("53", "986");  // Transaction Currency: BRL
    payload += emvField("58", "BR");
    payload += emvField("59", QString::fromLatin1(kMerchantName));
    payload += emvField("60", QString::fromLatin1(kMerchantCity));
    payload += emvField("62", additionalData);

    payload += "6304"; // CRC id + length; value appended once known
    const quint16 crc = crc16Ccitt(payload.toLatin1());
    payload += QString("%1").arg(crc, 4, 16, QLatin1Char('0')).toUpper();
    return payload;
}

QPixmap renderQrCode(const qrcodegen::QrCode& qr, int scale = 8, int border = 4) {
    const int size = qr.getSize();
    const int imageSize = (size + border * 2) * scale;
    QImage image(imageSize, imageSize, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (qr.getModule(x, y)) {
                painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
            }
        }
    }
    return QPixmap::fromImage(image);
}

} // namespace

DonateDialog::DonateDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Support TraceView"));
    setMinimumWidth(360);

    const qrcodegen::QrCode qr =
        qrcodegen::QrCode::encodeText(buildPixPayload().toLatin1().constData(), qrcodegen::QrCode::Ecc::MEDIUM);

    auto* phraseLabel =
        new QLabel(tr("Se o TraceView te ajudou, um cafézinho via Pix é sempre bem-vindo."), this);
    phraseLabel->setWordWrap(true);
    phraseLabel->setAlignment(Qt::AlignHCenter);

    auto* qrLabel = new QLabel(this);
    qrLabel->setPixmap(renderQrCode(qr));
    qrLabel->setAlignment(Qt::AlignHCenter);

    auto* pixKeyLabel =
        new QLabel(tr("Pix key (phone): %1").arg(QString::fromLatin1(kPixKeyDisplay)), this);
    pixKeyLabel->setAlignment(Qt::AlignHCenter);
    pixKeyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto* internationalLabel = new QLabel(
        tr("Outside Brazil? An international donation option isn't set up yet, but it's on "
           "the way. Thanks for considering supporting the project either way!"),
        this);
    internationalLabel->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(phraseLabel);
    layout->addSpacing(8);
    layout->addWidget(qrLabel);
    layout->addWidget(pixKeyLabel);
    layout->addSpacing(8);
    layout->addWidget(separator);
    layout->addSpacing(4);
    layout->addWidget(internationalLabel);
    layout->addSpacing(8);
    layout->addWidget(buttons);
}

} // namespace traceview
