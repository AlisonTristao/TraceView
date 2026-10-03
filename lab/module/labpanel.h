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

namespace tracelab::lf {
class LineControllerDevice;
class LinePlantDevice;
}  // namespace tracelab::lf

namespace tracelab {

// The TraceLab tab's page: start a simulation from the preset project and
// drive the live TraceLab devices of the open project -- run, pause, reset,
// one step, the clock (1x, 2x, as fast as possible, lockstep), the track
// (seed and type, applied on reset), sensor noise, and the controller (qLPV
// MPC or GPC, the wheel-speed filter and the main weights).
class LabPanel : public QWidget {
    Q_OBJECT

public:
    // `openProject` opens a project file (ModuleHost::openProject()).
    explicit LabPanel(std::function<bool(const QString&)> openProject,
                      QWidget* parent = nullptr);

    // The ribbon's buttons (parented to this panel): New simulation, Play,
    // Pause, Reset, Step.
    QList<QAction*> ribbonActions() const;

    // The preset project "New simulation > Line follower" opens.
    static QString lineFollowerPresetPath();

public slots:
    void newLineFollowerSimulation();
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

    std::function<bool(const QString&)> m_openProject;
    QAction* m_newAction = nullptr;
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
