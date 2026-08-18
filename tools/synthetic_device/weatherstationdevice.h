#pragma once

#include "syntheticdevicesession.h"

// Simulates a weather station, on two fixed topics: irradiance/air
// temperature/humidity/pressure, and soil humidity/wind/air quality/rain
// status. Deliberately independent of SolarPanelDevice's own simulation --
// same style of shared day/night cycle, but phase-shifted and separately
// modulated, so a chart comparing the two devices' irradiance/humidity
// readings shows genuinely different values, the way two installations far
// apart from each other would report different local weather. See
// docs/SYNTHETIC_DEVICE.md.
class WeatherStationDevice : public SyntheticDeviceSession {
    Q_OBJECT

public:
    static constexpr quint32 kDefaultSourceId = 0x57454131;

    explicit WeatherStationDevice(quint32 sourceId, QObject* parent = nullptr);

protected:
    QByteArray sampleBody(quint16 topicId) override;
    QString heartbeatDetail(quint16 topicId) const override;
};
