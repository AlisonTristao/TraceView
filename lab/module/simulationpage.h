#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

#include "lab/plant/labplantdevice.h"

class QAction;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QSpinBox;

namespace tracelab {

class LabStatusLine;

// The Simulation page of the TraceLab tab: the run -- whether it runs,
// where it stands (tick, time and the plant's own figures) and the buttons
// that drive it (the ribbon's: Play, Pause, Step, Reset) -- and the clock
// (speed, sample rate), for any kind of plant (LabPlantDevice).
class SimulationPage : public QWidget {
    Q_OBJECT

public:
    // `runActions`: the ribbon's Play, Pause, Step and Reset, shown as
    // buttons here too.
    explicit SimulationPage(const QList<QAction*>& runActions, QWidget* parent = nullptr);

    void setPlant(LabPlantDevice* plant);
    LabPlantDevice* plant() const {
        return m_plant;
    }
    // The status and the figures (the panel calls it a few times a second).
    void refreshLive();

private:
    void refreshClock();
    void applySpeed();
    void applyRate();
    void addStat(const QString& caption);

    QPointer<LabPlantDevice> m_plant;
    bool m_updating = false;
    LabStatusLine* m_status = nullptr;
    QHBoxLayout* m_stats = nullptr;
    QVector<QLabel*> m_statValues;
    QVector<QLabel*> m_statCaptions;
    QComboBox* m_speedCombo = nullptr;
    QSpinBox* m_rateSpin = nullptr;
};

}  // namespace tracelab
