#include "weatherstationdevice.h"

#include <QtMath>

#include <cmath>

#include "wireutil.h"

using wireutil::appendF32;
using wireutil::appendLe;
using wireutil::kFieldTypeEnum8;
using wireutil::kFieldTypeFloat32;
using wireutil::kFieldTypeUInt16;

namespace {

constexpr quint16 kTopicEnvironment = 0x0001;
constexpr quint16 kTopicGround = 0x0002;
constexpr quint32 kEnvironmentMaxRateMillihz = 5000;  // 5 Hz ceiling
constexpr quint32 kGroundMaxRateMillihz = 2000;       // 2 Hz ceiling -- soil/wind don't need to be fast

// Same style of day/night cycle SolarPanelDevice uses, but phase-shifted and
// separately cloud-modulated: this station is somewhere else, so its
// irradiance/humidity genuinely differ from the panel's even though both are
// driven by "the sun", the way two real, physically distant installations
// would report different local weather.
constexpr double kDaySeconds = 120.0;
constexpr double kPhaseOffsetSeconds = 23.0;  // this station's local "solar noon" lags the panel's

constexpr quint8 kRainNone = 0;
constexpr quint8 kRainLight = 1;
constexpr quint8 kRainModerate = 2;
constexpr quint8 kRainHeavy = 3;

double sunFactorAt(double t) {
    const double shifted = t + kPhaseOffsetSeconds;
    const double phase = std::fmod(shifted, kDaySeconds) / kDaySeconds;
    const double base = qMax(0.0, std::sin(2.0 * M_PI * phase));
    const double clouds = 0.85 + 0.15 * std::sin(t * 0.37);  // independent slow cloud modulation
    return base * clouds;
}
double irradianceAt(double sun) { return 950.0 * sun; }
double airTemperatureAt(double sun) { return 18.0 + 10.0 * sun; }
double airHumidityAt(double sun) { return qBound(15.0, 80.0 - 35.0 * sun, 98.0); }
double atmosphericPressureAt(double t) { return 1013.0 + 5.0 * std::sin(t * 0.05); }
double soilHumidityAt(double t, double sun) { return qBound(5.0, 40.0 - 8.0 * sun + 4.0 * std::sin(t * 0.02), 90.0); }
double windSpeedAt(double t) { return qMax(0.0, 3.5 + 2.5 * std::sin(t * 0.9) + 1.0 * std::sin(t * 2.3)); }
double windDirectionAt(double t) { return std::fmod(t * 6.0, 360.0); }
double airQualityIndexAt(double sun, double windSpeed) { return qBound(5.0, 80.0 - 8.0 * windSpeed + 20.0 * sun, 300.0); }
quint8 rainStatusAt(double humidity, double pressure) {
    if (pressure < 1008.0 && humidity > 75.0) {
        return kRainHeavy;
    }
    if (pressure < 1011.0 && humidity > 65.0) {
        return kRainModerate;
    }
    if (humidity > 55.0) {
        return kRainLight;
    }
    return kRainNone;
}

SyntheticDeviceSession::TopicSpec environmentTopic() {
    using FieldSpec = SyntheticDeviceSession::FieldSpec;
    return {kTopicEnvironment,
            QStringLiteral("weather.environment"),
            kEnvironmentMaxRateMillihz,
            {
                FieldSpec{1, 0, kFieldTypeFloat32, QStringLiteral("irradiance"), QStringLiteral("W/m2"), {}},
                FieldSpec{2, 1, kFieldTypeFloat32, QStringLiteral("air_temperature"), QStringLiteral("Cel"), {}},
                FieldSpec{3, 2, kFieldTypeFloat32, QStringLiteral("air_humidity"), QStringLiteral("%"), {}},
                FieldSpec{
                    4, 3, kFieldTypeFloat32, QStringLiteral("atmospheric_pressure"), QStringLiteral("hPa"), {}},
            }};
}

SyntheticDeviceSession::TopicSpec groundTopic() {
    using EnumEntry = SyntheticDeviceSession::EnumEntry;
    using FieldSpec = SyntheticDeviceSession::FieldSpec;
    return {kTopicGround,
            QStringLiteral("weather.ground"),
            kGroundMaxRateMillihz,
            {
                FieldSpec{1, 0, kFieldTypeFloat32, QStringLiteral("soil_humidity"), QStringLiteral("%"), {}},
                FieldSpec{2, 1, kFieldTypeFloat32, QStringLiteral("wind_speed"), QStringLiteral("m/s"), {}},
                FieldSpec{3, 2, kFieldTypeFloat32, QStringLiteral("wind_direction"), QStringLiteral("deg"), {}},
                FieldSpec{
                    4, 3, kFieldTypeUInt16, QStringLiteral("air_quality_index"), QStringLiteral("1"), {}},
                FieldSpec{5,
                          4,
                          kFieldTypeEnum8,
                          QStringLiteral("rain_status"),
                          QStringLiteral("1"),
                          {
                              EnumEntry{kRainNone, QStringLiteral("none")},
                              EnumEntry{kRainLight, QStringLiteral("light")},
                              EnumEntry{kRainModerate, QStringLiteral("moderate")},
                              EnumEntry{kRainHeavy, QStringLiteral("heavy")},
                          }},
            }};
}

}  // namespace

WeatherStationDevice::WeatherStationDevice(quint32 sourceId, QObject* parent)
    : SyntheticDeviceSession(sourceId, QStringLiteral("weather_station (ESP32-S3)"),
                            {environmentTopic(), groundTopic()}, parent) {}

QByteArray WeatherStationDevice::sampleBody(quint16 topicId) {
    const double t = elapsedSeconds();
    const double sun = sunFactorAt(t);
    QByteArray body;

    if (topicId == kTopicEnvironment) {
        appendF32(body, float(irradianceAt(sun)));
        appendF32(body, float(airTemperatureAt(sun)));
        appendF32(body, float(airHumidityAt(sun)));
        appendF32(body, float(atmosphericPressureAt(t)));
        return body;
    }
    if (topicId == kTopicGround) {
        const double wind = windSpeedAt(t);
        const double humidity = airHumidityAt(sun);
        const double pressure = atmosphericPressureAt(t);
        appendF32(body, float(soilHumidityAt(t, sun)));
        appendF32(body, float(wind));
        appendF32(body, float(windDirectionAt(t)));
        appendLe(body, quint32(airQualityIndexAt(sun, wind)), 2);
        body.append(static_cast<char>(rainStatusAt(humidity, pressure)));
        return body;
    }
    return body;
}

QString WeatherStationDevice::heartbeatDetail(quint16 topicId) const {
    const double t = elapsedSeconds();
    const double sun = sunFactorAt(t);
    if (topicId == kTopicEnvironment) {
        return QStringLiteral("irradiance=%1W/m2 humidity=%2 pct")
            .arg(irradianceAt(sun), 0, 'f', 0)
            .arg(airHumidityAt(sun), 0, 'f', 0);
    }
    if (topicId == kTopicGround) {
        const double wind = windSpeedAt(t);
        return QStringLiteral("wind=%1m/s aqi=%2").arg(wind, 0, 'f', 1).arg(int(airQualityIndexAt(sun, wind)));
    }
    return QString();
}
