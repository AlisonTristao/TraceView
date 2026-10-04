#pragma once

#include <memory>

#include "lab/benchmarks/linefollower/plant/cardynamics.h"
#include "lab/benchmarks/linefollower/plant/trackgenerator.h"
#include "lab/plant/plantenvironment.h"

namespace tracelab::lf {

class LineSensorArray;
class TrackProgress;

// The line follower's world for a plant written in C ("linefollower.track"):
// the track the Python simulator generates (seed, lemniscate or circle), how
// far along it the car got (the pygame reach rule, coverage, end of the lap),
// the preview of the points ahead (sim.preview, car frame) and the drawing
// the scene shows (sim.track, as LinePlantDevice sends it).
//
// The plant reads its world through two functions (tracelab_track.h):
//   double tl_track_distance(double x, double y)  m from a world point to
//                                                 the line's centre
//   double tl_track_line_half_width(void)         m
// and the environment reads the plant's world pose from its states x, y,
// theta (m, rad; the track frame, drawn y down: the car points along
// (cos theta, sin theta)). sim.disturb's value q reaches the code as
// tl_disturb(0) = +q (left motor) and tl_disturb(1) = -q (right).
class TrackEnvironment final : public PlantEnvironment {
public:
    static constexpr char kId[] = "linefollower.track";

    TrackEnvironment();
    ~TrackEnvironment() override;

    QString id() const override;
    QString name() const override;
    QVector<code::HostSymbol> symbols() const override;
    QStringList requiredStates() const override;
    QVector<PlantSetting> settingsSchema() const override;
    void applySettings(const QVariantMap& values) override;
    QJsonObject config() const override;
    void setConfig(const QJsonObject& config) override;
    void reset(double ts, QHash<QString, double>* initialStates) override;
    void afterStep(const CodePlant& plant) override;
    bool finished() const override;
    QVector<double> disturbances(double value) const override;
    int previewCapacity() const override;
    void preview(QVector<double>* x, QVector<double>* y) const override;
    QVector<QPair<QString, QString>> truthFields() const override;
    QVector<double> truthValues(const CodePlant& plant) const override;
    QByteArray documentTopic() const override;
    QByteArray document() const override;
    QVector<QPair<QString, QString>> statusItems() const override;

    // tl_track_distance()'s answer, for the code and the tests.
    double distance(double x, double y) const;
    double lineHalfWidth() const;
    const Track& track() const {
        return m_track;
    }

private:
    TrackParams m_trackParams;
    int m_horizon = 10;
    int m_futureSpacing = 5;
    double m_ts = 1.0 / 80.0;
    Track m_track;
    bool m_hasTrack = false;
    std::int64_t m_builtSeed = 0;
    TrackType m_builtType = TrackType::Lemniscate;
    int m_stride = 1;
    std::unique_ptr<TrackProgress> m_progress;
    std::unique_ptr<LineSensorArray> m_line;
    Pose m_pose;
    Pose m_previous;
    long long m_steps = 0;
};

// Registers "linefollower.track" with PlantEnvironmentRegistry.
void registerTrackEnvironment();

}  // namespace tracelab::lf
