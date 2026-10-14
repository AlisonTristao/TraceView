#pragma once

#include <QJsonObject>
#include <QVector>

#include "dashboard/widgetconfigeditor.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace traceview {

// Settings for MultiSourceLineChartWidget ("multi_line"): per series, the
// device, topic and field it plots (each series its own -- the point of the
// widget) with its name, line style and color; then the axes, as a line
// chart's (how many samples the X axis keeps and at what period, the Y
// axis's range, unit and decimals, or one automatic axis per unit). Topics
// and fields are picked from each device's catalog (DeviceOption::
// catalogTopics); a binding the catalog does not know (the device offline)
// keeps showing what it was bound to by name.
class MultiSourceChartConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit MultiSourceChartConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    struct Row {
        QWidget* box = nullptr;
        QComboBox* device = nullptr;
        QComboBox* topic = nullptr;
        QComboBox* field = nullptr;
        QLineEdit* name = nullptr;
        QComboBox* style = nullptr;
        QPushButton* color = nullptr;
        QString colorName;
        QString unit;
        // What it is bound to, kept while the catalog does not know it.
        QJsonObject binding;
    };

    void addRow(const QJsonObject& series, const QJsonObject& binding);
    void removeRow(QWidget* box);
    void fillTopics(Row& row);
    void fillFields(Row& row);
    void setColor(Row& row, const QString& color);
    void updateAxisRows();
    void emitChanged();

    QVector<DeviceOption> m_devices;
    QVector<Row> m_rows;
    bool m_updating = false;
    QJsonObject m_base;  // what this editor does not edit ("view"...), kept

    QVBoxLayout* m_rowsLayout = nullptr;
    QSpinBox* m_xLimit = nullptr;
    QDoubleSpinBox* m_sampleTime = nullptr;
    QComboBox* m_yMode = nullptr;
    QDoubleSpinBox* m_yMin = nullptr;
    QDoubleSpinBox* m_yMax = nullptr;
    QLineEdit* m_yUnit = nullptr;
    QSpinBox* m_decimals = nullptr;
    QCheckBox* m_autoAxis = nullptr;
};

}  // namespace traceview
