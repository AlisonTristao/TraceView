#include "ota/otadevicepanel.h"

#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "theme/dialogpresenter.h"
#include "theme/iconlibrary.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr int kPollIntervalMs = 1000;

// ESP image header (esp_image_header_t): magic, segment count, SPI mode and
// speed/size, entry point, WP pin, 3 drive bytes, then the 16-bit chip id.
constexpr int kChipIdOffset = 12;
// esp_app_desc_t sits right after the image header (24 bytes) and the first
// segment header (8 bytes); its fields at fixed offsets from there.
constexpr int kAppDescOffset = 32;
constexpr quint32 kAppDescMagic = 0xABCD5432;

QString chipName(int chipId) {
    switch (chipId) {
        case 0: return QStringLiteral("esp32");
        case 2: return QStringLiteral("esp32s2");
        case 5: return QStringLiteral("esp32c3");
        case 9: return QStringLiteral("esp32s3");
        case 12: return QStringLiteral("esp32c2");
        case 13: return QStringLiteral("esp32c6");
        case 16: return QStringLiteral("esp32h2");
        case 18: return QStringLiteral("esp32p4");
        case 23: return QStringLiteral("esp32c5");
        default: return QString();
    }
}

QString fixedString(const QByteArray& data, int offset, int length) {
    if (data.size() < offset + length) return QString();
    const QByteArray field = data.mid(offset, length);
    const int end = field.indexOf('\0');
    return QString::fromLatin1(end >= 0 ? field.left(end) : field).trimmed();
}

QString sizeText(qint64 bytes) {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString reasonText(const QString& reason) {
    if (reason == QLatin1String("command")) return OtaDevicePanel::tr("Asked for (btp -recovery).");
    if (reason == QLatin1String("crash_loop")) return OtaDevicePanel::tr("The application kept crashing.");
    if (reason.startsWith(QLatin1String("factory_reset"))) return OtaDevicePanel::tr("After a factory reset.");
    return reason.isEmpty() || reason == QLatin1String("none")
               ? OtaDevicePanel::tr("No valid application, or the boot chose it.")
               : reason;
}

QString rgba(const QColor& color, double alpha) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(alpha, 0, 'f', 2);
}

QLabel* keyLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("otaKey"));
    return label;
}

QLabel* valueLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("otaValue"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QLabel* cardTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text.toUpper(), parent);
    label->setObjectName(QStringLiteral("otaCardTitle"));
    return label;
}

}  // namespace

FirmwareImageInfo readFirmwareImageInfo(const QString& filePath) {
    FirmwareImageInfo info;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = OtaDevicePanel::tr("Cannot read the file: %1").arg(file.errorString());
        return info;
    }
    info.size = file.size();
    const QByteArray head = file.read(256);
    if (head.isEmpty() || static_cast<unsigned char>(head.at(0)) != OtaClient::kImageMagic) {
        info.error = OtaDevicePanel::tr("Not an ESP application image (it must start with 0xE9). "
                                        "Use the application .bin from build/, not the merged one.");
        return info;
    }
    info.valid = true;
    if (head.size() >= kChipIdOffset + 2) {
        const int chipId = static_cast<unsigned char>(head.at(kChipIdOffset)) |
                           (static_cast<unsigned char>(head.at(kChipIdOffset + 1)) << 8);
        info.chip = chipName(chipId);
    }
    if (head.size() >= kAppDescOffset + 176) {
        const auto byteAt = [&head](int i) { return static_cast<quint32>(static_cast<unsigned char>(head.at(i))); };
        const quint32 magic = byteAt(kAppDescOffset) | (byteAt(kAppDescOffset + 1) << 8) |
                              (byteAt(kAppDescOffset + 2) << 16) | (byteAt(kAppDescOffset + 3) << 24);
        if (magic == kAppDescMagic) {
            const int base = kAppDescOffset;
            info.version = fixedString(head, base + 16, 32);
            info.project = fixedString(head, base + 48, 32);
            const QString time = fixedString(head, base + 80, 16);
            const QString date = fixedString(head, base + 96, 16);
            info.built = (date + QLatin1Char(' ') + time).trimmed();
            info.idfVersion = fixedString(head, base + 112, 32);
        }
    }
    return info;
}

// The three steps of an install, drawn as numbered dots on a line: done ones
// get a check, the current one the accent ring, a failed one the danger color.
class OtaStepIndicator : public QWidget {
public:
    explicit OtaStepIndicator(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("otaSteps"));
        setMinimumHeight(58);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setLabels(const QStringList& labels) {
        m_labels = labels;
        update();
    }
    // current < 0: nothing started. current >= labels.size(): all done.
    void setState(int current, bool failed) {
        m_current = current;
        m_failed = failed;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (m_labels.isEmpty()) return;
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int count = int(m_labels.size());
        const qreal slot = qreal(width()) / count;
        const qreal radius = 11.0;
        const qreal cy = radius + 4.0;

        // The line behind the dots, accent up to the current step.
        for (int i = 0; i + 1 < count; ++i) {
            const qreal x0 = slot * (i + 0.5) + radius + 4;
            const qreal x1 = slot * (i + 1.5) - radius - 4;
            const bool done = m_current > i;
            painter.setPen(QPen(done ? palette.accent : palette.border, 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(x0, cy), QPointF(x1, cy));
        }

        QFont numberFont = font();
        numberFont.setBold(true);
        QFont labelFont = font();
        for (int i = 0; i < count; ++i) {
            const QPointF center(slot * (i + 0.5), cy);
            const QRectF dot(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
            const bool done = m_current > i;
            const bool active = m_current == i;
            const bool failedHere = active && m_failed;

            QColor fill = palette.surface;
            QColor ring = palette.border;
            QColor glyph = palette.textSecondary;
            if (failedHere) {
                fill = palette.danger;
                ring = palette.danger;
                glyph = palette.background;
            } else if (done) {
                fill = palette.accent;
                ring = palette.accent;
                glyph = palette.background;
            } else if (active) {
                ring = palette.accent;
                glyph = palette.accent;
            }
            if (active && !failedHere) {
                QColor halo = palette.accent;
                halo.setAlphaF(0.18);
                painter.setPen(Qt::NoPen);
                painter.setBrush(halo);
                painter.drawEllipse(dot.adjusted(-4, -4, 4, 4));
            }
            painter.setPen(QPen(ring, 2));
            painter.setBrush(fill);
            painter.drawEllipse(dot);

            painter.setPen(QPen(glyph, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            if (done) {
                QPainterPath check;
                check.moveTo(center + QPointF(-4.5, 0.5));
                check.lineTo(center + QPointF(-1.2, 3.8));
                check.lineTo(center + QPointF(5.0, -3.5));
                painter.drawPath(check);
            } else if (failedHere) {
                painter.drawLine(center + QPointF(-3.5, -3.5), center + QPointF(3.5, 3.5));
                painter.drawLine(center + QPointF(3.5, -3.5), center + QPointF(-3.5, 3.5));
            } else {
                painter.setFont(numberFont);
                painter.drawText(dot, Qt::AlignCenter, QString::number(i + 1));
            }

            painter.setFont(labelFont);
            painter.setPen(failedHere ? palette.danger
                                      : (done || active) ? palette.textPrimary : palette.textSecondary);
            const QRectF text(slot * i + 2, cy + radius + 6, slot - 4, height() - (cy + radius + 6));
            painter.drawText(text, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_labels.at(i));
        }
    }

private:
    QStringList m_labels;
    int m_current = -1;
    bool m_failed = false;
};

OtaDevicePanel::OtaDevicePanel(const Device& device, QWidget* parent)
    : QWidget(parent), m_device(device), m_wasConnected(device.connected) {
    m_client = new OtaClient(this);
    connect(m_client, &OtaClient::statusChecked, this, &OtaDevicePanel::onStatusChecked);
    connect(m_client, &OtaClient::uploadProgress, this, &OtaDevicePanel::onUploadProgress);
    connect(m_client, &OtaClient::uploadFinished, this, &OtaDevicePanel::onUploadFinished);
    connect(m_client, &OtaClient::rebootFinished, this, &OtaDevicePanel::onRebootFinished);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &OtaDevicePanel::poll);

    buildUi();
    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { applyTheme(); });
    refreshView();
}

void OtaDevicePanel::setKeyProvider(std::function<QByteArray(const QString&)> provider) {
    m_keyProvider = std::move(provider);
}

// --- Layout ---------------------------------------------------------------

void OtaDevicePanel::buildUi() {
    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("otaContent"));

    // Installed: what the device runs now, and its recovery.
    m_deviceCard = new QFrame(content);
    m_deviceCard->setObjectName(QStringLiteral("otaCard"));
    m_stateBadge = new QLabel(m_deviceCard);
    m_stateBadge->setObjectName(QStringLiteral("otaBadge"));
    m_stateHint = new QLabel(m_deviceCard);
    m_stateHint->setObjectName(QStringLiteral("otaHint"));
    m_stateHint->setWordWrap(true);
    m_appValue = valueLabel(m_deviceCard);
    m_builtValue = valueLabel(m_deviceCard);
    m_recoveryValue = valueLabel(m_deviceCard);
    m_addressValue = valueLabel(m_deviceCard);

    auto* badgeRow = new QHBoxLayout();
    badgeRow->addWidget(cardTitle(tr("Installed"), m_deviceCard));
    badgeRow->addStretch(1);
    badgeRow->addWidget(m_stateBadge);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    grid->addWidget(keyLabel(tr("Firmware"), m_deviceCard), 0, 0);
    grid->addWidget(m_appValue, 0, 1);
    grid->addWidget(keyLabel(tr("Built"), m_deviceCard), 1, 0);
    grid->addWidget(m_builtValue, 1, 1);
    grid->addWidget(keyLabel(tr("Recovery"), m_deviceCard), 2, 0);
    grid->addWidget(m_recoveryValue, 2, 1);
    grid->addWidget(keyLabel(tr("Address"), m_deviceCard), 3, 0);
    grid->addWidget(m_addressValue, 3, 1);

    auto* deviceLayout = new QVBoxLayout(m_deviceCard);
    deviceLayout->setContentsMargins(16, 14, 16, 14);
    deviceLayout->setSpacing(10);
    deviceLayout->addLayout(badgeRow);
    deviceLayout->addLayout(grid);
    deviceLayout->addWidget(m_stateHint);

    // New firmware: the drop zone, then what the picked file is.
    m_fileCard = new QFrame(content);
    m_fileCard->setObjectName(QStringLiteral("otaCard"));

    m_dropZone = new QFrame(m_fileCard);
    m_dropZone->setObjectName(QStringLiteral("otaDrop"));
    m_dropZone->setAcceptDrops(true);
    m_dropZone->setCursor(Qt::PointingHandCursor);
    m_dropZone->installEventFilter(this);
    m_dropIcon = new QLabel(m_dropZone);
    m_dropIcon->setAlignment(Qt::AlignCenter);
    m_dropText = new QLabel(m_dropZone);
    m_dropText->setObjectName(QStringLiteral("otaHint"));
    m_dropText->setAlignment(Qt::AlignCenter);
    m_dropText->setWordWrap(true);
    m_chooseButton = new QPushButton(tr("Choose file\xE2\x80\xA6"), m_dropZone);
    m_chooseButton->setCursor(Qt::PointingHandCursor);
    connect(m_chooseButton, &QPushButton::clicked, this, &OtaDevicePanel::chooseFile);

    m_fileDetails = new QWidget(m_dropZone);
    m_fileDetails->setObjectName(QStringLiteral("otaFileDetails"));
    m_fileName = new QLabel(m_fileDetails);
    m_fileName->setObjectName(QStringLiteral("otaFileName"));
    m_fileName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fileMeta = new QLabel(m_fileDetails);
    m_fileMeta->setObjectName(QStringLiteral("otaHint"));
    m_fileMeta->setWordWrap(true);
    auto* detailsLayout = new QVBoxLayout(m_fileDetails);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(2);
    detailsLayout->addWidget(m_fileName);
    detailsLayout->addWidget(m_fileMeta);

    auto* dropLayout = new QHBoxLayout(m_dropZone);
    dropLayout->setContentsMargins(16, 14, 16, 14);
    dropLayout->setSpacing(14);
    dropLayout->addWidget(m_dropIcon);
    dropLayout->addWidget(m_dropText, 1);
    dropLayout->addWidget(m_fileDetails, 1);
    dropLayout->addWidget(m_chooseButton);

    m_fileCheck = new QLabel(m_fileCard);
    m_fileCheck->setWordWrap(true);
    m_fitBar = new QProgressBar(m_fileCard);
    m_fitBar->setObjectName(QStringLiteral("otaFit"));
    m_fitBar->setTextVisible(false);
    m_fitBar->setFixedHeight(6);
    m_fitText = new QLabel(m_fileCard);
    m_fitText->setObjectName(QStringLiteral("otaHint"));

    auto* fileLayout = new QVBoxLayout(m_fileCard);
    fileLayout->setContentsMargins(16, 14, 16, 14);
    fileLayout->setSpacing(10);
    fileLayout->addWidget(cardTitle(tr("New firmware"), m_fileCard));
    fileLayout->addWidget(m_dropZone);
    fileLayout->addWidget(m_fileCheck);
    fileLayout->addWidget(m_fitBar);
    fileLayout->addWidget(m_fitText);

    // Install: the three steps, the bar, the buttons.
    auto* installCard = new QFrame(content);
    installCard->setObjectName(QStringLiteral("otaCard"));
    m_steps = new OtaStepIndicator(installCard);
    m_steps->setLabels({tr("Restart into the recovery"), tr("Send the image"), tr("Start the new firmware")});
    m_progress = new QProgressBar(installCard);
    m_progress->setObjectName(QStringLiteral("otaProgress"));
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_progressText = new QLabel(installCard);
    m_progressText->setWordWrap(true);
    m_progressText->setObjectName(QStringLiteral("otaHint"));

    m_toAppButton = new QPushButton(tr("Back to the application"), installCard);
    m_toAppButton->setToolTip(tr("Leave the recovery for the application already installed."));
    connect(m_toAppButton, &QPushButton::clicked, this, &OtaDevicePanel::backToApp);
    m_installButton = new QPushButton(installCard);
    m_installButton->setObjectName(QStringLiteral("otaPrimary"));
    m_installButton->setCursor(Qt::PointingHandCursor);
    connect(m_installButton, &QPushButton::clicked, this, &OtaDevicePanel::install);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_progressText, 1);
    buttons->addWidget(m_toAppButton);
    buttons->addWidget(m_installButton);

    auto* installLayout = new QVBoxLayout(installCard);
    installLayout->setContentsMargins(16, 14, 16, 14);
    installLayout->setSpacing(10);
    installLayout->addWidget(cardTitle(tr("Install"), installCard));
    installLayout->addWidget(m_steps);
    installLayout->addWidget(m_progress);
    installLayout->addLayout(buttons);

    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 4, 0);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(m_deviceCard);
    contentLayout->addWidget(m_fileCard);
    contentLayout->addWidget(installCard);
    contentLayout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("otaScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
}

void OtaDevicePanel::applyTheme() {
    const ThemePalette& p = ThemeManager::instance().currentTheme();
    const QString sheet = QStringLiteral(R"(
QScrollArea#otaScroll, QWidget#otaContent, QWidget#otaSteps, QWidget#otaFileDetails { background: transparent; }
QFrame#otaCard {
    background-color: %1;
    border: 1px solid %2;
    border-radius: 10px;
}
QLabel#otaCardTitle { color: %3; font-size: 8pt; font-weight: 600; }
QLabel#otaKey { color: %3; }
QLabel#otaValue { color: %4; }
QLabel#otaHint { color: %3; }
QLabel#otaFileName { color: %4; font-weight: 600; }
QFrame#otaDrop {
    background-color: %5;
    border: 2px dashed %6;
    border-radius: 8px;
    min-height: 44px;
}
QFrame#otaDrop[dragOver="true"] {
    background-color: %7;
    border-color: %8;
}
QPushButton#otaPrimary, QPushButton#otaPrimary:hover, QPushButton#otaPrimary:pressed {
    background-color: %8;
    border: 1px solid %8;
    color: %9;
    font-weight: 600;
    padding: 5px 14px;
}
QPushButton#otaPrimary:hover { background-color: %10; border-color: %10; }
QPushButton#otaPrimary:disabled {
    background-color: %5;
    border-color: %2;
    color: %11;
}
QProgressBar#otaProgress, QProgressBar#otaFit {
    background-color: %5;
    border: none;
    border-radius: 3px;
}
QProgressBar#otaProgress::chunk, QProgressBar#otaFit::chunk {
    background-color: %8;
    border-radius: 3px;
}
)")
                              .arg(p.surface.name(), p.border.name(), p.textSecondary.name(),
                                   p.textPrimary.name(), p.surfaceAlt.name(), p.textDisabled.name(),
                                   rgba(p.accent, 0.10), p.accent.name(), p.background.name())
                              .arg(p.accentHover.name(), p.textDisabled.name());
    setStyleSheet(sheet);
    m_dropIcon->setPixmap(IconLibrary::instance()
                              .icon(QStringLiteral("lucide:file-up"), p.textSecondary, 28)
                              .pixmap(28, 28));
    sizeButtons();
    refreshView();
}

void OtaDevicePanel::sizeButtons() {
    // One size for every button on the page, wide enough for the longest
    // text any of them can show, so none jumps when its text changes
    // ("Install" <-> "Restart and install", "Choose file..." <-> "Change...").
    QFont bold = m_installButton->font();
    bold.setBold(true);  // the primary button's text is bold: measure the widest case
    const QFontMetrics metrics(bold);
    int width = 0;
    for (const QString& label :
         {tr("Choose file\xE2\x80\xA6"), tr("Change\xE2\x80\xA6"), tr("Back to the application"), tr("Install"),
          tr("Restart and install")}) {
        width = std::max(width, metrics.horizontalAdvance(label));
    }
    const QSize size(width + 40, metrics.height() + 16);
    for (QPushButton* button : {m_chooseButton, m_toAppButton, m_installButton}) {
        button->setFixedSize(size);
    }
}

// --- Polling and the client's answers ---------------------------------------

void OtaDevicePanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    poll();
    m_pollTimer->start();
}

void OtaDevicePanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (!busy()) m_pollTimer->stop();
}

void OtaDevicePanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) update();
}

void OtaDevicePanel::poll() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_phase == Phase::Restarting && now > m_deadlineMs) {
        m_pendingKey.clear();
        setPhase(Phase::Failed,
                 tr("%1 did not answer as a recovery in %2 s. Is it on a Wi-Fi network this "
                    "computer reaches? Without a known network the recovery raises its own "
                    "(\"btp-<name>\").")
                     .arg(m_device.name)
                     .arg(kRecoveryWaitMs / 1000));
    } else if (m_phase == Phase::Starting && now > m_deadlineMs) {
        const QString running = reported("fw_version");
        if (m_device.connected && !m_image.version.isEmpty() && !running.isEmpty() &&
            running != m_image.version) {
            setPhase(Phase::Failed, tr("It came back running %1, not %2: the new firmware did not "
                                       "start and the device went back to the previous one.")
                                        .arg(running, m_image.version));
        } else {
            setPhase(Phase::Done, tr("The recovery accepted the image and started it. Connect "
                                     "the device to see it running."));
        }
    } else if (m_phase == Phase::Restarting) {
        refreshProgress();  // the countdown
    }

    if (m_phase != Phase::Uploading && !m_client->statusRequestInFlight(m_device.id)) {
        const QString address = otaAddressFor(m_device);
        if (!address.isEmpty()) m_client->checkStatus(m_device.id, address);
    }
    if (!busy() && !isVisible()) m_pollTimer->stop();
}

void OtaDevicePanel::onStatusChecked(const QString& deviceId, bool reachable, const RecoveryStatus& status,
                                     const QString& errorMessage) {
    if (deviceId != m_device.id) return;
    m_checkedOnce = true;
    if (reachable) {
        m_recovery = status;
        m_lastError.clear();
    } else {
        m_recovery.reset();
        m_lastError = errorMessage;
    }
    if (m_phase == Phase::Restarting && reachable && status.otaReady) {
        const QByteArray key = m_pendingKey;
        m_pendingKey.clear();
        beginUpload(key);
        return;
    }
    refreshView();
}

void OtaDevicePanel::setDevice(const Device& device) {
    const bool cameBack = device.connected && !m_wasConnected;
    m_device = device;
    m_wasConnected = device.connected;
    if (m_phase == Phase::Starting && device.connected) {
        const QString running = reported("fw_version");
        if (!running.isEmpty() && (m_image.version.isEmpty() || running == m_image.version)) {
            setPhase(Phase::Done, tr("%1 is running %2.").arg(device.name, running));
            emit statusMessage(tr("%1: running %2").arg(device.name, running));
            return;
        }
    }
    if (cameBack) m_recovery.reset();
    refreshView();
}

void OtaDevicePanel::onUploadProgress(const QString& deviceId, qint64 sent, qint64 total) {
    if (deviceId != m_device.id || m_phase != Phase::Uploading) return;
    m_percent = total > 0 ? int((sent * 100) / total) : -1;
    m_sendTotal = total;
    refreshProgress();
}

void OtaDevicePanel::onUploadFinished(const QString& deviceId, bool success, const QString& message) {
    if (deviceId != m_device.id) return;
    if (success) {
        m_recovery.reset();
        m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + kBootWaitMs;
        setPhase(Phase::Starting, message);
        emit statusMessage(tr("%1: %2").arg(m_device.name, message));
        return;
    }
    if (message.contains(QLatin1String("wrong password"), Qt::CaseInsensitive)) {
        m_sessionPassword.clear();  // ask again next time
    }
    setPhase(Phase::Failed, message);
}

void OtaDevicePanel::onRebootFinished(const QString& deviceId, bool success, const QString& message) {
    if (deviceId != m_device.id) return;
    if (success) {
        m_recovery.reset();
        setPhase(Phase::Idle);
        emit statusMessage(tr("%1: %2").arg(m_device.name, message));
    } else {
        setPhase(Phase::Failed, message);
    }
}

// --- File -------------------------------------------------------------------

bool OtaDevicePanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_dropZone) return QWidget::eventFilter(watched, event);
    auto setDragOver = [this](bool on) {
        m_dragOver = on;
        m_dropZone->setProperty("dragOver", on);
        m_dropZone->style()->unpolish(m_dropZone);
        m_dropZone->style()->polish(m_dropZone);
    };
    switch (event->type()) {
        case QEvent::DragEnter: {
            auto* drag = static_cast<QDragEnterEvent*>(event);
            if (!busy() && drag->mimeData()->hasUrls() && drag->mimeData()->urls().size() == 1 &&
                drag->mimeData()->urls().first().isLocalFile()) {
                drag->acceptProposedAction();
                setDragOver(true);
            }
            return true;
        }
        case QEvent::DragLeave:
            setDragOver(false);
            return true;
        case QEvent::Drop: {
            auto* drop = static_cast<QDropEvent*>(event);
            setDragOver(false);
            if (!busy() && drop->mimeData()->hasUrls()) {
                setFile(drop->mimeData()->urls().first().toLocalFile());
                drop->acceptProposedAction();
            }
            return true;
        }
        case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && !busy()) chooseFile();
            return true;
        default:
            return QWidget::eventFilter(watched, event);
    }
}

void OtaDevicePanel::chooseFile() {
    const QString start = m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Select Firmware"), start,
                                                      tr("Firmware Binary (*.bin);;All Files (*)"));
    if (!path.isEmpty()) setFile(path);
}

void OtaDevicePanel::setFile(const QString& filePath) {
    m_filePath = filePath;
    m_image = readFirmwareImageInfo(filePath);
    if (m_phase == Phase::Done || m_phase == Phase::Failed) setPhase(Phase::Idle);
    refreshView();
}

// --- Install ----------------------------------------------------------------

QByteArray OtaDevicePanel::keyFor(bool* cancelled) {
    *cancelled = false;
    QString password = m_device.peerPassword;
    if (password.isEmpty()) password = m_sessionPassword;
    if (password.isEmpty()) {
        bool ok = false;
        password = DialogPresenter::getText(
            this, tr("Install Firmware"),
            tr("Password of %1 (the one its key was provisioned with). It stays on this "
               "computer: the upload is signed with it.")
                .arg(m_device.name),
            QLineEdit::Password, QString(), &ok);
        if (!ok || password.isEmpty()) {
            *cancelled = true;
            return QByteArray();
        }
        m_sessionPassword = password;
    }
    return m_keyProvider ? m_keyProvider(password) : QByteArray();
}

void OtaDevicePanel::install() {
    if (busy() || !m_image.valid) return;
    const bool inRecovery = m_recovery.has_value() && m_recovery->otaReady;
    if (!inRecovery && !m_device.connected) return;  // the button is disabled then

    const QString deviceChip = m_recovery ? m_recovery->chip : reported("chip");
    if (!m_image.chip.isEmpty() && !deviceChip.isEmpty() && m_image.chip != deviceChip &&
        !DialogPresenter::confirm(this, tr("Install Firmware"),
                                  tr("This image was built for %1, and %2 is an %3. Install it "
                                     "anyway?")
                                      .arg(m_image.chip, m_device.name, deviceChip),
                                  tr("Install anyway"), tr("Cancel"))) {
        return;
    }

    QByteArray keyE;
    if (!inRecovery || m_recovery->authRequired) {  // a DEVELOPMENT recovery needs none
        bool cancelled = false;
        keyE = keyFor(&cancelled);
        if (cancelled) return;
    }
    if (inRecovery) {
        beginUpload(keyE);
        return;
    }
    m_pendingKey = keyE;
    m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + kRecoveryWaitMs;
    setPhase(Phase::Restarting);
    emit recoveryRequested(m_device.id);
    m_pollTimer->start();
}

void OtaDevicePanel::beginUpload(const QByteArray& keyE) {
    m_percent = 0;
    m_sendTotal = 0;
    setPhase(Phase::Uploading);
    m_pollTimer->start();
    m_client->uploadFirmware(m_device.id, otaAddressFor(m_device), keyE, m_filePath);
}

void OtaDevicePanel::backToApp() {
    if (busy() || !m_recovery) return;
    QByteArray keyE;
    if (m_recovery->authRequired) {
        bool cancelled = false;
        keyE = keyFor(&cancelled);
        if (cancelled) return;
    }
    m_toAppButton->setEnabled(false);
    m_progressText->setText(tr("Leaving the recovery\xE2\x80\xA6"));
    m_client->rebootToApp(m_device.id, otaAddressFor(m_device), keyE);
}

void OtaDevicePanel::setPhase(Phase phase, const QString& detail) {
    if (phase == Phase::Failed) {
        m_failedStep = m_phase == Phase::Restarting ? 0
                       : m_phase == Phase::Uploading ? 1
                       : m_phase == Phase::Starting  ? 2
                                                     : -1;
    }
    m_phase = phase;
    m_phaseDetail = detail;
    if (!busy() && !isVisible()) m_pollTimer->stop();
    refreshView();
}

bool OtaDevicePanel::busy() const {
    return m_phase == Phase::Restarting || m_phase == Phase::Uploading || m_phase == Phase::Starting;
}

QString OtaDevicePanel::reported(const char* key) const {
    for (const DeviceInfoRecord& entry : m_device.reportedInfo) {
        if (entry.key == QLatin1String(key)) return entry.value;
    }
    return QString();
}

QString OtaDevicePanel::installedVersion() const {
    if (m_recovery) return m_recovery->appValid ? m_recovery->appVersion : QString();
    return m_device.connected ? reported("fw_version") : QString();
}

// --- View -------------------------------------------------------------------

void OtaDevicePanel::refreshView() {
    refreshDeviceCard();
    refreshFileCard();
    refreshProgress();
    refreshButtons();
}

void OtaDevicePanel::refreshDeviceCard() {
    const ThemePalette& p = ThemeManager::instance().currentTheme();
    const QString address = otaAddressFor(m_device);

    QString badge;
    QColor color;
    QString hint;
    if (m_recovery) {
        badge = m_recovery->otaReady ? tr("In the recovery") : tr("Receiving a firmware");
        color = p.warning;
        hint = reasonText(m_recovery->reason);
        if (!m_recovery->ownNetwork.isEmpty()) {
            hint += QLatin1Char(' ') + tr("On its own Wi-Fi network: %1.").arg(m_recovery->ownNetwork);
        }
    } else if (m_device.connected) {
        badge = tr("Running");
        color = p.success;
        hint = tr("Install restarts it into the recovery, sends the image and starts it.");
    } else if (address.isEmpty()) {
        badge = tr("No address");
        color = p.textSecondary;
        hint = tr("Connect it once over Wi-Fi so it reports where its recovery answers, or set "
                  "the OTA address in Device Settings.");
    } else if (!m_checkedOnce) {
        badge = tr("Checking\xE2\x80\xA6");
        color = p.textSecondary;
    } else {
        badge = tr("Offline");
        color = p.textSecondary;
        hint = tr("Not connected and no recovery answers at %1%2. Connect it to install a "
                  "firmware.")
                   .arg(address, m_lastError.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(m_lastError));
    }
    m_stateBadge->setText(QStringLiteral("●  ") + badge);
    m_stateBadge->setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 10px; "
                                               "padding: 3px 10px; font-weight: 600; }")
                                    .arg(rgba(color, 0.16), color.name()));
    m_stateHint->setText(hint);
    m_stateHint->setVisible(!hint.isEmpty());

    const QString dash = QStringLiteral("—");
    if (m_recovery) {
        m_appValue->setText(m_recovery->appValid
                                ? QStringLiteral("<b>%1</b> &nbsp;%2")
                                      .arg(m_recovery->appVersion.toHtmlEscaped(),
                                           m_recovery->appProject.toHtmlEscaped())
                                : tr("none: waiting for a firmware"));
        m_builtValue->setText(dash);
        m_recoveryValue->setText(tr("%1 · %2 · application up to %3")
                                     .arg(m_recovery->recoveryVersion, m_recovery->chip,
                                          sizeText(m_recovery->maxSize)));
    } else {
        const QString running = m_device.connected ? reported("fw_version") : QString();
        m_appValue->setText(running.isEmpty() ? dash : QStringLiteral("<b>%1</b>").arg(running.toHtmlEscaped()));
        const QString built = m_device.connected ? reported("fw_build") : QString();
        const QString idf = m_device.connected ? reported("idf_version") : QString();
        m_builtValue->setText(built.isEmpty() ? dash
                                              : idf.isEmpty() ? built
                                                              : tr("%1 · ESP-IDF %2").arg(built, idf));
        const QString chip = m_device.connected ? reported("chip") : QString();
        m_recoveryValue->setText(chip.isEmpty() ? tr("details once it is in the recovery")
                                                : tr("%1 · details once it is in the recovery").arg(chip));
    }
    m_addressValue->setText(address.isEmpty() ? dash : address);
    m_addressValue->setToolTip(m_device.otaAddress.trimmed().isEmpty()
                                   ? tr("From the device itself (the address it reported, or its TCP host).")
                                   : tr("Set in Device Settings."));
}

void OtaDevicePanel::refreshFileCard() {
    const ThemePalette& p = ThemeManager::instance().currentTheme();
    const bool hasFile = !m_filePath.isEmpty();
    m_dropText->setVisible(!hasFile);
    m_fileDetails->setVisible(hasFile);
    m_dropText->setText(tr("Drop the application .bin here, or click to choose it"));
    m_chooseButton->setText(hasFile ? tr("Change\xE2\x80\xA6") : tr("Choose file\xE2\x80\xA6"));
    m_chooseButton->setEnabled(!busy());

    m_fitBar->setVisible(false);
    m_fitText->setVisible(false);
    m_fileCheck->setVisible(hasFile);
    if (!hasFile) return;

    const QFileInfo info(m_filePath);
    m_fileName->setText(info.fileName());
    m_fileName->setToolTip(QDir::toNativeSeparators(m_filePath));
    QStringList meta;
    meta << sizeText(m_image.size);
    if (!m_image.built.isEmpty()) meta << tr("built %1").arg(m_image.built);
    else meta << tr("saved %1").arg(QLocale().toString(info.lastModified(), QLocale::ShortFormat));
    m_fileMeta->setText(meta.join(QStringLiteral("  ·  ")));

    auto setCheck = [this](const QColor& color, const QString& text) {
        m_fileCheck->setText(text);
        m_fileCheck->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(color.name()));
    };
    if (!m_image.valid) {
        setCheck(p.danger, QStringLiteral("✕  ") + m_image.error);
        return;
    }

    QString line;
    if (!m_image.project.isEmpty()) {
        line = tr("<b>%1</b> %2").arg(m_image.project.toHtmlEscaped(), m_image.version.toHtmlEscaped());
    } else {
        line = tr("ESP application image");
    }
    if (!m_image.chip.isEmpty()) line += QStringLiteral(" · ") + m_image.chip;

    const QString deviceChip = m_recovery ? m_recovery->chip : reported("chip");
    const QString installed = installedVersion();
    const QString installedProject = m_recovery && m_recovery->appValid ? m_recovery->appProject : QString();
    QColor color = p.success;
    QString mark = QStringLiteral("✓  ");
    QString note;
    if (!m_image.chip.isEmpty() && !deviceChip.isEmpty() && m_image.chip != deviceChip) {
        color = p.danger;
        mark = QStringLiteral("✕  ");
        note = tr("built for %1, the device is an %2").arg(m_image.chip, deviceChip);
    } else if (!installedProject.isEmpty() && !m_image.project.isEmpty() && installedProject != m_image.project) {
        color = p.warning;
        mark = QStringLiteral("⚠  ");
        note = tr("a different project from the installed %1").arg(installedProject);
    } else if (!installed.isEmpty() && !m_image.version.isEmpty()) {
        note = installed == m_image.version ? tr("the same version as installed")
                                            : tr("replaces %1").arg(installed);
    }
    if (!note.isEmpty()) line += QStringLiteral(" — ") + note.toHtmlEscaped();
    setCheck(color, mark + line);

    if (m_recovery && m_recovery->maxSize > 0) {
        const int percent = int(std::min<qint64>(100, (m_image.size * 100) / m_recovery->maxSize));
        m_fitBar->setRange(0, 100);
        m_fitBar->setValue(percent);
        m_fitBar->setVisible(true);
        m_fitText->setVisible(true);
        m_fitText->setText(m_image.size > m_recovery->maxSize
                               ? tr("Too large: %1 for a partition of %2.")
                                     .arg(sizeText(m_image.size), sizeText(m_recovery->maxSize))
                               : tr("%1% of the application partition (%2).")
                                     .arg(percent)
                                     .arg(sizeText(m_recovery->maxSize)));
    }
}

void OtaDevicePanel::refreshProgress() {
    const ThemePalette& p = ThemeManager::instance().currentTheme();
    QString text;
    QColor color = p.textSecondary;
    switch (m_phase) {
        case Phase::Idle:
            m_steps->setState(-1, false);
            m_progress->setVisible(false);
            break;
        case Phase::Restarting: {
            m_steps->setState(0, false);
            m_progress->setVisible(true);
            m_progress->setRange(0, 0);
            const qint64 left = std::max<qint64>(0, (m_deadlineMs - QDateTime::currentMSecsSinceEpoch()) / 1000);
            text = tr("Restarting %1 and waiting for its recovery to answer\xE2\x80\xA6 (%2 s)")
                       .arg(m_device.name)
                       .arg(left);
            break;
        }
        case Phase::Uploading:
            m_steps->setState(1, false);
            m_progress->setVisible(true);
            if (m_percent < 0) {
                m_progress->setRange(0, 0);
                text = tr("Sending\xE2\x80\xA6");
            } else {
                m_progress->setRange(0, 100);
                m_progress->setValue(m_percent);
                // Compressed on the way when the recovery inflates it: say so.
                text = m_sendTotal > 0 && m_sendTotal < m_image.size
                           ? tr("Sending\xE2\x80\xA6 %1% of %2 (compressed from %3)")
                                 .arg(m_percent)
                                 .arg(sizeText(m_sendTotal), sizeText(m_image.size))
                           : tr("Sending\xE2\x80\xA6 %1% of %2").arg(m_percent).arg(sizeText(m_image.size));
            }
            break;
        case Phase::Starting:
            m_steps->setState(2, false);
            m_progress->setVisible(true);
            m_progress->setRange(0, 0);
            text = tr("Image accepted. Starting the new firmware and waiting for it to connect\xE2\x80\xA6");
            break;
        case Phase::Done:
            m_steps->setState(3, false);
            m_progress->setVisible(true);
            m_progress->setRange(0, 100);
            m_progress->setValue(100);
            text = QStringLiteral("✓  ") + m_phaseDetail;
            color = p.success;
            break;
        case Phase::Failed: {
            m_steps->setState(m_failedStep, m_failedStep >= 0);
            m_progress->setVisible(false);
            text = QStringLiteral("✕  ") + m_phaseDetail;
            color = p.danger;
            break;
        }
    }
    m_progressText->setText(text);
    m_progressText->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(color.name()));
}

void OtaDevicePanel::refreshButtons() {
    const bool inRecovery = m_recovery.has_value() && m_recovery->otaReady;
    const bool tooLarge = m_recovery && m_recovery->maxSize > 0 && m_image.size > m_recovery->maxSize;
    const bool reachable = inRecovery || m_device.connected;
    m_installButton->setText(inRecovery ? tr("Install") : tr("Restart and install"));
    m_installButton->setEnabled(!busy() && m_image.valid && !tooLarge && reachable &&
                                !otaAddressFor(m_device).isEmpty());
    if (!m_image.valid) {
        m_installButton->setToolTip(tr("Choose a firmware image first."));
    } else if (!reachable) {
        m_installButton->setToolTip(tr("Connect the device, or put it in the recovery."));
    } else {
        m_installButton->setToolTip(QString());
    }
    m_toAppButton->setVisible(m_recovery.has_value() && m_recovery->appValid);
    m_toAppButton->setEnabled(!busy() && inRecovery);
}

}  // namespace traceview
