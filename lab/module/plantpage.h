#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

#include "lab/plant/labplantdevice.h"

class QFormLayout;
class QLabel;
class QPushButton;

namespace tracelab {

class LabSection;
class LabStatusLine;

// The Plant page of the TraceLab tab, for any kind of plant
// (LabPlantDevice): what the plant is -- its kind, how it stands, where its
// code is -- and the settings of its world as the plant describes them
// (applied together on a reset). Its code has a page of its own (Code); the
// clock and the run, another (SimulationPage).
class PlantPage : public QWidget {
    Q_OBJECT

public:
    explicit PlantPage(QWidget* parent = nullptr);

    void setPlant(LabPlantDevice* plant);
    LabPlantDevice* plant() const {
        return m_plant;
    }
    // How it stands (the panel calls it a few times a second).
    void refreshLive();

signals:
    // "Go to the code": the plant's block on the Code page.
    void codeRequested();

private:
    void rebuildSettings();
    void applySettings();

    QPointer<LabPlantDevice> m_plant;
    bool m_updating = false;
    QLabel* m_description = nullptr;
    QLabel* m_kind = nullptr;
    QLabel* m_device = nullptr;
    LabStatusLine* m_status = nullptr;
    QPushButton* m_codeButton = nullptr;
    LabSection* m_settingsSection = nullptr;
    QWidget* m_settingsBox = nullptr;
    QFormLayout* m_settingsForm = nullptr;
    QPushButton* m_applyButton = nullptr;
    QVariantMap m_pending;
};

}  // namespace tracelab
