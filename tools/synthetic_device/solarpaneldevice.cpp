#include "solarpaneldevice.h"

#include <QtMath>

#include <cmath>

#include "wireutil.h"

using wireutil::appendF32;
using wireutil::kFieldTypeEnum8;
using wireutil::kFieldTypeFloat32;

namespace {

constexpr quint16 kTopicEnvironment = 0x0001;
constexpr quint16 kTopicElectrical = 0x0002;
constexpr quint32 kEnvironmentMaxRateMillihz = 5000;  // 5 Hz ceiling -- sensors, no need to be fast
constexpr quint32 kElectricalMaxRateMillihz = 10000;  // 10 Hz ceiling -- fast enough for MPPT-style monitoring

// The simulated day/night cycle this whole device is driven by: one full
// cycle every kDaySeconds, sped up massively from a real 24h day so a chart
// actually shows movement during a demo instead of a flat line for hours.
constexpr double kDaySeconds = 120.0;
constexpr double kLoadWatts = 40.0;    // constant simulated house/load draw
constexpr double kBatteryCapacityWh = 200.0;
constexpr int kSimulationTickMs = 250;  // battery charge integration cadence

constexpr quint8 kPanelStatusIdle = 0;
constexpr quint8 kPanelStatusCharging = 1;
constexpr quint8 kPanelStatusFull = 2;
constexpr quint8 kPanelStatusDischarging = 3;

// Pure functions of elapsedSeconds() -- every field moves coherently off one
// shared day/night cycle rather than as independent random walks.
double sunFactorAt(double t) {
    const double phase = std::fmod(t, kDaySeconds) / kDaySeconds;
    return qMax(0.0, std::sin(2.0 * M_PI * phase));  // 0 at night, bell curve peaking at midday
}
double ambientTemperatureAt(double sun) { return 20.0 + 8.0 * sun; }
double panelTemperatureAt(double sun, double ambient) { return ambient + 20.0 * sun; }  // self-heating under sun
double irradianceAt(double sun) { return 1000.0 * sun; }                                // W/m^2, ~peak midday sun
double humidityAt(double sun) { return qBound(10.0, 70.0 - 30.0 * sun, 95.0); }          // drier at midday
double panelVoltageAt(double panelTempC) {
    return qBound(28.0, 36.5 - 0.08 * (panelTempC - 25.0), 40.0);  // Voc droops slightly as the panel heats
}
double panelCurrentAt(double sun) { return 8.6 * sun; }  // Isc proportional to irradiance

SyntheticDeviceSession::TopicSpec environmentTopic() {
    using FieldSpec = SyntheticDeviceSession::FieldSpec;
    return {kTopicEnvironment,
            QStringLiteral("panel.environment"),
            kEnvironmentMaxRateMillihz,
            {
                FieldSpec{1, 0, kFieldTypeFloat32, QStringLiteral("temperature"), QStringLiteral("Cel"), {}},
                FieldSpec{2, 1, kFieldTypeFloat32, QStringLiteral("irradiance"), QStringLiteral("W/m2"), {}},
                FieldSpec{3, 2, kFieldTypeFloat32, QStringLiteral("humidity"), QStringLiteral("%"), {}},
            }};
}

SyntheticDeviceSession::TopicSpec electricalTopic() {
    using EnumEntry = SyntheticDeviceSession::EnumEntry;
    using FieldSpec = SyntheticDeviceSession::FieldSpec;
    return {kTopicElectrical,
            QStringLiteral("panel.electrical"),
            kElectricalMaxRateMillihz,
            {
                FieldSpec{1, 0, kFieldTypeFloat32, QStringLiteral("panel_voltage"), QStringLiteral("V"), {}},
                FieldSpec{2, 1, kFieldTypeFloat32, QStringLiteral("panel_current"), QStringLiteral("A"), {}},
                FieldSpec{3, 2, kFieldTypeFloat32, QStringLiteral("panel_power"), QStringLiteral("W"), {}},
                FieldSpec{4, 3, kFieldTypeFloat32, QStringLiteral("battery_charge"), QStringLiteral("%"), {}},
                FieldSpec{5, 4, kFieldTypeFloat32, QStringLiteral("battery_voltage"), QStringLiteral("V"), {}},
                FieldSpec{6,
                          5,
                          kFieldTypeEnum8,
                          QStringLiteral("panel_status"),
                          QStringLiteral("1"),
                          {
                              EnumEntry{kPanelStatusIdle, QStringLiteral("idle")},
                              EnumEntry{kPanelStatusCharging, QStringLiteral("charging")},
                              EnumEntry{kPanelStatusFull, QStringLiteral("full")},
                              EnumEntry{kPanelStatusDischarging, QStringLiteral("discharging")},
                          }},
            }};
}

}  // namespace

SolarPanelDevice::SolarPanelDevice(quint32 sourceId, QObject* parent)
    : SyntheticDeviceSession(sourceId, QStringLiteral("solar_panel (ESP32)"), {environmentTopic(), electricalTopic()},
                            parent) {
    // Battery charge keeps integrating in the background regardless of
    // whether anything is subscribed to panel.electrical -- a real battery
    // doesn't freeze just because nobody's watching it.
    m_simulationTimer.setInterval(kSimulationTickMs);
    connect(&m_simulationTimer, &QTimer::timeout, this, &SolarPanelDevice::tickSimulation);
    m_simulationTimer.start();
}

void SolarPanelDevice::tickSimulation() {
    const double sun = sunFactorAt(elapsedSeconds());
    const double panelTemp = panelTemperatureAt(sun, ambientTemperatureAt(sun));
    const double power = panelVoltageAt(panelTemp) * panelCurrentAt(sun);

    const double dtHours = (kSimulationTickMs / 1000.0) / 3600.0;
    const double netWatts = power - kLoadWatts;
    const double deltaPct = (netWatts * dtHours / kBatteryCapacityWh) * 100.0;
    m_batteryChargePct = qBound(0.0, m_batteryChargePct + deltaPct, 100.0);
}

QByteArray SolarPanelDevice::sampleBody(quint16 topicId) {
    const double sun = sunFactorAt(elapsedSeconds());
    QByteArray body;

    if (topicId == kTopicEnvironment) {
        const double ambient = ambientTemperatureAt(sun);
        appendF32(body, float(panelTemperatureAt(sun, ambient)));
        appendF32(body, float(irradianceAt(sun)));
        appendF32(body, float(humidityAt(sun)));
        return body;
    }
    if (topicId == kTopicElectrical) {
        const double panelTemp = panelTemperatureAt(sun, ambientTemperatureAt(sun));
        const double voltage = panelVoltageAt(panelTemp);
        const double current = panelCurrentAt(sun);
        const double power = voltage * current;
        const double netWatts = power - kLoadWatts;
        const double batteryVoltage = 11.5 + (m_batteryChargePct / 100.0) * 1.6;  // 11.5V empty .. 13.1V full

        quint8 status = kPanelStatusIdle;
        if (m_batteryChargePct >= 99.5) {
            status = kPanelStatusFull;
        } else if (netWatts > 5.0) {
            status = kPanelStatusCharging;
        } else if (netWatts < -5.0) {
            status = kPanelStatusDischarging;
        }

        appendF32(body, float(voltage));
        appendF32(body, float(current));
        appendF32(body, float(power));
        appendF32(body, float(m_batteryChargePct));
        appendF32(body, float(batteryVoltage));
        body.append(static_cast<char>(status));
        return body;
    }
    return body;
}

QString SolarPanelDevice::heartbeatDetail(quint16 topicId) const {
    const double sun = sunFactorAt(elapsedSeconds());
    if (topicId == kTopicEnvironment) {
        return QStringLiteral("temperature=%1C irradiance=%2W/m2")
            .arg(panelTemperatureAt(sun, ambientTemperatureAt(sun)), 0, 'f', 1)
            .arg(irradianceAt(sun), 0, 'f', 0);
    }
    if (topicId == kTopicElectrical) {
        return QStringLiteral("battery_charge=%1 pct").arg(m_batteryChargePct, 0, 'f', 1);
    }
    return QString();
}
