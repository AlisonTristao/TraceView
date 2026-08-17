#pragma once

#include "dashboard/widgetconfigeditor.h"

class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace traceview {

// Settings for DummyGaugeWidget (see widgets/chartwidgets.h): which BTP
// source/topic (TELEMETRY.md section 8) this gauge reads from, the fixed
// min/max range and unit/decimals shared by every ring, and a table of
// rings -- one per concentric arc the gauge draws, each binding its own
// field and color (mirrors ChartConfigEditor's series table minus the
// per-series Style column, which a ring has no equivalent of). No history/
// axis settings (there's nothing to scroll), and no threshold-triggered
// actions -- this only ever reflects each ring's current value.
class GaugeConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit GaugeConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    // Appends one ring row built from `series` (missing fields fall back to
    // sensible defaults). Safe to call both while setConfig() is rebuilding
    // the whole table and from the "+ Add ring" button.
    void addSeriesRow(const QJsonObject& series);
    // Common tail of every field's change handler: no-ops while setConfig()
    // is programmatically repopulating the UI (m_updating), otherwise
    // re-derives config() and emits configChanged().
    void emitChanged();

    bool m_updating = false;

    QFormLayout* m_formLayout = nullptr;
    QLineEdit* m_sourceIdEdit = nullptr;
    QLineEdit* m_topicIdEdit = nullptr;
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QLineEdit* m_unitEdit = nullptr;
    QSpinBox* m_decimalsSpin = nullptr;
    QTableWidget* m_seriesTable = nullptr;
    QPushButton* m_addSeriesButton = nullptr;
};

}  // namespace traceview
