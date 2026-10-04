#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

class QAction;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;

namespace traceview {
class ModuleHost;
}

namespace tracelab {

class BlockPage;
class LabCard;
class LabControllerDevice;
class LabPlantDevice;
class PlantPage;

// The TraceLab tab's page. The loop of a plant drawn as a chain of cards --
// the plant, the filters, the controller -- with a "+" where a block can be
// added; picking a card shows its page below: the plant's clock and settings
// (PlantPage), or a code block's page (BlockPage; its code opens in VS Code
// or a window of its own, CodeWindow). It knows plants and
// controller devices only through LabPlantDevice and LabControllerDevice
// (lab/control/labdevices.h): what kinds of plant exist is whatever the
// benchmarks registered. Every change a device makes to its configuration
// is stored in the project (ModuleHost::updateModuleDeviceConfig()).
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
    // A plant of the first registered kind, with its default settings.
    void addPlant();
    // A controller device on the current plant, with the first controller
    // template; picks it. Nothing without a plant.
    void addController();
    // A Scene bound by name to the current plant and controller.
    void addScene();
    void play();
    void pauseSimulation();
    void resetSimulation();
    void stepSimulation();

private:
    enum class Pick { Plant, Filter, Controller, Other };

    void applyTheme();
    void refreshDevices();
    void rebuildChain();
    void refreshCards();
    void refreshLive();
    void pick(Pick what, int index);
    void persistDevices();
    LabPlantDevice* plant() const;
    LabControllerDevice* controller() const;
    QString plantId() const;
    QMenu* blockMenu(bool controllerBlock);
    void addBlock(bool controllerBlock, const QString& templateId, const QString& path);
    void addControllerDevice(const QJsonObject& config, int pickAfter);

    traceview::ModuleHost* m_host = nullptr;
    QAction* m_addPlantAction = nullptr;
    QAction* m_addControllerAction = nullptr;
    QAction* m_addSceneAction = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_pauseAction = nullptr;
    QAction* m_resetAction = nullptr;
    QAction* m_stepAction = nullptr;
    QAction* m_groupSeparator = nullptr;

    QWidget* m_emptyState = nullptr;
    QWidget* m_main = nullptr;
    QComboBox* m_plantCombo = nullptr;
    QLabel* m_plantComboLabel = nullptr;
    QHBoxLayout* m_chain = nullptr;
    QWidget* m_chainScroll = nullptr;
    QStackedWidget* m_pages = nullptr;
    PlantPage* m_plantPage = nullptr;
    BlockPage* m_blockPage = nullptr;
    QWidget* m_otherPage = nullptr;
    QLabel* m_otherText = nullptr;

    struct Card {
        LabCard* card = nullptr;
        QLabel* kind = nullptr;
        QLabel* status = nullptr;
        Pick pick = Pick::Plant;
        int index = 0;
    };
    QVector<Card> m_cards;
    QString m_chainSignature;
    Pick m_pick = Pick::Plant;
    int m_pickIndex = 0;
    QString m_otherId;
    // A controller device asked for and not live yet: picked (that block)
    // when it is.
    QString m_pendingController;
    int m_pendingPick = 0;
    QSet<QObject*> m_persisted;
};

}  // namespace tracelab
