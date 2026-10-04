#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace tracelab {

class LoopbackTransport;

// How a plant's clock runs (plan 4.2).
enum class RunMode {
    RealTime = 0,          // ticks at Ts / timeScale of wall time
    AsFastAsPossible = 1,  // one tick per event-loop turn, never waits
    Lockstep = 2,          // as fast as possible, but tick k+1 waits for the
                           // loop.u answering tick k (in REMOTE mode)
};

// One setting of a plant shown on its card in the TraceLab tab: what it is,
// its value and range. Applied together on the next reset
// (LabPlantDevice::applySettings()).
struct PlantSetting {
    enum class Type { Int, Double, Enum, Bool };
    QString key;      // stable, the one applySettings() takes
    QString label;    // translated
    QString unit;
    QString toolTip;  // translated, may be empty
    Type type = Type::Double;
    QVariant value;   // int, double, the option's index (Enum) or bool
    double minimum = 0.0;
    double maximum = 0.0;
    QStringList options;  // Enum: translated labels
};

// A simulated plant as the TraceLab tab and the controller devices see it,
// whatever it simulates: its clock (run, pause, step, speed, sample rate),
// the BTP link a controller host attaches to, its own settings described as
// data, and a few status values. Each kind of plant (a benchmark's device)
// implements it; nothing here knows what the plant is.
class LabPlantDevice : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // The kind of plant, translated ("Line follower").
    virtual QString typeName() const = 0;

    // The controller's link: `peer` is the plant's end of a connected
    // loopback pair; the device takes it over. A new one replaces the old.
    virtual void attachController(LoopbackTransport* peer) = 0;

    virtual bool running() const = 0;
    virtual long long tick() const = 0;
    // Simulated time since the reset, s.
    virtual double time() const = 0;

    // Ticks per second (Ts = 1 / rate). Setting it restarts the run.
    virtual int sampleRate() const = 0;
    virtual void setSampleRate(int hz) = 0;
    double sampleTime() const {
        return sampleRate() > 0 ? 1.0 / sampleRate() : 0.0;
    }

    virtual RunMode runMode() const = 0;
    virtual double timeScale() const = 0;
    virtual void setRunMode(RunMode mode, double timeScale) = 0;

    // The plant's own settings (the track, sensor noise...), with their
    // current values.
    virtual QVector<PlantSetting> settingsSchema() const {
        return {};
    }
    // Takes the values by key (missing keys keep theirs) and resets.
    virtual void applySettings(const QVariantMap& values) {
        Q_UNUSED(values);
    }

    // Short status values for the card next to tick and time, as (caption,
    // text): "covered" "63.2 %".
    virtual QVector<QPair<QString, QString>> statusItems() const {
        return {};
    }

    // What the project stores for this device (Device::moduleConfig).
    virtual QJsonObject config() const = 0;

public slots:
    virtual void run() = 0;
    virtual void pause() = 0;
    // Back to tick 0 with the current settings; keeps running or paused.
    virtual void reset() = 0;
    // Exactly one tick (while paused).
    virtual void step() = 0;

signals:
    void runningChanged(bool running);
    // After every tick (and after reset(), with tick 0).
    void ticked(long long tick);
    // settingsSchema(), sampleRate(), runMode() or config() changed.
    void settingsChanged();
};

}  // namespace tracelab
