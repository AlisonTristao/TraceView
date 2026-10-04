#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

#include "lab/plant/labplantdevice.h"

class QComboBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSpinBox;

namespace tracelab {


// The page of the plant in the TraceLab tab, for any kind of plant
// (LabPlantDevice): where the run stands (tick, time and the plant's own
// status values), the clock (speed, sample rate) and the plant's settings as
// it describes them, applied together on a reset.
class PlantPage : public QWidget {
    Q_OBJECT

public:
    explicit PlantPage(QWidget* parent = nullptr);

    void setPlant(LabPlantDevice* plant);
    LabPlantDevice* plant() const {
        return m_plant;
    }
    // The status values (the panel calls it a few times a second).
    void refreshLive();

private:
    void rebuildSettings();
    void refreshClock();
    void applySpeed();
    void applyRate();
    void applySettings();
    void addStat(const QString& caption);

    QPointer<LabPlantDevice> m_plant;
    bool m_updating = false;
    QLabel* m_title = nullptr;
    QHBoxLayout* m_stats = nullptr;
    QVector<QLabel*> m_statValues;
    QVector<QLabel*> m_statCaptions;
    QComboBox* m_speedCombo = nullptr;
    QSpinBox* m_rateSpin = nullptr;
    QWidget* m_settingsBox = nullptr;
    QFormLayout* m_settingsForm = nullptr;
    QPushButton* m_applyButton = nullptr;
    QVariantMap m_pending;
};

}  // namespace tracelab
