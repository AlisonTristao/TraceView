#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>

#include "dashboard/widgetconfigeditor.h"
#include "dashboard/scenes/scenerenderer.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QPushButton;

namespace traceview {

// `bind` (a Scene widget's "bind", see scenewidget.h) with every input that
// has no entry yet bound by name against the catalogs of `devices`: first the
// input's defaultTopic/defaultField, else a field named like the input
// itself in any topic -- when several topics report one, the topic another
// input is already bound to (so theta1 and theta2 come from the same topic),
// else the first reported. The kind has to match:
// a Scalar input takes a numeric field, an Array input an array field, a Text
// input a UTF8 topic. Inputs already bound are left as they are.
QJsonObject autoBindSceneInputs(const QList<SceneInput>& inputs, const QJsonObject& bind,
                                const QVector<traceview::DeviceOption>& devices);

// The Scene widget's properties: scene type, one binding picker per input
// (every reported field of the right kind, on every device) with "Bind by
// name", and the scene's parameters.
class SceneConfigEditor : public traceview::WidgetConfigEditor {
    Q_OBJECT

public:
    explicit SceneConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<traceview::DeviceOption>& devices) override;

private:
    void rebuildRows();
    void fillBindingCombo(QComboBox* combo, const SceneInput& input) const;
    void emitChanged();

    bool m_updating = false;
    QVector<traceview::DeviceOption> m_devices;
    QString m_sceneType;
    QList<SceneInput> m_inputs;
    QList<SceneParam> m_params;
    QJsonObject m_bind;
    QHash<QString, double> m_paramValues;

    QComboBox* m_sceneCombo = nullptr;
    QPushButton* m_bindByNameButton = nullptr;
    QFormLayout* m_inputsLayout = nullptr;
    QFormLayout* m_paramsLayout = nullptr;
    QHash<QString, QComboBox*> m_bindingCombos;
    QHash<QString, QDoubleSpinBox*> m_paramSpins;
};

}  // namespace traceview
