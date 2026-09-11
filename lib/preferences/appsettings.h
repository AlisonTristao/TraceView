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

    int recentProjectsLimit() const;
    bool autoConnectOnProjectOpen() const;

    int terminalScrollbackLines() const;
    bool terminalWordWrap() const;
    bool terminalAutoScroll() const;
    bool terminalCursorBlink() const;

    bool autoReconnect() const;
    int reconnectIntervalSeconds() const;

    int frameLogCapacity() const;
    int notificationHistoryCapacity() const;

    bool updateAutoCheckEnabled() const;
    qint64 updateLastCheckEpochMs() const;
    QString updateSkippedVersion() const;

    void setRenderProfile(RenderProfile profile);
    void setCustomRenderFps(int fps);
    void setRecentProjectsLimit(int limit);
    void setAutoConnectOnProjectOpen(bool enabled);
    void setTerminalScrollbackLines(int lines);
    void setTerminalWordWrap(bool enabled);
    void setTerminalAutoScroll(bool enabled);
    void setTerminalCursorBlink(bool enabled);
    void setAutoReconnect(bool enabled);
    void setReconnectIntervalSeconds(int seconds);
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
