#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariant>

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
    // Which notifications float over the dashboard as cards; every one of
    // them lands behind the bell either way (NotificationPanel). Stored as
    // an int: append only.
    enum class NotificationPopups { All, BellOnly };
    // A pinned notification panel docked to an edge either floats over the
    // dashboard like the other panels (Overlay) or takes its strip away
    // from the canvas, which then shrinks so nothing is hidden (Push).
    enum class NotificationPinStyle { Overlay, Push };

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

    // Whether AI assistants (Claude and other MCP clients, via `TraceView
    // --mcp`) may drive this app -- see docs/MCP.md. Off by default: turning
    // it on is the user's consent to being driven.
    bool agentBridgeEnabled() const;
    // Whether an AI assistant's hardware access and destructive actions go
    // through without asking. Deliberately not in the update_settings tool's
    // list: only the user can grant it, never the assistant itself.
    bool agentAutoApprove() const;

    int frameLogCapacity() const;
    int notificationHistoryCapacity() const;

    // Default BellOnly with errors still popping up: the dashboard stays
    // clear, and only what needs attention interrupts it.
    NotificationPopups notificationPopups() const;
    // In BellOnly mode, whether Error notifications still pop up as cards.
    bool notificationErrorPopups() const;
    NotificationPinStyle notificationPinStyle() const;
    // The panel stays open (and comes back open on the next start).
    bool notificationPanelPinned() const;

    bool updateAutoCheckEnabled() const;
    qint64 updateLastCheckEpochMs() const;

    // The app-wide chart style (View > Chart Style): "dashboard",
    // "engineering" or "scientific", see ChartStyleId in
    // dashboard/widgets/chartstyle.h. Every chart whose own gear menu is left
    // at "App default" draws in it. Kept as the plain id string so this
    // library stays independent of the dashboard one.
    QString chartStyleId() const;

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
    void setAgentBridgeEnabled(bool enabled);
    void setAgentAutoApprove(bool enabled);
    void setFrameLogCapacity(int entries);
    void setNotificationHistoryCapacity(int entries);
    void setNotificationPopups(NotificationPopups popups);
    void setNotificationErrorPopups(bool enabled);
    void setNotificationPinStyle(NotificationPinStyle style);
    void setNotificationPanelPinned(bool pinned);

    void setUpdateAutoCheckEnabled(bool enabled);
    void setUpdateLastCheckEpochMs(qint64 epochMs);

    void setChartStyleId(const QString& id);

signals:
    void dashboardPreferencesChanged();
    void generalPreferencesChanged();
    void terminalPreferencesChanged();
    void connectionPreferencesChanged();
    void updatePreferencesChanged();
    void agentPreferencesChanged();
    // Its own signal rather than dashboardPreferencesChanged(): charts only
    // need a repaint, not the resubscribe that one triggers.
    void chartStyleChanged();
    void notificationPreferencesChanged();

private:
    AppSettings();

    int value(const char* key, int fallback, int minimum, int maximum) const;
    void setValue(const char* key, int value);

    // Every getter goes through stored(): the first read of a key hits
    // QSettings (registry / INI), later ones come from m_cache. Hot paths read
    // these per paint or per serial chunk (chart style, verbose logging), and
    // a fresh QSettings each time is far from free. Only this class writes
    // these keys, so store() keeps the cache exact.
    QVariant stored(const char* key, const QVariant& fallback) const;
    void store(const char* key, const QVariant& value);

    mutable QMutex m_cacheMutex;  // the Android serial transport may read off the UI thread
    mutable QHash<QByteArray, QVariant> m_cache;
};

}  // namespace traceview
