#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QAction;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QScrollArea;
class QToolButton;
class QStackedWidget;
class QVBoxLayout;

namespace traceview {
class ModuleHost;
}

namespace tracelab {

class BlockPage;
class CodeBlockOwner;
class LabControllerDevice;
class LabPlantDevice;
class LabSection;
class LoopDiagram;
class PlantPage;
class SimulationPage;

// The TraceLab tab's page, laid out like Settings: a menu of what can be
// done down the left, the picked page on the right. Overview draws the loop
// of the plant as a chain of cards -- the plant, the filters, the
// controller -- with a "+" where a block can be added; Simulation drives
// the run and its clock (SimulationPage); Plant holds what the plant is and
// its settings (PlantPage); Code, Parameters and Signals show one code
// block each -- picked on top of the page, the same block on the three --
// in parts of a BlockPage (its code opens in VS Code or a window of its
// own, CodeWindow); Scenes puts the plant's animation on the dashboard. It
// knows plants and controller devices only through LabPlantDevice and
// LabControllerDevice (lab/control/labdevices.h): what kinds of plant exist
// is whatever the benchmarks registered. Every change a device makes to its
// configuration is stored in the project
// (ModuleHost::updateModuleDeviceConfig()).
class LabPanel : public QWidget {
    Q_OBJECT

public:
    // The menu's pages, in order.
    enum class Category { Overview, Simulation, Plant, Code, Parameters, Signals, Scenes };

    // `host` adds the devices and widgets the tools build; it must outlive
    // the panel (null: the tools do nothing).
    explicit LabPanel(traceview::ModuleHost* host, QWidget* parent = nullptr);

    // The ribbon's buttons (parented to this panel), in two groups split by
    // a separator: Add plant, Add controller, Add scene | Play, Pause,
    // Reset, Step.
    QList<QAction*> ribbonActions() const;

    // The benchmark projects (File > Open Example).
    static QString lineFollowerProjectPath();
    static QString furutaProjectPath();

    void showCategory(Category category);
    Category currentCategory() const;
    // The block the Code, Parameters and Signals pages show (null: none).
    CodeBlockOwner* currentBlockOwner() const;
    int currentBlock() const;

public slots:
    // A plant of the first registered kind, with its default settings (the
    // Add plant button's menu offers every kind: addPlantOfType()).
    void addPlant();
    void addPlantOfType(int index);
    // A controller device on the current plant, with the first controller
    // template of the plant's benchmark; picks it. Nothing without a plant.
    void addController();
    // A Scene bound by name to the current plant and controller: the
    // plant's benchmark's scene (by type id), the line follower's otherwise.
    void addScene();
    void play();
    void pauseSimulation();
    void resetSimulation();
    void stepSimulation();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // A code block the Code, Parameters and Signals pages can show.
    struct BlockRef {
        QPointer<CodeBlockOwner> owner;
        int block = 0;
        QString label;
    };
    // The three pages of a block.
    struct BlockCategory {
        BlockPage* page = nullptr;
        QComboBox* combo = nullptr;
        QWidget* content = nullptr;  // the block's row and its page
        QWidget* none = nullptr;     // shown without a block
    };
    enum class CardKind { Plant, Filter, Controller, Other };
    // How a block of the loop stands: tone (0 neutral, 1 good, 2 warning,
    // 3 bad), a word, what it is, a line.
    struct BlockStatus {
        int tone = 0;
        QString header;
        QString kind;
        QString status;
    };

    void buildMenu();
    QWidget* buildOverview();
    QWidget* buildBlockCategory(const QString& title, const QString& description, int part,
                                BlockCategory* category);
    QWidget* buildScenes();
    void addPage(QWidget* page);
    void applyTheme();
    void applyCompactLayout(bool compact);
    void fitCategoryListHeight();

    void refreshDevices();
    void rebuildChain();
    void rebuildBlocks();
    BlockStatus blockStatus(CardKind what, int index) const;
    // The diagram from the blocks' declarations, with the values now.
    void refreshDiagram();
    void refreshScenes();
    void refreshLive();
    void selectBlock(CodeBlockOwner* owner, int block);
    void onCardClicked(CardKind kind, int index);
    void persistDevices();
    LabPlantDevice* plant() const;
    LabControllerDevice* controller() const;
    QString plantId() const;
    // The benchmark the current plant comes from ("linefollower", "furuta"):
    // the first segment of its template's id, or of its file's origin line.
    QString plantBenchmark() const;
    // The scene that animates the current plant (a SceneRegistry type id).
    QString sceneType() const;
    void fillBlockMenu(QMenu* menu, bool controllerBlock);
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
    QVBoxLayout* m_mainLayout = nullptr;
    QHBoxLayout* m_bodyLayout = nullptr;
    QWidget* m_plantRow = nullptr;
    QComboBox* m_plantCombo = nullptr;
    QListWidget* m_categoryList = nullptr;
    QStringList m_categoryNames;
    QStackedWidget* m_pages = nullptr;
    QList<QVBoxLayout*> m_pageLayouts;
    bool m_compact = false;

    // Overview.
    LoopDiagram* m_diagram = nullptr;
    QScrollArea* m_diagramScroll = nullptr;
    QToolButton* m_addFilterButton = nullptr;
    QToolButton* m_addControllerButton = nullptr;
    LabSection* m_legacySection = nullptr;
    QLabel* m_legacyText = nullptr;
    QString m_legacyId;

    SimulationPage* m_simulationPage = nullptr;
    PlantPage* m_plantPage = nullptr;
    BlockCategory m_blockCategories[3];  // Code, Parameters, Signals
    QPushButton* m_addBlockButton = nullptr;

    // Scenes.
    QLabel* m_sceneText = nullptr;
    QPushButton* m_addSceneButton = nullptr;

    QString m_chainSignature;
    QVector<BlockRef> m_blocks;
    int m_blockIndex = -1;
    // A controller device asked for and not live yet: its block is picked
    // when it is.
    QString m_pendingController;
    int m_pendingPick = 0;
    QSet<QObject*> m_persisted;
};

}  // namespace tracelab
