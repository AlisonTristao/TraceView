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
    enum class RenderProfile { Low, Medium, High, Custom };

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
    QString updateSkippedVersion() const;

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
    void setVerboseSerialLogging(bool enabled);
    void setFrameLogCapacity(int entries);
    void setNotificationHistoryCapacity(int entries);

    void setUpdateAutoCheckEnabled(bool enabled);
    void setUpdateLastCheckEpochMs(qint64 epochMs);
    void setUpdateSkippedVersion(const QString& version);

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
