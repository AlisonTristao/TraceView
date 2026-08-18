#pragma once

#include "syntheticdevicesession.h"

#include <QTimer>

// Simulates a solar/photovoltaic panel + battery installation, on two fixed
// topics driven off one shared, deterministic day/night cycle: irradiance
// rises and falls in a bell curve, the panel heats up and generates current
// in proportion, and the battery charges or drains depending on whether
// generation currently exceeds a fixed simulated load. See
// docs/SYNTHETIC_DEVICE.md.
class SolarPanelDevice : public SyntheticDeviceSession {
    Q_OBJECT

public:
    static constexpr quint32 kDefaultSourceId = 0x53594E31;

    explicit SolarPanelDevice(quint32 sourceId, QObject* parent = nullptr);

protected:
    QByteArray sampleBody(quint16 topicId) override;
    QString heartbeatDetail(quint16 topicId) const override;

private:
    void tickSimulation();

    // The one quantity that genuinely integrates over time rather than being
    // a pure function of elapsedSeconds() -- updated on every
    // m_simulationTimer tick regardless of whether anything is subscribed to
    // panel.electrical, so it keeps evolving in the background the same way
    // a real battery would.
    QTimer m_simulationTimer;
    double m_batteryChargePct = 55.0;
};
