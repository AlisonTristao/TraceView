#pragma once

#include <QHash>
#include <QVector>
#include <QWidget>

#include "devices/device.h"

class QCheckBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;

namespace traceview {

class OtaClient;

// The OTA tab's content -- lists every Device in the project (see
// setDevices()), polls bally_OS's GET /status for whichever ones have an
// otaAddress configured (Device::otaAddress, edited via
// DeviceConfigDialog's OTA group) while this tab is the visible one, and
// lets the user type each device's OTA password and POST /update a firmware
// file with a live progress readout. Built the same way LogViewer is (a
// single QTableWidget, no undo stack of its own) -- unlike LogViewer,
// though, one field it shows (the password) can flow back into the actual
// Device via passwordCacheChanged(), the one point where this tab commits a
// real project mutation.
class OtaTab : public QWidget {
    Q_OBJECT

public:
    explicit OtaTab(QWidget* parent = nullptr);

    // Full replace, called once up front by MainWindow and again on every
    // DevicesGrid::deviceAdded/Removed/Updated so this tab never shows a
    // stale device list. Session-only state that isn't itself part of
    // Device -- a typed-but-not-yet-cached password, an in-flight upload's
    // progress -- survives the rebuild for any device id that's still
    // present, keyed off Device::id rather than row position.
    void setDevices(const QVector<Device>& devices);

    // Starts/stops the background status-poll timer -- called by MainWindow
    // from onRibbonTabChanged() so this tab only generates HTTP traffic
    // while it's actually the visible one. Turning it on also triggers one
    // immediate poll pass instead of waiting a full interval.
    void setActive(bool active);

    // Device::id of the currently selected row, or empty if nothing's
    // selected -- MainWindow's shared Remove Device action (see
    // buildRibbon()/onOpenOtaTab()) reads this the same way it reads
    // DevicesGrid::selectedCount()/removeSelected() for the Devices tab.
    QString selectedDeviceId() const;

    // Polls every configured row immediately instead of waiting for the next
    // tick of the background timer -- called by setActive(true) so switching
    // to this tab doesn't sit on stale "Checking..." text for a full second.
    // A no-op row-by-row for anything with no address configured or an
    // upload already in flight, same as the timer-driven poll.
    void refreshNow();

signals:
    // Forwarded from the table's own itemSelectionChanged -- MainWindow
    // uses this to keep the shared Remove Device action's enabled state in
    // sync, same role DevicesGrid::selectionChanged plays for the Devices
    // tab.
    void selectionChanged();
    // Fired when the user toggles a row's "remember" checkbox, or when an
    // upload finishes successfully while that checkbox is checked (so a
    // password edited after checking the box is still the one that gets
    // saved). MainWindow is the one that actually calls
    // DevicesGrid::updateDevice() in response -- this tab has no undo stack
    // of its own to push that mutation onto.
    void passwordCacheChanged(const QString& deviceId, const QString& password, bool cache);

private:
    struct RowWidgets {
        QLineEdit* passwordEdit = nullptr;
        QCheckBox* rememberCheck = nullptr;
        QStackedWidget* uploadStack = nullptr;
        QPushButton* uploadButton = nullptr;
        QProgressBar* progressBar = nullptr;
    };

    void onStatusChecked(const QString& deviceId, bool reachable, bool otaReady, const QString& firmwareVersion,
                          const QString& errorMessage);
    void onUploadProgress(const QString& deviceId, qint64 sent, qint64 total);
    void onUploadFinished(const QString& deviceId, bool success, const QString& message);
    void startUpload(const QString& deviceId);

    QTableWidget* m_table;
    OtaClient* m_client;
    QTimer* m_pollTimer;

    QVector<Device> m_devices;                  // parallel to m_table's rows
    QHash<QString, int> m_rowByDeviceId;         // Device::id -> row index
    QHash<QString, RowWidgets> m_rowWidgets;     // Device::id -> that row's cell widgets
    QHash<QString, QString> m_sessionPasswords;  // typed-but-not-cached passwords, survives setDevices()
    QHash<QString, bool> m_uploading;            // Device::id -> upload currently in flight
};

}  // namespace traceview
