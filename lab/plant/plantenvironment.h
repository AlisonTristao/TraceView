#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <functional>
#include <memory>

#include "lab/code/codemodule.h"
#include "lab/plant/labplantdevice.h"

namespace tracelab {

class CodePlant;

// The world a plant written in C lives in, when there is more to it than
// equations: the line follower's track -- its geometry the code's sensors
// query, how far along it the car got, the preview a controller may use and
// the drawing the scene shows. Native C++ (one per benchmark), registered by
// id; a CodePlantDevice owns at most one. It reads the plant through its
// states (requiredStates(), by name) and talks to the code through the
// functions symbols() adds to it (tracelab.h's CodeContext::environment
// points at the environment while the plant runs).
class PlantEnvironment {
public:
    virtual ~PlantEnvironment() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;  // translated

    // The functions the plant's code may call, with the header that
    // declares them (#include <...> in the code).
    virtual QVector<code::HostSymbol> symbols() const {
        return {};
    }
    // States the plant must declare (by name) for this environment.
    virtual QStringList requiredStates() const {
        return {};
    }

    // Its settings, applied on the next reset (by the device).
    virtual QVector<PlantSetting> settingsSchema() const {
        return {};
    }
    virtual void applySettings(const QVariantMap& values) {
        Q_UNUSED(values);
    }
    // What the project stores; setConfig() takes it back.
    virtual QJsonObject config() const {
        return {};
    }
    virtual void setConfig(const QJsonObject& config) {
        Q_UNUSED(config);
    }

    // A new run with sample time `ts` (a new track when its settings
    // changed). Fills the states it places (the car on the track's start).
    virtual void reset(double ts, QHash<QString, double>* initialStates) {
        Q_UNUSED(ts);
        Q_UNUSED(initialStates);
    }
    // After every plant step: where the plant got.
    virtual void afterStep(const CodePlant& plant) {
        Q_UNUSED(plant);
    }
    // The run is over (the lap completed); the device pauses.
    virtual bool finished() const {
        return false;
    }

    // The channels tl_disturb() gives the code for sim.disturb's value (%):
    // by default that value on channel 0.
    virtual QVector<double> disturbances(double value) const {
        return {value};
    }

    // sim.preview: up to previewCapacity() points of the reference ahead, in
    // the plant's frame (x lateral right, y forward). 0: no preview topic.
    virtual int previewCapacity() const {
        return 0;
    }
    virtual void preview(QVector<double>* x, QVector<double>* y) const {
        Q_UNUSED(x);
        Q_UNUSED(y);
    }

    // sim.truth's fields after tick and t (name, unit): the scene reads them.
    // Empty: sim.truth carries the plant's states.
    virtual QVector<QPair<QString, QString>> truthFields() const {
        return {};
    }
    virtual QVector<double> truthValues(const CodePlant& plant) const {
        Q_UNUSED(plant);
        return {};
    }

    // A UTF8 document topic (the track's drawing): its name (empty: none)
    // and its text, published on subscribe and after every reset.
    virtual QByteArray documentTopic() const {
        return {};
    }
    virtual QByteArray document() const {
        return {};
    }

    // Short status values for the plant's card ("covered" "63.2 %").
    virtual QVector<QPair<QString, QString>> statusItems() const {
        return {};
    }
};

// The environments the benchmarks offer, by id.
class PlantEnvironmentRegistry {
public:
    struct Entry {
        QString id;
        QString name;  // translated
        std::function<std::unique_ptr<PlantEnvironment>()> create;
    };

    static PlantEnvironmentRegistry& instance();
    // Ignored when the id is taken.
    void registerEnvironment(const Entry& entry);
    std::unique_ptr<PlantEnvironment> create(const QString& id) const;
    const QVector<Entry>& entries() const {
        return m_entries;
    }

private:
    QVector<Entry> m_entries;
};

}  // namespace tracelab
