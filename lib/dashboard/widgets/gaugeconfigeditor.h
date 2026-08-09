#pragma once

#include "dashboard/widgetconfigeditor.h"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace traceview {

// Settings for DummyGaugeWidget (see widgets/chartwidgets.h): unlike the
// line/bar charts, a gauge only ever displays one live value, so its config
// is the single-series subset of ChartConfigEditor's shape — how the
// incoming frame is read (delimited text or raw bytes, which slot of it)
// and the fixed min/max range plus unit/decimals used to scale and label
// what's read. No history/axis settings (there's nothing to scroll), and no
// threshold-triggered actions — this only ever reflects the current value,
// same as the chart types it sits alongside. This only captures the shape;
// nothing parses incoming frames or feeds the gauge's paint yet — that's
// later work, tracked against this config's JSON shape (see config()).
class GaugeConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit GaugeConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    // The byte-type field only makes sense when Format is "Bytes".
    void updateByteTypeVisibility();
    void emitChanged();

    bool m_updating = false;

    QFormLayout* m_formLayout = nullptr;
    QComboBox* m_formatCombo = nullptr;
    QSpinBox* m_indexSpin = nullptr;
    QComboBox* m_byteTypeCombo = nullptr;
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QLineEdit* m_unitEdit = nullptr;
    QSpinBox* m_decimalsSpin = nullptr;
};

} // namespace traceview
