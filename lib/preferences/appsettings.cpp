#include "preferences/appsettings.h"

#include <QSettings>
#include <QtMath>

namespace traceview {

namespace {
constexpr char kRenderProfileKey[] = "dashboard/renderProfile";
constexpr char kCustomRenderFpsKey[] = "dashboard/customRenderFps";
constexpr char kSubscribeRateOverrideEnabledKey[] = "dashboard/subscribeRateOverrideEnabled";
constexpr char kSubscribeRateOverrideHzKey[] = "dashboard/subscribeRateOverrideHz";
constexpr char kRecentProjectsLimitKey[] = "general/recentProjectsLimit";
constexpr char kAutoConnectOnProjectOpenKey[] = "connections/autoConnectOnProjectOpen";
constexpr char kTerminalScrollbackLinesKey[] = "terminal/scrollbackLines";
constexpr char kTerminalWordWrapKey[] = "terminal/wordWrap";
constexpr char kTerminalAutoScrollKey[] = "terminal/autoScroll";
constexpr char kTerminalCursorBlinkKey[] = "terminal/cursorBlink";
constexpr char kAutoReconnectKey[] = "connections/autoReconnect";
constexpr char kReconnectIntervalSecondsKey[] = "connections/reconnectIntervalSeconds";
constexpr char kVerboseSerialLoggingKey[] = "connections/verboseSerialLogging";
constexpr char kFrameLogCapacityKey[] = "diagnostics/frameLogCapacity";
constexpr char kNotificationHistoryCapacityKey[] = "diagnostics/notificationHistoryCapacity";
constexpr char kUpdateAutoCheckEnabledKey[] = "updates/autoCheckEnabled";
constexpr char kUpdateLastCheckEpochMsKey[] = "updates/lastCheckEpochMs";
constexpr char kUpdateSkippedVersionKey[] = "updates/skippedVersion";

constexpr int kLowFps = 15;
constexpr int kMediumFps = 30;
constexpr int kHighFps = 60;
}  // namespace

AppSettings& AppSettings::instance() {
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings() = default;

AppSettings::RenderProfile AppSettings::renderProfile() const {
    const int stored = value(kRenderProfileKey, int(RenderProfile::Medium), int(RenderProfile::Low),
                             int(RenderProfile::Custom));
    return static_cast<RenderProfile>(stored);
}

int AppSettings::customRenderFps() const {
    return value(kCustomRenderFpsKey, kMediumFps, 1, 240);
}

int AppSettings::repaintIntervalMs() const {
    int fps = kMediumFps;
    switch (renderProfile()) {
        case RenderProfile::Low:
            fps = kLowFps;
            break;
        case RenderProfile::Medium:
            fps = kMediumFps;
            break;
        case RenderProfile::High:
            fps = kHighFps;
            break;
        case RenderProfile::Custom:
            fps = customRenderFps();
            break;
    }
    return qMax(1, qRound(1000.0 / fps));
}

bool AppSettings::subscribeRateOverrideEnabled() const {
    return QSettings().value(kSubscribeRateOverrideEnabledKey, false).toBool();
}

int AppSettings::subscribeRateOverrideHz() const {
    return value(kSubscribeRateOverrideHzKey, 10, 1, 1000);
}

int AppSettings::recentProjectsLimit() const {
    return value(kRecentProjectsLimitKey, 10, 1, 50);
}

bool AppSettings::autoConnectOnProjectOpen() const {
    return QSettings().value(kAutoConnectOnProjectOpenKey, true).toBool();
}

int AppSettings::terminalScrollbackLines() const {
    return value(kTerminalScrollbackLinesKey, 10000, 100, 100000);
}

bool AppSettings::terminalWordWrap() const {
    return QSettings().value(kTerminalWordWrapKey, true).toBool();
}

bool AppSettings::terminalAutoScroll() const {
    return QSettings().value(kTerminalAutoScrollKey, true).toBool();
}

bool AppSettings::terminalCursorBlink() const {
    return QSettings().value(kTerminalCursorBlinkKey, true).toBool();
}

bool AppSettings::autoReconnect() const {
    return QSettings().value(kAutoReconnectKey, true).toBool();
}

int AppSettings::reconnectIntervalSeconds() const {
    return value(kReconnectIntervalSecondsKey, 3, 1, 60);
}

bool AppSettings::verboseSerialLogging() const {
    return QSettings().value(kVerboseSerialLoggingKey, false).toBool();
}

int AppSettings::frameLogCapacity() const {
    return value(kFrameLogCapacityKey, 2000, 100, 50000);
}

int AppSettings::notificationHistoryCapacity() const {
    return value(kNotificationHistoryCapacityKey, 500, 100, 10000);
}

bool AppSettings::updateAutoCheckEnabled() const {
    return QSettings().value(kUpdateAutoCheckEnabledKey, true).toBool();
}

qint64 AppSettings::updateLastCheckEpochMs() const {
    return QSettings().value(kUpdateLastCheckEpochMsKey, 0).toLongLong();
}

QString AppSettings::updateSkippedVersion() const {
    return QSettings().value(kUpdateSkippedVersionKey).toString();
}

void AppSettings::setRenderProfile(RenderProfile profile) {
    setValue(kRenderProfileKey, int(profile));
    emit dashboardPreferencesChanged();
}

void AppSettings::setCustomRenderFps(int fps) {
    setValue(kCustomRenderFpsKey, qBound(1, fps, 240));
    if (renderProfile() == RenderProfile::Custom) {
        emit dashboardPreferencesChanged();
    }
}

void AppSettings::setSubscribeRateOverrideEnabled(bool enabled) {
    QSettings().setValue(kSubscribeRateOverrideEnabledKey, enabled);
    emit dashboardPreferencesChanged();
}

void AppSettings::setSubscribeRateOverrideHz(int hz) {
    setValue(kSubscribeRateOverrideHzKey, qBound(1, hz, 1000));
    if (subscribeRateOverrideEnabled()) {
        emit dashboardPreferencesChanged();
    }
}

void AppSettings::setRecentProjectsLimit(int limit) {
    setValue(kRecentProjectsLimitKey, qBound(1, limit, 50));
    emit generalPreferencesChanged();
}

void AppSettings::setAutoConnectOnProjectOpen(bool enabled) {
    QSettings().setValue(kAutoConnectOnProjectOpenKey, enabled);
    emit generalPreferencesChanged();
}

void AppSettings::setTerminalScrollbackLines(int lines) {
    setValue(kTerminalScrollbackLinesKey, qBound(100, lines, 100000));
    emit terminalPreferencesChanged();
}

void AppSettings::setTerminalWordWrap(bool enabled) {
    QSettings().setValue(kTerminalWordWrapKey, enabled);
    emit terminalPreferencesChanged();
}

void AppSettings::setTerminalAutoScroll(bool enabled) {
    QSettings().setValue(kTerminalAutoScrollKey, enabled);
    emit terminalPreferencesChanged();
}

void AppSettings::setTerminalCursorBlink(bool enabled) {
    QSettings().setValue(kTerminalCursorBlinkKey, enabled);
    emit terminalPreferencesChanged();
}

void AppSettings::setAutoReconnect(bool enabled) {
    QSettings().setValue(kAutoReconnectKey, enabled);
    emit connectionPreferencesChanged();
}

void AppSettings::setReconnectIntervalSeconds(int seconds) {
    setValue(kReconnectIntervalSecondsKey, qBound(1, seconds, 60));
    emit connectionPreferencesChanged();
}

void AppSettings::setVerboseSerialLogging(bool enabled) {
    QSettings().setValue(kVerboseSerialLoggingKey, enabled);
    emit connectionPreferencesChanged();
}

void AppSettings::setFrameLogCapacity(int entries) {
    setValue(kFrameLogCapacityKey, qBound(100, entries, 50000));
}

void AppSettings::setNotificationHistoryCapacity(int entries) {
    setValue(kNotificationHistoryCapacityKey, qBound(100, entries, 10000));
}

void AppSettings::setUpdateAutoCheckEnabled(bool enabled) {
    QSettings().setValue(kUpdateAutoCheckEnabledKey, enabled);
    emit updatePreferencesChanged();
}

void AppSettings::setUpdateLastCheckEpochMs(qint64 epochMs) {
    QSettings().setValue(kUpdateLastCheckEpochMsKey, epochMs);
}

void AppSettings::setUpdateSkippedVersion(const QString& version) {
    QSettings().setValue(kUpdateSkippedVersionKey, version);
}

int AppSettings::value(const char* key, int fallback, int minimum, int maximum) const {
    return qBound(minimum, QSettings().value(key, fallback).toInt(), maximum);
}

void AppSettings::setValue(const char* key, int settingValue) {
    QSettings().setValue(key, settingValue);
}

}  // namespace traceview
