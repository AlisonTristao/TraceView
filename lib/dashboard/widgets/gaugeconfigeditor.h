#pragma once

#include "dashboard/widgetconfigeditor.h"

class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace traceview {

// Settings for DummyGaugeWidget (see widgets/chartwidgets.h): unlike the
// line/bar charts, a gauge only ever displays one live value, so its config
// is the single-field subset of ChartConfigEditor's shape — which BTP
// source/topic/field (TELEMETRY.md section 8) it reads, plus the fixed
// min/max range and unit/decimals used to scale and label it. No history/
// axis settings (there's nothing to scroll), and no threshold-triggered
// actions — this only ever reflects the current value.
class GaugeConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit GaugeConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    void emitChanged();

    bool m_updating = false;

    QFormLayout* m_formLayout = nullptr;
    QLineEdit* m_sourceIdEdit = nullptr;
    QLineEdit* m_topicIdEdit = nullptr;
    QSpinBox* m_fieldIdSpin = nullptr;
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QLineEdit* m_unitEdit = nullptr;
    QSpinBox* m_decimalsSpin = nullptr;
};

}  // namespace traceview
