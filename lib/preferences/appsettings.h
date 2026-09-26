#pragma once

#include <QObject>
#include <QString>

namespace traceview {

// Central, typed home for preferences that affect more than one UI surface.
// Appearance keeps using its dedicated managers because themes, fonts and
// translators have their own application-wide apply paths.
class AppSettings : public QObject {
    Q_OBJECT

public:
    // Stored as an int in QSettings: append new profiles at the end so a
    // saved value keeps meaning the same profile.
    enum class RenderProfile { Low, Medium, High, Custom, ExtraHigh };

    static AppSettings& instance();

    RenderProfile renderProfile() const;
    int customRenderFps() const;
    int repaintIntervalMs() const;

    // When enabled, this rate replaces every widget's own requested rate
    // (chart/text board sample time, the gauge's fixed rate) at subscribe
    // time -- one dial for the whole dashboard's subscribe load instead of
    // editing each widget's config. The server-side topic still clamps it to
    // that topic's own max/min, same as any per-widget request would be.
    bool subscribeRateOverrideEnabled() const;
    int subscribeRateOverrideHz() const;

    int recentProjectsLimit() const;
    bool autoConnectOnProjectOpen() const;

    int terminalScrollbackLines() const;
    bool terminalWordWrap() const;
    bool terminalAutoScroll() const;
    bool terminalCursorBlink() const;

    bool autoReconnect() const;
    int reconnectIntervalSeconds() const;

    // Manifest cache (BTP 2.48.0, protocol/manifeststore.h): keep each
    // device's manifest on disk so the next session asks "still revision N?"
    // and gets a tiny NOT_MODIFIED instead of the whole catalog. On by default.
    bool manifestCacheEnabled() const;
    // Direct TCP/BLE only: skip even that question when HELLO_RESULT already
    // reports the cached revision. Off by default, and off is recommended --
    // the skipped answer is tiny and is what refreshes the device's reported
    // info (firmware version etc.).
    bool manifestCacheSkipOnHello() const;
    // Whether SerialManager logs every raw byte it writes/reads (as a hex
    // dump, via AppLog's file logger) rather than just open/close/error
    // lifecycle events. Off by default -- a device streaming telemetry at a
    // high rate would otherwise fill the log file fast -- and meant to be
    // switched on only while actively chasing a connection problem.
    bool verboseSerialLogging() const;

    int frameLogCapacity() const;
    int notificationHistoryCapacity() const;

    bool updateAutoCheckEnabled() const;
    qint64 updateLastCheckEpochMs() const;

    void setRenderProfile(RenderProfile profile);
    void setCustomRenderFps(int fps);
    void setSubscribeRateOverrideEnabled(bool enabled);
    void setSubscribeRateOverrideHz(int hz);
    void setRecentProjectsLimit(int limit);
    void setAutoConnectOnProjectOpen(bool enabled);
    void setTerminalScrollbackLines(int lines);
    void setTerminalWordWrap(bool enabled);
    void setTerminalAutoScroll(bool enabled);
    void setTerminalCursorBlink(bool enabled);
    void setAutoReconnect(bool enabled);
    void setReconnectIntervalSeconds(int seconds);
    void setManifestCacheEnabled(bool enabled);
    void setManifestCacheSkipOnHello(bool skip);
    void setVerboseSerialLogging(bool enabled);
    void setFrameLogCapacity(int entries);
    void setNotificationHistoryCapacity(int entries);

    void setUpdateAutoCheckEnabled(bool enabled);
    void setUpdateLastCheckEpochMs(qint64 epochMs);

signals:
    void dashboardPreferencesChanged();
    void generalPreferencesChanged();
    void terminalPreferencesChanged();
    void connectionPreferencesChanged();
    void updatePreferencesChanged();

private:
    AppSettings();

    int value(const char* key, int fallback, int minimum, int maximum) const;
    void setValue(const char* key, int value);
};

}  // namespace traceview
