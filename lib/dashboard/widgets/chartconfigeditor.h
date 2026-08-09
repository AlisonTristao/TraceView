#pragma once

#include "dashboard/widgetconfigeditor.h"

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
// widgetregistry.cpp): how an incoming data frame is shaped — delimited
// text ("1;2;3;4;5") or a raw byte array — and, per plotted series, which
// slot of that frame it reads plus its display name/color/style, and (for
// byte frames) which primitive type it's packed as. Also covers axis
// display: what the X axis counts (sample index, or elapsed time derived
// from a fixed per-sample period — never wall-clock arrival time), how much
// history it keeps before older data scrolls off (in points or seconds,
// matching whichever unit it's currently counting), and how the Y axis is
// scaled/labeled.
//
// This only edits and stores that shape; it does not parse incoming frames
// or feed anything to chart rendering yet — both are future work, tracked
// against this config's JSON shape (see config()).
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
    // The byte-type column only makes sense when Format is "Bytes".
    void updateByteTypeColumnVisibility();
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
    QComboBox* m_formatCombo = nullptr;
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
    QTableWidget* m_seriesTable = nullptr;
    QPushButton* m_addSeriesButton = nullptr;
};

} // namespace traceview
