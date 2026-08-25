#pragma once

#include "dashboard/widgetconfigeditor.h"

class QComboBox;
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
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    // Appends one ring row built from `series` (missing fields fall back to
    // sensible defaults). Safe to call both while setConfig() is rebuilding
    // the whole table and from the "+ Add ring" button.
    void addSeriesRow(const QJsonObject& series);
    // Common tail of every field's change handler: no-ops while setConfig()
    // is programmatically repopulating the UI (m_updating), otherwise
    // re-derives config() and emits configChanged().
    void emitChanged();
    // Re-derives what the Source/Topic fields should display from
    // m_sourceId/m_topicId (the actual bound identity) against m_devices --
    // a resolved catalog name/owning device name where one is known, the raw
    // hex otherwise. Called on every change to Device/Source/Topic and from
    // setAvailableDevices(), since a catalog can arrive after the binding was
    // already set. Display-only: never touches m_sourceId/m_topicId
    // themselves.
    void updateIdentityDisplay();
    // Repopulates every ring row's Field ID combo from the currently bound
    // topic's catalog fields (see resolveCatalogTopicFields()) -- called
    // whenever that topic could have changed: from updateIdentityDisplay()
    // and from addSeriesRow() for a freshly inserted row.
    void refreshSeriesFieldOptions();

    bool m_updating = false;

    QVector<DeviceOption> m_devices;

    // The actual bound identity -- what config()/persistence read, and the
    // only thing updateIdentityDisplay() treats as ground truth. Set from
    // picking a catalog entry (m_topicIdEdit's activated handler) or typing a
    // numeric id by hand (its lineEdit's editingFinished); everything the
    // fields below *show* is derived from these, never the other way around.
    quint32 m_sourceId = 0;
    quint16 m_topicId = 0;

    QFormLayout* m_formLayout = nullptr;
    QComboBox* m_deviceCombo = nullptr;
    // Read-only: shows the name of the device that owns m_sourceId (see
    // resolveSourceLabel(), widgetconfigeditor.h), or its raw hex if no
    // configured device claims that identity. Never user-typed -- Source is
    // now always derived from the Topic field/Device picker, not entered
    // directly (see docs/DEVICES.md's Hub section: each robot is its own
    // Device now, so Source rarely needs to differ from "whichever device
    // owns the bound topic").
    QLineEdit* m_sourceIdEdit = nullptr;
    // Editable: offers the selected device's reported catalog topics as
    // pickable entries (see populateTopicCombo()), showing each one's
    // readable name once picked (or once the catalog resolves an id typed by
    // hand) instead of raw hex -- but still accepts a hand-typed hex/decimal
    // topicId for one the device hasn't reported yet.
    QComboBox* m_topicIdEdit = nullptr;
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QLineEdit* m_unitEdit = nullptr;
    QSpinBox* m_decimalsSpin = nullptr;
    QTableWidget* m_seriesTable = nullptr;
    QPushButton* m_addSeriesButton = nullptr;
};

}  // namespace traceview
