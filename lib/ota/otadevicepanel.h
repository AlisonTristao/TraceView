#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

#include "devices/device.h"
#include "ota/otaclient.h"

class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace traceview {

class StatusBadge;

class OtaStepIndicator;

// What the first bytes of an ESP application image say about it: the chip
// it was built for (image header) and the esp_app_desc_t the build stamps in
// right after the first segment header -- project, version, build date.
// `valid` is false when the file is not an ESP image at all.
struct FirmwareImageInfo {
    bool valid = false;
    QString error;      // why it is not usable, when !valid
    qint64 size = 0;
    QString chip;       // "esp32s3", ... -- empty for a chip id this build does not know
    QString project;    // empty when the image carries no app descriptor
    QString version;
    QString built;      // "Nov 20 2026 18:39:07"
    QString idfVersion;
};
FirmwareImageInfo readFirmwareImageInfo(const QString& filePath);

// The Firmware page of one TraceKit device's panel (TraceKitDialog): what is
// installed, a .bin to pick or drop, and one "Install" that takes the device
// from running its application to running the new one -- restart into the
// recovery (`btp -recovery`, through MainWindow: recoveryRequested), upload
// signed with key E, recovery starts the new application. The same flow as
// a row of the OTA tab (OtaTab), for one device and with room to say what is
// happening.
//
// Polls the recovery's /status at 1 Hz while the page is visible, and while
// an install is under way whether visible or not.
class OtaDevicePanel : public QWidget {
    Q_OBJECT

public:
    explicit OtaDevicePanel(const Device& device, QWidget* parent = nullptr);

    // Live state (connected, reported info, address). Also how the page
    // notices the new firmware came back after an install.
    void setDevice(const Device& device);

    // Password -> key E (MainWindow::channelKeyFor, cached PBKDF2).
    void setKeyProvider(std::function<QByteArray(const QString& password)> provider);

    // Same budget as OtaTab::kRecoveryWaitMs: the restart plus joining Wi-Fi.
    static constexpr int kRecoveryWaitMs = 90000;
    // After a good upload: how long "Starting the new firmware" waits for the
    // device to connect again before it just says the upload went through.
    static constexpr int kBootWaitMs = 60000;

signals:
    // MainWindow sends `btp -recovery` over this device's connection.
    void recoveryRequested(const QString& deviceId);
    void statusMessage(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Phase { Idle, Restarting, Uploading, Starting, Done, Failed };

    void buildUi();
    void applyTheme();
    void poll();
    void onStatusChecked(const QString& deviceId, bool reachable, const RecoveryStatus& status,
                         const QString& errorMessage);
    void onUploadProgress(const QString& deviceId, qint64 sent, qint64 total);
    void onUploadFinished(const QString& deviceId, bool success, const QString& message);
    void onRebootFinished(const QString& deviceId, bool success, const QString& message);

    void chooseFile();
    void setFile(const QString& filePath);
    void install();
    void beginUpload(const QByteArray& keyE);
    void backToApp();
    QByteArray keyFor(bool* cancelled);
    void setPhase(Phase phase, const QString& detail = QString());

    // Re-renders everything from the current state.
    void sizeButtons();
    void refreshView();
    void refreshDeviceCard();
    void refreshFileCard();
    void refreshProgress();
    void refreshButtons();
    bool busy() const;
    QString reported(const char* key) const;
    QString installedVersion() const;

    Device m_device;
    std::function<QByteArray(const QString&)> m_keyProvider;
    OtaClient* m_client;
    QTimer* m_pollTimer;

    // Last /status, while the device answers as a recovery.
    std::optional<RecoveryStatus> m_recovery;
    QString m_lastError;
    bool m_checkedOnce = false;
    QString m_sessionPassword;  // typed in the prompt, memory only

    QString m_filePath;
    FirmwareImageInfo m_image;

    Phase m_phase = Phase::Idle;
    QString m_phaseDetail;
    qint64 m_deadlineMs = 0;
    QByteArray m_pendingKey;  // key E kept while waiting for the recovery
    bool m_wasConnected = false;
    int m_percent = 0;
    qint64 m_sendTotal = 0;  // bytes on the wire: smaller than the image when compressed
    int m_failedStep = -1;  // the step a Failed phase stopped at, -1 for none

    // Widgets.
    QFrame* m_deviceCard = nullptr;
    StatusBadge* m_stateBadge = nullptr;
    QLabel* m_stateHint = nullptr;
    QLabel* m_appValue = nullptr;
    QLabel* m_builtValue = nullptr;
    QLabel* m_recoveryValue = nullptr;
    QLabel* m_addressValue = nullptr;

    QFrame* m_fileCard = nullptr;
    QFrame* m_dropZone = nullptr;
    QLabel* m_dropIcon = nullptr;
    QLabel* m_dropText = nullptr;
    QPushButton* m_chooseButton = nullptr;
    QWidget* m_fileDetails = nullptr;
    QLabel* m_fileName = nullptr;
    QLabel* m_fileMeta = nullptr;
    QLabel* m_fileCheck = nullptr;
    QProgressBar* m_fitBar = nullptr;
    QLabel* m_fitText = nullptr;
    bool m_dragOver = false;

    OtaStepIndicator* m_steps = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressText = nullptr;
    QPushButton* m_toAppButton = nullptr;
    QPushButton* m_installButton = nullptr;
};

}  // namespace traceview
