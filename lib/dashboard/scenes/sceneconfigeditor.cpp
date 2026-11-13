#include "sceneconfigeditor.h"

#include <algorithm>

#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "dashboard/scenes/sceneregistry.h"
#include "theme/doublespinbox.h"

namespace traceview {

namespace {

using traceview::CatalogTopicField;
using traceview::CatalogTopicInfo;
using traceview::DeviceOption;
using traceview::DoubleSpinBox;

bool isTextTopic(const CatalogTopicInfo& topic) {
    return topic.encoding.compare(QLatin1String("UTF8"), Qt::CaseInsensitive) == 0;
}

bool isArrayField(const CatalogTopicField& field) {
    return field.type.contains(QLatin1Char('['));
}

bool fieldFits(const SceneInput& input, const CatalogTopicField& field) {
    return input.kind == SceneInput::Kind::Array ? isArrayField(field)
                                                 : traceview::isPlottableScalarField(field);
}

QJsonObject bindingFor(const DeviceOption& device, const CatalogTopicInfo& topic,
                       const CatalogTopicField* field) {
    QJsonObject json;
    json[QLatin1String("deviceId")] = device.id;
    json[QLatin1String("sourceId")] = traceview::formatHexId(topic.sourceId, 8);
    json[QLatin1String("topicId")] = traceview::formatHexId(topic.topicId, 4);
    json[QLatin1String("topicName")] = topic.name;
    if (field) {
        json[QLatin1String("fieldId")] = int(field->fieldId);
        json[QLatin1String("fieldName")] = field->name;
    }
    return json;
}

// Same binding: same device and, when both carry them, the same names (the
// names are the binding), else the same ids.
bool sameBinding(const QJsonObject& a, const QJsonObject& b) {
    const auto text = [](const QJsonObject& o, const char* key) {
        return o.value(QLatin1String(key)).toString();
    };
    if (text(a, "deviceId") != text(b, "deviceId")) {
        return false;
    }
    if (!text(a, "topicName").isEmpty() && !text(b, "topicName").isEmpty()) {
        return text(a, "topicName") == text(b, "topicName") &&
               text(a, "fieldName") == text(b, "fieldName");
    }
    return text(a, "sourceId").toULongLong(nullptr, 0) ==
               text(b, "sourceId").toULongLong(nullptr, 0) &&
           text(a, "topicId").toUInt(nullptr, 0) == text(b, "topicId").toUInt(nullptr, 0) &&
           a.value(QLatin1String("fieldId")).toInt(-1) ==
               b.value(QLatin1String("fieldId")).toInt(-1);
}

QString bindingLabel(const QJsonObject& binding) {
    const QString topic = binding.value(QLatin1String("topicName")).toString();
    const QString field = binding.value(QLatin1String("fieldName")).toString();
    const QString topicText =
        topic.isEmpty() ? binding.value(QLatin1String("topicId")).toString() : topic;
    if (!binding.contains(QLatin1String("fieldId"))) {
        return topicText;
    }
    return QStringLiteral("%1/%2").arg(
        topicText,
        field.isEmpty() ? QString::number(binding.value(QLatin1String("fieldId")).toInt()) : field);
}

}  // namespace

QJsonObject autoBindSceneInputs(const QList<SceneInput>& inputs, const QJsonObject& bind,
                                const QVector<DeviceOption>& devices) {
    QJsonObject out = bind;
    for (const SceneInput& input : inputs) {
        if (out.contains(input.name)) {
            continue;
        }
        QJsonObject preferred;
        QVector<QJsonObject> byInputName;
        for (const DeviceOption& device : devices) {
            for (const CatalogTopicInfo& topic : device.catalogTopics) {
                if (input.kind == SceneInput::Kind::Text) {
                    if (isTextTopic(topic) && topic.name == input.defaultTopic &&
                        preferred.isEmpty()) {
                        preferred = bindingFor(device, topic, nullptr);
                    }
                    continue;
                }
                for (const CatalogTopicField& field : topic.fields) {
                    if (!fieldFits(input, field)) {
                        continue;
                    }
                    if (topic.name == input.defaultTopic && field.name == input.defaultField &&
                        preferred.isEmpty()) {
                        preferred = bindingFor(device, topic, &field);
                    }
                    if (field.name == input.name) {
                        byInputName.append(bindingFor(device, topic, &field));
                    }
                }
            }
        }
        if (!preferred.isEmpty()) {
            out[input.name] = preferred;
        } else if (!byInputName.isEmpty()) {
            // Several topics report the name: keep the inputs together on a
            // topic one of them already reads, else take the first reported.
            QJsonObject chosen = byInputName.first();
            for (const QJsonObject& candidate : byInputName) {
                const auto sameTopic = [&candidate](const QJsonValue& bound) {
                    const QJsonObject b = bound.toObject();
                    return b.value(QLatin1String("deviceId")) ==
                               candidate.value(QLatin1String("deviceId")) &&
                           b.value(QLatin1String("topicName")) ==
                               candidate.value(QLatin1String("topicName"));
                };
                if (std::any_of(out.begin(), out.end(), sameTopic)) {
                    chosen = candidate;
                    break;
                }
            }
            out[input.name] = chosen;
        }
    }
    return out;
}

SceneConfigEditor::SceneConfigEditor(QWidget* parent) : traceview::WidgetConfigEditor(parent) {
    m_sceneCombo = new QComboBox(this);
    for (const SceneTypeInfo& type : SceneRegistry::instance().types()) {
        m_sceneCombo->addItem(type.displayName, type.typeId);
    }

    m_bindByNameButton = new QPushButton(tr("Bind by name"), this);
    m_bindByNameButton->setToolTip(
        tr("Binds every unbound input to the reported field of the same name"));

    auto* top = new QFormLayout;
    top->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    top->addRow(tr("Scene"), m_sceneCombo);

    auto* inputsTitle = new QLabel(tr("Inputs"), this);
    QFont titleFont = inputsTitle->font();
    titleFont.setBold(true);
    inputsTitle->setFont(titleFont);
    m_inputsLayout = new QFormLayout;
    m_inputsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* paramsTitle = new QLabel(tr("Parameters"), this);
    paramsTitle->setFont(titleFont);
    m_paramsLayout = new QFormLayout;
    m_paramsLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(top);
    layout->addWidget(inputsTitle);
    layout->addWidget(m_bindByNameButton);
    layout->addLayout(m_inputsLayout);
    layout->addWidget(paramsTitle);
    layout->addLayout(m_paramsLayout);

    connect(m_sceneCombo, &QComboBox::activated, this, [this](int index) {
        const QString type = m_sceneCombo->itemData(index).toString();
        if (type == m_sceneType) {
            return;
        }
        // Another scene: its own inputs, bound by name, and its defaults.
        m_sceneType = type;
        const std::unique_ptr<SceneRenderer> scene = SceneRegistry::instance().create(type);
        m_bind = scene ? autoBindSceneInputs(scene->inputs(), {}, m_devices) : QJsonObject();
        m_paramValues.clear();
        rebuildRows();
        emitChanged();
    });
    connect(m_bindByNameButton, &QPushButton::clicked, this, [this] {
        const QJsonObject bound = autoBindSceneInputs(m_inputs, m_bind, m_devices);
        if (bound != m_bind) {
            m_bind = bound;
            rebuildRows();
            emitChanged();
        }
    });

    setConfig({});
}

void SceneConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_sceneType = config.value(QLatin1String("scene")).toString();
    if (m_sceneType.isEmpty() && m_sceneCombo->count() > 0) {
        m_sceneType = m_sceneCombo->itemData(0).toString();
    }
    m_bind = config.value(QLatin1String("bind")).toObject();
    m_paramValues.clear();
    const QJsonObject params = config.value(QLatin1String("params")).toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        m_paramValues.insert(it.key(), it.value().toDouble());
    }
    rebuildRows();
    m_updating = false;
}

QJsonObject SceneConfigEditor::config() const {
    QJsonObject params;
    for (const SceneParam& param : m_params) {
        params[param.name] = m_paramValues.value(param.name, param.defaultValue);
    }
    QJsonObject config;
    config[QLatin1String("scene")] = m_sceneType;
    config[QLatin1String("bind")] = m_bind;
    config[QLatin1String("params")] = params;
    return config;
}

void SceneConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_devices = devices;
    for (const SceneInput& input : std::as_const(m_inputs)) {
        if (QComboBox* combo = m_bindingCombos.value(input.name)) {
            fillBindingCombo(combo, input);
        }
    }
    m_updating = wasUpdating;
}

void SceneConfigEditor::rebuildRows() {
    const bool wasUpdating = m_updating;
    m_updating = true;
    const int sceneIndex = m_sceneCombo->findData(m_sceneType);
    if (sceneIndex >= 0) {
        m_sceneCombo->setCurrentIndex(sceneIndex);
    }
    const std::unique_ptr<SceneRenderer> scene = SceneRegistry::instance().create(m_sceneType);
    m_inputs = scene ? scene->inputs() : QList<SceneInput>();
    m_params = scene ? scene->params() : QList<SceneParam>();

    // The old rows go later, not now: this runs inside a field's own signal
    // too (a check box toggled -> configChanged -> the properties panel's
    // setConfig()), and deleting the sender there crashes once it returns.
    for (QFormLayout* layout : {m_inputsLayout, m_paramsLayout}) {
        while (layout->rowCount() > 0) {
            const QFormLayout::TakeRowResult row = layout->takeRow(0);
            for (QLayoutItem* item : {row.labelItem, row.fieldItem}) {
                if (!item) {
                    continue;
                }
                if (QWidget* widget = item->widget()) {
                    widget->hide();
                    widget->deleteLater();
                }
                delete item;
            }
        }
    }
    m_bindingCombos.clear();
    m_paramSpins.clear();

    for (const SceneInput& input : std::as_const(m_inputs)) {
        auto* combo = new QComboBox(this);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
        fillBindingCombo(combo, input);
        const QString name = input.name;
        connect(combo, &QComboBox::activated, this, [this, combo, name](int index) {
            const QJsonObject binding = combo->itemData(index).toJsonObject();
            if (binding.isEmpty()) {
                m_bind.remove(name);
            } else {
                m_bind[name] = binding;
            }
            emitChanged();
        });
        m_bindingCombos.insert(input.name, combo);
        const QString label = input.unit.isEmpty()
                                  ? input.name
                                  : QStringLiteral("%1 (%2)").arg(input.name, input.unit);
        m_inputsLayout->addRow(label, combo);
    }

    for (const SceneParam& param : std::as_const(m_params)) {
        if (param.toggle) {
            auto* check = new QCheckBox(this);
            check->setChecked(m_paramValues.value(param.name, param.defaultValue) != 0.0);
            const QString name = param.name;
            connect(check, &QCheckBox::toggled, this, [this, name](bool on) {
                if (m_updating) {
                    return;
                }
                m_paramValues[name] = on ? 1.0 : 0.0;
                emitChanged();
            });
            m_paramsLayout->addRow(param.label, check);
            continue;
        }
        auto* spin = new DoubleSpinBox(this);
        spin->setRange(param.minimum, param.maximum);
        spin->setDecimals(param.decimals);
        if (!param.unit.isEmpty()) {
            spin->setSuffix(QStringLiteral(" %1").arg(param.unit));
        }
        spin->setValue(m_paramValues.value(param.name, param.defaultValue));
        const QString name = param.name;
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, name](double value) {
            if (m_updating) {
                return;
            }
            m_paramValues[name] = value;
            emitChanged();
        });
        m_paramSpins.insert(param.name, spin);
        m_paramsLayout->addRow(param.label, spin);
    }
    m_updating = wasUpdating;
}

void SceneConfigEditor::fillBindingCombo(QComboBox* combo, const SceneInput& input) const {
    const QJsonObject current = m_bind.value(input.name).toObject();
    combo->clear();
    combo->addItem(tr("(Unbound)"), QJsonObject());
    int selected = 0;
    for (const DeviceOption& device : m_devices) {
        for (const CatalogTopicInfo& topic : device.catalogTopics) {
            QVector<QJsonObject> choices;
            if (input.kind == SceneInput::Kind::Text) {
                if (isTextTopic(topic)) {
                    choices.append(bindingFor(device, topic, nullptr));
                }
            } else {
                for (const CatalogTopicField& field : topic.fields) {
                    if (fieldFits(input, field)) {
                        choices.append(bindingFor(device, topic, &field));
                    }
                }
            }
            for (const QJsonObject& choice : choices) {
                combo->addItem(QStringLiteral("%1: %2").arg(device.name, bindingLabel(choice)),
                               choice);
                if (!current.isEmpty() && selected == 0 && sameBinding(choice, current)) {
                    selected = combo->count() - 1;
                }
            }
        }
    }
    if (!current.isEmpty() && selected == 0) {
        // Bound to something no device reports right now: keep it, as is.
        combo->addItem(tr("%1 (offline)").arg(bindingLabel(current)), current);
        selected = combo->count() - 1;
    }
    combo->setCurrentIndex(selected);
}

void SceneConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

}  // namespace traceview
