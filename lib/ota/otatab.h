#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

#include "devices/device.h"
#include "ota/otaclient.h"

class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;

namespace traceview {

// The OTA tab: every Device in the project, and for each one whether its
// TraceKit recovery answers, what application and recovery it has, and an
// "Upload..." that sends a firmware image to it.
//
// The device does not need to be in the recovery already. When it is not but
// TraceView is connected to it, Upload asks MainWindow to send `btp -recovery`
// (recoveryRequested), waits for the recovery to answer (up to
// kRecoveryWaitMs) and uploads then -- one click from "running the old app"
// to "running the new one". The recovery restarts into the new application by
// itself after a good upload; "To app" leaves it without uploading.
//
// The address comes from otaAddressFor() (the device's own otaAddress, the
// "ota_endpoint" it announced, or its TCP host). Uploads are signed with key
// E, derived from the same password the device's BTP session uses
// (Device::peerPassword); when that is empty, the tab asks for it once per
// session and keeps it only in memory.
//
// Status is polled at 1 Hz and only while the tab is visible (setActive),
// plus while an upload waits for its device to reach the recovery.
class OtaTab : public QWidget {
    Q_OBJECT

public:
    explicit OtaTab(QWidget* parent = nullptr);

    // Full replace, called by MainWindow on every device add/remove/update.
    // Session state -- a typed password, an upload in flight or waiting --
    // survives for every device id still present.
    void setDevices(const QVector<Device>& devices);

    // Live state only (connected, reported info): updates that row's
    // device in place, without rebuilding the table.
    void updateDevice(const Device& device);

    // Password -> key E. MainWindow hands in its cached PBKDF2
    // (MainWindow::channelKeyFor): the derivation is deliberately slow.
    void setKeyProvider(std::function<QByteArray(const QString& password)> provider);

    // Starts/stops the 1 Hz poll; turning it on also polls once at once.
    void setActive(bool active);

    // Device::id of the selected row, or empty -- for the shared Remove
    // Device action.
    QString selectedDeviceId() const;

    // Polls every row with an address now.
    void refreshNow();

    // How long an upload waits for a device sent `btp -recovery` to answer
    // as a recovery: the restart plus joining Wi-Fi.
    static constexpr int kRecoveryWaitMs = 90000;

signals:
    void selectionChanged();
    // MainWindow sends `btp -recovery` to this device's connection.
    void recoveryRequested(const QString& deviceId);
    // A one-line outcome for the status bar ("estacao: running 1.3.0").
    void statusMessage(const QString& text);

private:
    struct RowWidgets {
        QStackedWidget* uploadStack = nullptr;
        QPushButton* uploadButton = nullptr;
        QProgressBar* progressBar = nullptr;
        QPushButton* toAppButton = nullptr;
    };
    // An upload waiting for its device to restart into the recovery.
    struct Waiting {
        QString filePath;
        QByteArray keyE;
        qint64 deadlineMs = 0;
    };

    void onStatusChecked(const QString& deviceId, bool reachable, const RecoveryStatus& status,
                         const QString& errorMessage);
    void onUploadProgress(const QString& deviceId, qint64 sent, qint64 total);
    void onUploadFinished(const QString& deviceId, bool success, const QString& message);
    void onRebootFinished(const QString& deviceId, bool success, const QString& message);
    void startUpload(const QString& deviceId);
    void rebootToApp(const QString& deviceId);
    void beginUpload(const QString& deviceId, const QString& filePath, const QByteArray& keyE);
    void finishRow(const QString& deviceId);
    // Key E for this device, asking for the password when none is known.
    // Empty (and `cancelled` true) when the user cancels the prompt.
    QByteArray keyFor(const Device& device, bool* cancelled);
    const Device* deviceById(const QString& deviceId) const;

    QTableWidget* m_table;
    OtaClient* m_client;
    QTimer* m_pollTimer;
    std::function<QByteArray(const QString&)> m_keyProvider;

    QVector<Device> m_devices;                // parallel to m_table's rows
    QHash<QString, int> m_rowByDeviceId;      // Device::id -> row index
    QHash<QString, RowWidgets> m_rowWidgets;  // Device::id -> that row's cell widgets
    QHash<QString, QString> m_sessionPasswords;     // typed in the prompt, memory only
    QHash<QString, RecoveryStatus> m_inRecovery;    // last status of rows answering as a recovery
    QHash<QString, bool> m_uploading;               // an upload (or reboot) in flight
    QHash<QString, Waiting> m_waiting;              // uploads waiting for the recovery
};

}  // namespace traceview
