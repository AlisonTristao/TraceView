#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QWidget>
#include <functional>

#include "diagnostics/framelogentry.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableView;
class QToolButton;

namespace traceview {

class FrameLog;

// Table model mirroring a FrameLog, with the monitor's three filters applied
// internally (no proxy): hide-TELEMETRY, a device-name filter, and a
// substring text filter matched against the composed "type objectId src
// payloadHex errorText" of each row. Rows are kept as copies so the model
// survives the log's ring eviction; `seq` on each entry is the stable identity
// the view selects by.
class FrameTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        SeqColumn = 0,
        TimeColumn,
        DirectionColumn,
        DeviceColumn,
        TypeColumn,
        ObjectColumn,
        SequenceColumn,
        FlagsColumn,
        LengthColumn,
        PayloadColumn,
        ColumnCount
    };

    explicit FrameTableModel(FrameLog* log, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // nullptr for an out-of-range row.
    const FrameLogEntry* entryForRow(int row) const;

    void setTelemetryHidden(bool hidden);
    void setDeviceFilter(const QString& deviceName);  // empty = all
    void setTextFilter(const QString& text);          // empty = all
    void setPaused(bool paused);                      // resume re-syncs from the log

signals:
    // Emitted whenever a device name is seen for the first time, so the tab can
    // keep its device filter combo current without knowing the device list.
    void deviceSeen(const QString& deviceName);

private:
    void onEntryAdded(const FrameLogEntry& entry, bool droppedOldest);
    void rebuildVisible();
    void syncFromLog();
    bool rowPasses(const FrameLogEntry& entry) const;

    FrameLog* m_log;
    QVector<FrameLogEntry> m_all;
    QVector<int> m_visible;  // indices into m_all
    bool m_telemetryHidden = false;
    QString m_deviceFilter;
    QString m_textFilter;
    bool m_paused = false;
    QSet<QString> m_seenDevices;
};

// The "BTP Traffic Monitor" ribbon tab (File menu, singleton closable tab --
// same lifecycle as the OTA tab). A live table of every BTP frame each device
// connection sends and receives, a per-frame detail view (decoded header +
// hex dump), and a Decrypt box that derives a channel-B key from a typed
// password and tries to open the selected frame's sealed payload.
class BtpMonitorTab : public QWidget {
    Q_OBJECT

public:
    using PasswordProvider = std::function<QString(const QString& deviceId)>;

    explicit BtpMonitorTab(FrameLog* log, PasswordProvider passwordProvider,
                           QWidget* parent = nullptr);

private:
    void onSelectionChanged();
    void onDecryptClicked();
    void onExportClicked();
    void addDeviceToFilter(const QString& deviceName);
    QByteArray keyForPassword(const QString& password);

    FrameLog* m_log;
    PasswordProvider m_passwordProvider;

    FrameTableModel* m_model = nullptr;
    QTableView* m_table = nullptr;
    QCheckBox* m_hideTelemetry = nullptr;
    QComboBox* m_deviceFilter = nullptr;
    QLineEdit* m_textFilter = nullptr;
    QPushButton* m_pauseButton = nullptr;

    QPlainTextEdit* m_detail = nullptr;
    QLineEdit* m_password = nullptr;
    QToolButton* m_revealPassword = nullptr;
    QPushButton* m_decryptButton = nullptr;
    QPlainTextEdit* m_decryptResult = nullptr;

    // Cache: PBKDF2 is ~200k iterations, far too slow to run per click.
    QHash<QString, QByteArray> m_keyCache;
};

}  // namespace traceview
