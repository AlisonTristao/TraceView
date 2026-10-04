#pragma once

#include <QPointer>
#include <QWidget>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;

namespace traceview {
class ModuleHost;
}

namespace tracelab::lf {
class LineControllerDevice;
class LinePlantDevice;
}  // namespace tracelab::lf

namespace tracelab {

class LabCard;

// The TraceLab tab's page: the tools to build a lab in the open project (add
// a simulated plant, a controller host bound to it and a Scene showing them;
// the benchmarks themselves are projects, under File > Open Example) and to
// drive its live TraceLab devices -- run, pause, reset,
// one step, the clock (1x, 2x, as fast as possible, lockstep), the track
// (seed and type, applied on reset), sensor noise, and the controller (qLPV
// MPC, GPC or PD, the wheel-speed filter and the main weights). Laid out as
// cards like the Devices tab's (Simulation, Track and sensors, Controller),
// side by side as far as the width allows.
class LabPanel : public QWidget {
    Q_OBJECT

public:
    // `host` adds the devices and widgets the tools build; it must outlive
    // the panel (null: the tools do nothing).
    explicit LabPanel(traceview::ModuleHost* host, QWidget* parent = nullptr);

    // The ribbon's buttons (parented to this panel), in two groups split by
    // a separator: Add plant, Add controller, Add scene | Play, Pause,
    // Reset, Step.
    QList<QAction*> ribbonActions() const;

    // The line-follower benchmark project (File > Open Example).
    static QString lineFollowerProjectPath();

public slots:
    // A simulated line-follower plant, with the default settings.
    void addLineFollowerPlant();
    // A controller host bound to the plant picked in the Run box (or the
    // only one); nothing without a plant.
    void addController();
    // A Scene bound by name to that plant and controller.
    void addScene();
    void play();
    void pauseSimulation();
    void resetSimulation();
    void stepSimulation();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyTheme();
    void alignCards();
    void layoutCards();
    void showControllerFields();
    lf::LinePlantDevice* plant() const;
    lf::LineControllerDevice* controller() const;
    void refreshDevices();
    void refreshFromDevices();
    void refreshStatus();
    void applySpeed();
    void applyRate();
    void applyTrackAndSensors();
    void applyController();

    QString currentPlantId() const;

    traceview::ModuleHost* m_host = nullptr;
    QAction* m_addPlantAction = nullptr;
    QAction* m_addControllerAction = nullptr;
    QAction* m_addSceneAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resetAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_groupSeparator = nullptr;

    QComboBox* m_plantCombo = nullptr;
    QComboBox* m_controllerCombo = nullptr;
    QComboBox* m_speedCombo = nullptr;
    QSpinBox* m_rateSpin = nullptr;
    QSpinBox* m_seedSpin = nullptr;
    QComboBox* m_trackCombo = nullptr;
    QCheckBox* m_noiseCheck = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QDoubleSpinBox* m_filterSpin = nullptr;
    QDoubleSpinBox* m_qLateralSpin = nullptr;
    QComboBox* m_schedulingCombo = nullptr;
    QComboBox* m_embeddingCombo = nullptr;
    QSpinBox* m_iterationsSpin = nullptr;
    QDoubleSpinBox* m_gpcLambdaSpin = nullptr;
    QDoubleSpinBox* m_pdKpSpin = nullptr;
    QDoubleSpinBox* m_pdKdSpin = nullptr;
    QDoubleSpinBox* m_pdBaseSpin = nullptr;
    QWidget* m_qlpvFields = nullptr;
    QWidget* m_gpcFields = nullptr;
    QWidget* m_pdFields = nullptr;
    LabCard* m_simulationCard = nullptr;
    LabCard* m_controllerCard = nullptr;
    QLabel* m_tickValue = nullptr;
    QLabel* m_timeValue = nullptr;
    QLabel* m_coveredValue = nullptr;
    QLabel* m_perturbationLabel = nullptr;
    QWidget* m_emptyState = nullptr;
    QScrollArea* m_cardsScroll = nullptr;
    QGridLayout* m_cardsGrid = nullptr;
    QList<LabCard*> m_cards;
    int m_columns = 0;
    bool m_updating = false;
};

}  // namespace tracelab
