#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>
#include <optional>

class QPainter;

namespace traceview {

// One value a scene reads, bound by the Scene widget's editor to a topic
// field (or a whole text topic) of some device, by name. `defaultTopic` and
// `defaultField` are what "Bind by name" looks for first: a plant that
// publishes sim.truth/x binds the scene's "x" with no clicking. Input names
// are identifiers, never translated, and are the keys of the .tvproj "bind".
struct SceneInput {
    enum class Kind {
        Scalar,  // one number per sample (Backend::fieldSample)
        Array,   // a whole array field (Backend::arraySample)
        Text,    // a whole UTF8 topic, a document (Backend::textSample)
    };

    QString name;
    QString unit;
    Kind kind = Kind::Scalar;
    QString defaultTopic;
    QString defaultField;  // empty for Text
    bool optional = false;
};

// One numeric setting of a scene (lengths, counts, scale), edited in the
// Scene widget's properties and saved in the .tvproj "params". `name` is the
// stable key; `label` is translated.
struct SceneParam {
    QString name;
    QString label;
    QString unit;
    double defaultValue = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    int decimals = 2;
    // An on/off option (0 or 1): a check box in the editor instead of a spin
    // box; minimum, maximum and decimals are ignored.
    bool toggle = false;
};

// Everything that arrived from one telemetry sample, for the inputs bound to
// its topic: the values of the Scalar inputs and the arrays of the Array
// inputs, all with the sample's own timestamp. Records keep values of one
// tick together (sim.truth's tick, x, y and theta) so a scene can index a
// history by tick.
struct SceneRecord {
    quint64 timestampUs = 0;
    QHash<QString, double> values;
    QHash<QString, QVector<double>> arrays;
};

// What update() gets once per rendered frame.
struct SceneFrame {
    // Every record received since the previous frame, oldest first.
    QVector<SceneRecord> records;
    // The newest value of every Scalar/Array input received so far (also the
    // ones that did not change this frame).
    QHash<QString, double> values;
    QHash<QString, QVector<double>> arrays;
};

// The camera the widget owns: the pixel size of the drawing area, and the
// user's zoom (mouse wheel, 1 = the scene's own scale) and pan (drag, in
// pixels). Each scene maps its world into this with its own projection.
struct SceneView {
    QSizeF size;
    double zoom = 1.0;
    QPointF pan;
    // Frames the widget painted over the last second.
    double renderFps = 0.0;
};

// An action a key asks for, sent to the bound plant (sim.disturb...).
// `argument` is the action's single numeric argument.
struct SceneAction {
    QString action;
    double argument = 0.0;
};

// One animated benchmark (line follower, Furuta pendulum...), drawn by the
// Scene widget at the dashboard's render rate. One class per benchmark,
// registered in SceneRegistry; the widget, its editor and the project format
// never change for a new one. No QObject: a scene is plain C++ driven by its
// widget, translated with QCoreApplication::translate("<ClassName>", ...).
class SceneRenderer {
public:
    virtual ~SceneRenderer() = default;

    virtual QList<SceneInput> inputs() const = 0;
    virtual QList<SceneParam> params() const = 0;
    // `value` is a number for every SceneParam. Unknown names are ignored.
    virtual void setParam(const QString& name, const QVariant& value) = 0;

    // Once per frame, before paint(), with what arrived since the last one.
    virtual void update(const SceneFrame& frame) = 0;
    // Draws the whole widget area (background included) for `view`.
    virtual void paint(QPainter& painter, const SceneView& view) = 0;

    // Whether paint() antialiases. When it does not, SceneWidget skips its
    // antialiased rounded clip (the cell's mask already cuts the corners):
    // an aliased drawing under a path clip takes Qt's slow path and costs
    // more than antialiasing saves.
    virtual bool antialiased() const {
        return true;
    }

    // A Text input received a document (the line follower's track). `input`
    // is the input's name.
    virtual void document(const QString& input, const QByteArray& body) {
        Q_UNUSED(input);
        Q_UNUSED(body);
    }

    // The action a key (Qt::Key) asks for while the scene has focus, if any.
    virtual std::optional<SceneAction> keyAction(int key) {
        Q_UNUSED(key);
        return std::nullopt;
    }
};

}  // namespace traceview
