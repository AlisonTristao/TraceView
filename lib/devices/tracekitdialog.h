#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

#include "devices/device.h"

class QLabel;
class QListWidget;
class QStackedWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QWidget;

namespace traceview {

// True when the device's manifest says it runs TraceKit (source_info key
// "tracekit", its version) -- the devices this dialog and the card's tools
// button are for.
bool isTraceKitDevice(const QVector<DeviceInfoRecord>& info);

// The TraceKit panel of one device: what a TraceKit firmware lets TraceView
// change on it, read from the device itself so it fits whatever that device
// declares.
//
//   Settings  every value its App::settings() declared (`cfg -json <i>`, one
//             per command: a COMMAND_RESULT is under 1 KB), with an editor
//             that matches the type and range; Apply sends `cfg -set`.
//   Wi-Fi     the networks it knows (`btp -wifi_json`, names only -- the
//             passwords never come back), add and remove.
//   Actions   clock, reboot, recovery, USB bootloader, keep awake, factory
//             reset, each one command.
//
// MainWindow adds pages that need more than this library has (addPage): the
// Firmware page (OtaDevicePanel, traceview_ota) is one, and works whether or
// not the device is connected -- unlike the three above, which go grey.
//
// It never talks to a connection itself: `Runner` sends a command line and
// hands back its output (MainWindow wires it to Backend::runCommand), so
// this stays in traceview_devices without the protocol layer.
class TraceKitDialog : public QDialog {
    Q_OBJECT

public:
    using ResultFn = std::function<void(bool ok, const QString& output)>;
    using Runner = std::function<void(const QString& commandLine, ResultFn done)>;

    TraceKitDialog(const Device& device, Runner runner, QWidget* parent = nullptr);

    // Reloads everything when the device (re)connects; greys out the pages
    // that need the connection while it is not.
    void setConnected(bool connected);

    // Live state for the header (connection, firmware version).
    void setDevice(const Device& device);

    // A page of the caller's, in the side list before Actions; it stays
    // enabled when the device is disconnected. Returns its index.
    int addPage(QWidget* page, const QString& title, const QString& iconId);
    void showPage(int index);

private:
    struct Setting {
        QJsonObject decl;       // as `cfg -json` describes it
        QWidget* editor = nullptr;
        QString loaded;         // the value text it had when read
    };

    void addNavItem(const QString& title, const QString& iconId, int row = -1);
    void refreshHeader();
    void applyTheme();
    void refreshNavIcons();
    QWidget* buildSettingsTab();
    QWidget* buildWifiTab();
    QWidget* buildActionsTab();
    void sizeActionButtons();

    void reloadAll();
    void loadSetting(int index);
    void addSettingRow(const QJsonObject& decl);
    QString editorText(const Setting& setting) const;
    void applySettings();
    void applyNext(QVector<int> rows, QStringList report);
    void resetSetting(int row);
    void reloadWifi();
    void addWifi();
    void removeWifi();
    // Runs `line`, shows its output in the Actions log.
    void action(const QString& line);
    void log(const QString& text);

    Device m_device;
    Runner m_runner;
    bool m_connected = false;
    int m_generation = 0;  // bumped on every reload: late answers of an old one are dropped

    QListWidget* m_nav = nullptr;
    QStackedWidget* m_pages = nullptr;
    QVector<QWidget*> m_connectionPages;  // greyed out while disconnected
    QStringList m_navIcons;               // lucide id per nav row
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_linkBadge = nullptr;
    QLabel* m_settingsState = nullptr;
    QTableWidget* m_settingsTable = nullptr;
    QPushButton* m_applyButton = nullptr;
    QVector<Setting> m_settings;
    QLabel* m_wifiState = nullptr;
    QListWidget* m_wifiList = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QVector<QPushButton*> m_actionButtons;  // one size for all (sizeActionButtons)
};

}  // namespace traceview
