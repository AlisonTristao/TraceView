#pragma once

#include "dashboard/widgetconfigeditor.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace traceview {

// Settings for the chart widgets (dummy_line/dummy_bar, see
// widgetregistry.cpp): which BTP source/topic this chart reads from, and
// per plotted series, which field of that topic it binds to
// (TELEMETRY.md section 8: source_id + topic_id + field_id) plus its
// display name/color/style. Also covers axis display: what the X axis
// counts (sample index, or elapsed time derived from a fixed per-sample
// period -- never wall-clock arrival time), how much history it keeps
// before older data scrolls off (in points or seconds, matching whichever
// unit it's currently counting), and how the Y axis is scaled/labeled.
//
// Since topico 14, there is no more per-series "Format"/"Type" choice --
// encoding and field type are schema properties (see
// protocol/telemetrycatalog.h), not something a chart can redeclare, so
// that config duplication was removed rather than kept as a second source
// of truth.
class ChartConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit ChartConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    // Appends one series row built from `series` (missing fields fall back
    // to sensible defaults). Safe to call both while setConfig() is
    // rebuilding the whole table and from the "+ Add series" button.
    void addSeriesRow(const QJsonObject& series);
    // Sample Time only makes sense when X Axis is "Time"; Y Min/Max only
    // when Y Axis is "Fixed". Also relabels the X axis Limit field's unit
    // ("pts"/"s") to match the current X Axis mode — it stays visible in
    // both modes, only what it counts changes.
    void updateAxisRowsVisibility();
    // Common tail of every field's change handler: no-ops while setConfig()
    // is programmatically repopulating the UI (m_updating), otherwise
    // re-derives config() and emits configChanged().
    void emitChanged();

    bool m_updating = false;

    QFormLayout* m_formLayout = nullptr;
    QLineEdit* m_sourceIdEdit = nullptr;
    QLineEdit* m_topicIdEdit = nullptr;
    QSpinBox* m_countSpin = nullptr;
    QComboBox* m_xAxisModeCombo = nullptr;
    QLabel* m_sampleTimeLabel = nullptr;
    QDoubleSpinBox* m_sampleTimeSpin = nullptr;
    QSpinBox* m_xLimitSpin = nullptr;
    QComboBox* m_yAxisModeCombo = nullptr;
    QHBoxLayout* m_yRangeRow = nullptr;
    QDoubleSpinBox* m_yMinSpin = nullptr;
    QDoubleSpinBox* m_yMaxSpin = nullptr;
    QLineEdit* m_yUnitEdit = nullptr;
    QCheckBox* m_gridCheck = nullptr;
    QTableWidget* m_seriesTable = nullptr;
    QPushButton* m_addSeriesButton = nullptr;
};

}  // namespace traceview
