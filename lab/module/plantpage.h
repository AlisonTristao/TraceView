#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

#include "lab/plant/labplantdevice.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;

namespace tracelab {

class BlockPage;
class LabSection;

// The page of the plant in the TraceLab tab, for any kind of plant
// (LabPlantDevice). The header: the plant, whether it runs, where the run
// stands (tick, time and the plant's own figures) and, for a plant written
// in C, "Open the code". Below, a column with the clock (speed, sample
// rate) and the settings as the plant describes them (applied together on
// a reset), then -- for a plant in C -- the columns of its code (BlockPage).
class PlantPage : public QWidget {
    Q_OBJECT

public:
    explicit PlantPage(QWidget* parent = nullptr);

    void setPlant(LabPlantDevice* plant);
    LabPlantDevice* plant() const {
        return m_plant;
    }
    // The figures, status and code columns (the panel calls it a few times
    // a second).
    void refreshLive();
    // The plant's code columns (no block for a plant built into TraceView).
    BlockPage* codePage() const {
        return m_page;
    }

private:
    void rebuildSettings();
    void refreshClock();
    void refreshHeader();
    void applySpeed();
    void applyRate();
    void applySettings();
    void addStat(const QString& caption);

    QPointer<LabPlantDevice> m_plant;
    bool m_updating = false;
    BlockPage* m_page = nullptr;
    QWidget* m_stats = nullptr;
    QVector<QLabel*> m_statValues;
    QVector<QLabel*> m_statCaptions;
    QComboBox* m_speedCombo = nullptr;
    QSpinBox* m_rateSpin = nullptr;
    LabSection* m_settingsSection = nullptr;
    QWidget* m_settingsBox = nullptr;
    QFormLayout* m_settingsForm = nullptr;
    QPushButton* m_applyButton = nullptr;
    QVariantMap m_pending;
};

}  // namespace tracelab
