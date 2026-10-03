#pragma once

#include <QPointer>
#include <QWidget>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace traceview {
class ModuleHost;
}

namespace tracelab::lf {
class LineControllerDevice;
class LinePlantDevice;
}  // namespace tracelab::lf

namespace tracelab {

// The TraceLab tab's page: the tools to build a lab in the open project (add
// a simulated plant, a controller host bound to it and a Scene showing them;
// the benchmarks themselves are projects, under File > Open Example) and to
// drive its live TraceLab devices -- run, pause, reset,
// one step, the clock (1x, 2x, as fast as possible, lockstep), the track
// (seed and type, applied on reset), sensor noise, and the controller (qLPV
// MPC or GPC, the wheel-speed filter and the main weights).
class LabPanel : public QWidget {
    Q_OBJECT

public:
    // `host` adds the devices and widgets the tools build; it must outlive
    // the panel (null: the tools do nothing).
    explicit LabPanel(traceview::ModuleHost* host, QWidget* parent = nullptr);

    // The ribbon's buttons (parented to this panel): Add plant, Add
    // controller, Add scene, Play, Pause, Reset, Step.
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

private:
    lf::LinePlantDevice* plant() const;
    lf::LineControllerDevice* controller() const;
    void refreshDevices();
    void refreshFromDevices();
    void refreshStatus();
    void applySpeed();
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

    QComboBox* m_plantCombo = nullptr;
    QComboBox* m_controllerCombo = nullptr;
    QComboBox* m_speedCombo = nullptr;
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
    QLabel* m_statusLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QWidget* m_controls = nullptr;
    bool m_updating = false;
};

}  // namespace tracelab
