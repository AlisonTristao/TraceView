// The TraceLab tab (lab/module/labpanel.h) on a live line follower: its menu
// of pages as Settings'; the loop drawn on Overview as a block diagram
// (plant > filters > controller, the signals between them); "+ Filter" and "+ Controller" adding blocks from the
// templates through the host; picking a block showing it on the Code page
// (and the same block on Parameters and Signals); Save turning an edited template into a
// file of the user's (the template untouched, the origin line on top) and
// "Back to the original" restoring it; every configuration change reaching
// the project through ModuleHost::updateModuleDeviceConfig(); the page
// without the code, which opens in a window of its own (CodeWindow); what
// opening it in VS Code passes and the editor configuration it writes. Offscreen (nothing is started); with
// TRACELAB_PANEL_SHOTS=<folder> it also saves screenshots there.

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>
#include <memory>

#include "dashboard/widgetregistry.h"
#include "lab/benchmarks/furuta/code/furutatemplates.h"
#include "lab/benchmarks/linefollower/code/lftemplates.h"
#include "lab/benchmarks/linefollower/device/lfplantdevice.h"
#include "lab/benchmarks/linefollower/device/trackenvironment.h"
#include "lab/code/codefiles.h"
#include "lab/code/coderuntime.h"
#include "lab/control/controllerhost.h"
#include "lab/control/labdevices.h"
#include "lab/plant/codeplantdevice.h"
#include "lab/module/blockpage.h"
#include "lab/module/codeeditor.h"
#include "lab/module/codewindow.h"
#include "lab/module/externaleditor.h"
#include "lab/module/labpanel.h"
#include "lab/module/labui.h"
#include "lab/module/loopdiagram.h"
#include "lab/module/plantpage.h"
#include "modules/appmodule.h"
#include "traceview/thememanager.h"

using namespace tracelab;

namespace {

// Adds the controller devices the panel asks for, live, as MainWindow would.
class Host final : public traceview::ModuleHost {
public:
    traceview::WidgetRegistry& widgetRegistry() override {
        return traceview::WidgetRegistry::instance();
    }
    void addRibbonTab(const QString&, const QList<QAction*>&, QWidget*) override {}
    QString addModuleDevice(const QString& typeId, const QString& name,
                            const QJsonObject& config) override {
        Q_UNUSED(name);
        const QString id = QStringLiteral("ctl%1").arg(++m_count);
        if (typeId == QLatin1String(kLabControllerDeviceType)) {
            auto device = std::make_unique<LabControllerDevice>(id, config);
            LabDeviceRegistry::instance().addController(id, device.get());
            m_devices.push_back(std::move(device));
        }
        return id;
    }
    bool updateModuleDeviceConfig(const QString& deviceId, const QJsonObject& config) override {
        m_configs.insert(deviceId, config);
        return true;
    }

    int m_count = 0;
    std::vector<std::unique_ptr<LabControllerDevice>> m_devices;
    QHash<QString, QJsonObject> m_configs;
};

void shoot(QWidget* widget, const QString& name) {
    const QString folder = qEnvironmentVariable("TRACELAB_PANEL_SHOTS");
    if (folder.isEmpty()) {
        return;
    }
    QDir().mkpath(folder);
    QCoreApplication::processEvents();
    widget->grab().save(folder + QLatin1Char('/') + name + QStringLiteral(".png"));
}

// The chain is rebuilt with new widgets (the old ones deleted at once), so
// what is there is current, shown or not (Overview may not be the page).
QToolButton* addButton(QWidget* panel, const QString& text) {
    for (QToolButton* button : panel->findChildren<QToolButton*>(QStringLiteral("labAddBlock"))) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

// The loop's diagram on Overview, and its blocks (the plant first; the
// controller, or the place for one, last).
LoopDiagram* diagram(QWidget* panel) {
    QCoreApplication::processEvents();
    return panel->findChild<LoopDiagram*>();
}

QVector<LoopDiagram::Block> blocks(QWidget* panel) {
    return diagram(panel)->model().blocks;
}

void clickBlock(QWidget* panel, int index) {
    LoopDiagram* d = diagram(panel);
    QTest::mouseClick(d, Qt::LeftButton, {}, d->blockRect(index).center().toPoint());
}

// The BlockPage on the page shown (Code, Parameters or Signals).
BlockPage* shownBlockPage(QWidget* panel) {
    auto* pages = panel->findChild<QStackedWidget*>();
    return pages && pages->currentWidget() ? pages->currentWidget()->findChild<BlockPage*>()
                                           : nullptr;
}

}  // namespace

class TestLabPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Screenshots in the application's look.
        if (!qEnvironmentVariableIsEmpty("TRACELAB_PANEL_SHOTS")) {
            traceview::ThemeManager::instance().applyCurrentTheme();
        }
        lf::registerLineFollowerTemplates();
        // Start from an empty folder of blocks.
        for (code::BlockKind kind : {code::BlockKind::Controller, code::BlockKind::Filter}) {
            QDir(code::CodeLibrary::userDirectory(kind)).removeRecursively();
        }
    }

    void buildsAndEditsTheLoop() {
        Host host;
        LabPanel panel(&host);
        panel.resize(1500, 900);
        panel.show();
        shoot(&panel, QStringLiteral("0-empty"));

        lf::PlantSettings settings;
        settings.autoRun = false;
        lf::LinePlantDevice plant(0x7000001u, settings);
        LabDeviceRegistry::instance().addPlant(QStringLiteral("plant"), &plant);
        // The plant, and the place for a controller.
        QTRY_COMPARE(blocks(&panel).size(), 2);
        QCOMPARE(blocks(&panel).last().kind, LoopDiagram::Block::NoController);
        QCOMPARE(panel.currentCategory(), LabPanel::Category::Overview);
        // The menu: Settings' list down the left, one row a page.
        auto* menu = panel.findChild<QListWidget*>(QStringLiteral("labMenu"));
        QVERIFY(menu && menu->isVisible());
        QCOMPARE(menu->count(), 7);
        QVERIFY(addButton(&panel, QStringLiteral("+ Filter")));
        QToolButton* addController = addButton(&panel, QStringLiteral("+ Controller"));
        QVERIFY(addController);
        shoot(&panel, QStringLiteral("1-plant"));

        // + Controller > the PD template: a controller device on this plant.
        QAction* pd = nullptr;
        for (QAction* action : addController->menu()->actions()) {
            if (action->text() == QLatin1String("PD (line sensors)")) {
                pd = action;
            }
        }
        QVERIFY(pd);
        pd->trigger();
        QCOMPARE(host.m_devices.size(), std::size_t(1));
        LabControllerDevice* controller = host.m_devices.front().get();
        QCOMPARE(controller->plantDeviceId(), QStringLiteral("plant"));
        QTRY_VERIFY(controller->host()->plantReady());
        QTRY_VERIFY(controller->host()->configured());
        QTRY_COMPARE(blocks(&panel).last().kind, LoopDiagram::Block::Controller);
        QCOMPARE(blocks(&panel).size(), 2);
        // The new block is picked, on the Code page.
        QCOMPARE(panel.currentCategory(), LabPanel::Category::Code);
        QCOMPARE(panel.currentBlockOwner(), static_cast<CodeBlockOwner*>(controller));
        QCOMPARE(panel.currentBlock(), int(LabControllerDevice::kController));
        BlockPage* editor = shownBlockPage(&panel);
        QVERIFY(editor);
        QCOMPARE(editor->owner(), static_cast<CodeBlockOwner*>(controller));
        // The page has no code: its one button opens it, here in a window.
        QVERIFY(!editor->findChild<CodeEditor*>());
        QVERIFY(editor->openCodeButton()->isVisible());
        QAction* inWindow = nullptr;
        QStringList choices;
        for (QAction* action : editor->openCodeButton()->menu()->actions()) {
            choices.append(action->text());
            if (action->text() == QLatin1String("In a window of its own")) {
                inWindow = action;
            }
        }
        QVERIFY(choices.contains(QStringLiteral("In VS Code")));
        QVERIFY(inWindow);
        editor->openCodeButton()->menu()->triggered(inWindow);
        CodeWindow* window = CodeWindow::find(controller, LabControllerDevice::kController);
        QVERIFY(window && window->isVisible());
        auto* code = window->editor();
        QVERIFY(code->toPlainText().contains(QLatin1String("TL_IN_ARRAY(s, N);")));
        plant.run();
        QTest::qWait(400);
        editor->refreshLive();
        shoot(&panel, QStringLiteral("2-controller"));

        // + Filter > the low-pass: on the same device, between the two.
        QToolButton* addFilter = addButton(&panel, QStringLiteral("+ Filter"));
        QAction* lowpass = nullptr;
        for (QAction* action : addFilter->menu()->actions()) {
            if (action->text() == QLatin1String("Low-pass (wheel speeds)")) {
                lowpass = action;
            }
        }
        QVERIFY(lowpass);
        lowpass->trigger();
        QCOMPARE(controller->filterCount(), 1);
        QTRY_COMPARE(blocks(&panel).size(), 3);
        // The diagram reads the blocks' declarations: the low-pass replaces
        // the wheel speeds (TL_INOUT), which the plant gives.
        {
            const LoopDiagram::Block filter = blocks(&panel)[1];
            QCOMPARE(filter.kind, LoopDiagram::Block::Filter);
            QVERIFY(filter.reads.contains(QStringLiteral("w_l")));
            QVERIFY(filter.writes.contains(QStringLiteral("w_l")));
            QVERIFY(blocks(&panel).first().writes.contains(QStringLiteral("w_l")));
            QVERIFY(!blocks(&panel).last().writes.isEmpty());  // the controller's commands
        }
        QCOMPARE(editor->block(), 0);
        // The same block on Parameters and Signals.
        panel.showCategory(LabPanel::Category::Parameters);
        QVERIFY(shownBlockPage(&panel) && shownBlockPage(&panel) != editor);
        QCOMPARE(shownBlockPage(&panel)->owner(), static_cast<CodeBlockOwner*>(controller));
        QCOMPARE(shownBlockPage(&panel)->block(), 0);
        shoot(&panel, QStringLiteral("3-parameters"));
        panel.showCategory(LabPanel::Category::Signals);
        QCOMPARE(shownBlockPage(&panel)->block(), 0);
        shownBlockPage(&panel)->refreshLive();
        shoot(&panel, QStringLiteral("3-signals"));
        panel.showCategory(LabPanel::Category::Overview);
        QVERIFY(host.m_configs.value(QStringLiteral("ctl1"))
                    .value(QStringLiteral("filters"))
                    .toArray()
                    .size() == 1);
        QTest::qWait(300);
        editor->refreshLive();
        shoot(&panel, QStringLiteral("3-filter"));

        // Back to the controller; edit the template and save: a file of the
        // user's, starting with the origin line, now the block's source.
        clickBlock(&panel, blocks(&panel).size() - 1);
        QCOMPARE(panel.currentCategory(), LabPanel::Category::Code);
        QCOMPARE(editor->block(), int(LabControllerDevice::kController));
        QString text = code->toPlainText();
        text.replace(QStringLiteral("TL_PARAM(base, 60,"), QStringLiteral("TL_PARAM(base, 55,"));
        code->setPlainText(text);
        QVERIFY(window->hasUnsavedChanges());
        shoot(window, QStringLiteral("4-edited-window"));
        auto* save = window->findChild<QPushButton*>();
        for (QPushButton* button : window->findChildren<QPushButton*>()) {
            if (button->text() == QLatin1String("Save")) {
                save = button;
            }
        }
        save->click();
        const QString path = controller->spec(LabControllerDevice::kController).path;
        QVERIFY(!path.isEmpty());
        QVERIFY(path.startsWith(code::CodeLibrary::userDirectory(code::BlockKind::Controller)));
        const QString saved = code::CodeLibrary::readFile(path);
        QCOMPARE(code::CodeLibrary::originOf(saved), QStringLiteral("linefollower/pd"));
        QVERIFY(saved.contains(QLatin1String("TL_PARAM(base, 55,")));
        QVERIFY(code::CodeLibrary::instance().isModified(saved));
        QVERIFY(!window->hasUnsavedChanges());
        QTRY_COMPARE(controller->module(LabControllerDevice::kController)
                         ->paramValue(QStringLiteral("base")),
                     55.0);
        QCOMPARE(host.m_configs.value(QStringLiteral("ctl1"))
                     .value(QStringLiteral("controller"))
                     .toObject()
                     .value(QStringLiteral("path"))
                     .toString(),
                 path);
        QVERIFY(code::CodeLibrary::instance()
                    .templateSource(QStringLiteral("linefollower/pd"))
                    .contains(QLatin1String("TL_PARAM(base, 60,")));
        editor->refreshLive();
        shoot(&panel, QStringLiteral("5-saved"));

        // A broken save shows its line; the previous code keeps the loop.
        code->setPlainText(code->toPlainText() + QStringLiteral("\nint broken = ;\n"));
        save->click();
        QTRY_VERIFY(!controller->messages(LabControllerDevice::kController).isEmpty());
        QVERIFY(controller->host()->configured());
        editor->refreshLive();
        shoot(&panel, QStringLiteral("6-error"));
        shoot(window, QStringLiteral("6-error-window"));

        // Back to the original: the template's code in the file again.
        for (QPushButton* button : editor->findChildren<QPushButton*>()) {
            if (button->text() == QLatin1String("Back to the original")) {
                QVERIFY(button->isEnabled());
            }
        }
        const QString original =
            code::CodeLibrary::instance().originalOf(code::CodeLibrary::readFile(path));
        code->setPlainText(original);
        save->click();
        QTRY_VERIFY(!code::CodeLibrary::instance().isModified(code::CodeLibrary::readFile(path)));
        window->close();
        QTRY_VERIFY(!CodeWindow::find(controller, LabControllerDevice::kController));
        // No value was set by hand: base is the code's default again.
        QTRY_COMPARE(controller->module(LabControllerDevice::kController)
                         ->paramValue(QStringLiteral("base")),
                     60.0);

        // A parameter set on the side panel reaches the code and the project.
        controller->setParam(LabControllerDevice::kController, QStringLiteral("kp"), 1800);
        QCOMPARE(controller->module(LabControllerDevice::kController)
                     ->paramValue(QStringLiteral("kp")),
                 1800.0);
        QCOMPARE(host.m_configs.value(QStringLiteral("ctl1"))
                     .value(QStringLiteral("controller"))
                     .toObject()
                     .value(QStringLiteral("params"))
                     .toObject()
                     .value(QStringLiteral("kp"))
                     .toDouble(),
                 1800.0);

        // The plant's card: its settings as it describes them.
        panel.showCategory(LabPanel::Category::Overview);
        clickBlock(&panel, 0);
        QCOMPARE(panel.currentCategory(), LabPanel::Category::Plant);
        shoot(&panel, QStringLiteral("7-plant-page"));
        panel.showCategory(LabPanel::Category::Simulation);
        shoot(&panel, QStringLiteral("7-simulation-page"));
        panel.showCategory(LabPanel::Category::Scenes);
        shoot(&panel, QStringLiteral("7-scenes-page"));
        // A phone's width: the menu down to its glyphs.
        panel.resize(420, 800);
        QTRY_VERIFY(menu->width() < 100);
        QVERIFY(menu->item(0)->text().isEmpty());
        shoot(&panel, QStringLiteral("7-compact"));
        plant.pause();
    }

    // A plant in C: the diagram takes its outputs and inputs from its code,
    // with their units; the controller's TL_IN and TL_OUT are its ports.
    void diagramOfACodePlant() {
        furuta::registerFurutaTemplates();
        Host host;
        LabPanel panel(&host);
        panel.resize(1500, 900);
        panel.show();
        CodePlantDevice plant(QStringLiteral("furuta-plant"), 0x7300009u,
                              QJsonObject{{"plant", QJsonObject{{"template", "furuta/pendulum"}}},
                                          {"autoRun", false}});
        QVERIFY(plant.codeBlocks()->compiled(0));
        LabDeviceRegistry::instance().addPlant(QStringLiteral("furuta-plant"), &plant);
        host.addModuleDevice(
            QString::fromLatin1(kLabControllerDeviceType), QStringLiteral("controller"),
            QJsonObject{{"plantDeviceId", "furuta-plant"},
                        {"controller", QJsonObject{{"template", "furuta/energy_lqr"}}}});
        QTRY_COMPARE(blocks(&panel).last().kind, LoopDiagram::Block::Controller);
        panel.showCategory(LabPanel::Category::Overview);
        const LoopDiagram::Model model = diagram(&panel)->model();
        QStringList outputs;
        QString commandUnit;
        for (const LoopDiagram::Signal& signal : model.signalList) {
            if (signal.kind == LoopDiagram::Signal::Measurement) {
                outputs.append(signal.name + QLatin1Char('/') + signal.unit);
            } else if (signal.kind == LoopDiagram::Signal::Command && signal.name == "u") {
                commandUnit = signal.unit;
            }
        }
        QCOMPARE(outputs, (QStringList{"th1/rad", "th2/rad", "w1/rad/s", "w2/rad/s"}));
        QCOMPARE(commandUnit, QStringLiteral("N m"));
        QCOMPARE(model.blocks.first().reads, QStringList{QStringLiteral("u")});
        QVERIFY(model.blocks.last().reads.contains(QStringLiteral("th2")));
        QCOMPARE(model.blocks.last().writes, QStringList{QStringLiteral("u")});
        // Its blocks open their pages: the controller its code.
        clickBlock(&panel, model.blocks.size() - 1);
        QCOMPARE(panel.currentCategory(), LabPanel::Category::Code);
        panel.showCategory(LabPanel::Category::Overview);
        shoot(&panel, QStringLiteral("11-furuta-diagram"));
    }

    void codeInItsOwnWindow() {
        LabControllerDevice device(
            QStringLiteral("ctl-window"),
            QJsonObject{{"controller", QJsonObject{{"template", "linefollower/pd"}}}});
        BlockPage page;
        page.setStyleSheet(ui::styleSheet(traceview::ThemeManager::instance().currentTheme()));
        page.resize(1366, 560);
        page.show();
        page.setBlock(&device, LabControllerDevice::kController);
        QVERIFY(!page.findChild<CodeEditor*>());
        shoot(&page, QStringLiteral("8-page"));

        // One window a block: opening again raises the same one.
        CodeWindow* window = CodeWindow::open(&device, LabControllerDevice::kController);
        QVERIFY(window && window->isWindow() && window->isVisible());
        QCOMPARE(CodeWindow::open(&device, LabControllerDevice::kController), window);
        QVERIFY(window->windowTitle().contains(QLatin1String("TraceLab")));
        QVERIFY(window->editor()->toPlainText().contains(QLatin1String("TL_PARAM(kp")));
        shoot(window, QStringLiteral("9-window"));

        // A signal double-clicked on the page goes into the open code.
        auto* signalList = page.findChild<QListWidget*>(QStringLiteral("labSignals"));
        QVERIFY(signalList);
        QListWidgetItem* declarable = nullptr;
        page.refreshLive();
        for (int i = 0; i < signalList->count(); ++i) {
            if (!signalList->item(i)->data(Qt::UserRole).toString().isEmpty()) {
                declarable = signalList->item(i);
                break;
            }
        }
        if (declarable) {
            const QString declaration = declarable->data(Qt::UserRole).toString();
            signalList->itemDoubleClicked(declarable);
            QVERIFY(window->editor()->toPlainText().contains(declaration));
            QVERIFY(window->hasUnsavedChanges());
        }

        // Unsaved edits: closing it asks (answered Discard here by dropping
        // them first); then the window is gone.
        window->reload();
        QPointer<CodeWindow> gone = window;
        window->close();
        QTRY_VERIFY(gone.isNull());
        QVERIFY(!CodeWindow::find(&device, LabControllerDevice::kController));

        // A device going away takes its window along.
        auto temporary = std::make_unique<LabControllerDevice>(
            QStringLiteral("ctl-temporary"),
            QJsonObject{{"controller", QJsonObject{{"template", "linefollower/pd"}}}});
        QPointer<CodeWindow> orphan = CodeWindow::open(temporary.get(),
                                                       LabControllerDevice::kController);
        QVERIFY(orphan);
        temporary.reset();
        QTRY_VERIFY(orphan.isNull());
    }

    void plantPageOfCodePlant() {
        lf::registerTrackEnvironment();
        CodePlantDevice plant(
            QStringLiteral("car-page"), 0x7300003u,
            QJsonObject{{"plant", QJsonObject{{"template", "linefollower/car"}}},
                        {"environment", QJsonObject{{"id", "linefollower.track"}}},
                        {"autoRun", false}});
        QVERIFY(plant.codeBlocks()->compiled(0));
        PlantPage page;
        page.setStyleSheet(ui::styleSheet(traceview::ThemeManager::instance().currentTheme()));
        page.resize(1366, 560);  // what a laptop leaves below the cards
        page.show();
        page.setPlant(&plant);
        page.refreshLive();
        // The plant's code is on the Code page: a button goes there; the
        // page holds no editor.
        QPushButton* goToCode = nullptr;
        for (QPushButton* button : page.findChildren<QPushButton*>()) {
            if (button->text() == QLatin1String("Go to its code")) {
                goToCode = button;
            }
        }
        QVERIFY(goToCode && goToCode->isVisible());
        QSignalSpy codeRequested(&page, &PlantPage::codeRequested);
        goToCode->click();
        QCOMPARE(codeRequested.count(), 1);
        QVERIFY(!page.findChild<CodeEditor*>());
        QStringList titleTexts;
        for (QGroupBox* section : page.findChildren<QGroupBox*>()) {
            titleTexts.append(section->title());
        }
        QVERIFY2(titleTexts.contains(plant.worldName()), qPrintable(titleTexts.join(", ")));
        shoot(&page, QStringLiteral("10-plant-page-code"));
    }

    void vsCodeArguments() {
        const QString root = code::CodeLibrary::userRoot();
        const QString file = root + QStringLiteral("/controllers/pd.c");
        // The TraceLab folder as the window's folder (its configuration),
        // the file at the line.
        QCOMPARE(external::vsCodeArguments(file, 12, root),
                 (QStringList{QDir::toNativeSeparators(root), QStringLiteral("--goto"),
                              QDir::toNativeSeparators(file) + QStringLiteral(":12")}));
        // A file elsewhere opens alone.
        const QString elsewhere = QDir::tempPath() + QStringLiteral("/elsewhere/x.c");
        QCOMPARE(external::vsCodeArguments(elsewhere, 0, root),
                 QStringList{QDir::toNativeSeparators(elsewhere)});
        // Never the user's Documents in a test.
        QVERIFY(!root.startsWith(
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
    }

    void editorConfiguration() {
        QString error;
        QVERIFY2(code::CodeLibrary::writeEditorConfig(&error), qPrintable(error));
        const QString root = code::CodeLibrary::userRoot();
        const QString include = QDir::cleanPath(code::CodeRuntime::instance().directory());
        QVERIFY(QFileInfo::exists(include + QStringLiteral("/tracelab.h")));
        const QString flags = code::CodeLibrary::readFile(root + QStringLiteral("/compile_flags.txt"));
        QVERIFY(flags.split(QLatin1Char('\n')).contains(QStringLiteral("-I") +
                                                         QDir::toNativeSeparators(include)));
        QVERIFY(flags.contains(QLatin1String("-nostdinc")));
        const QString propertiesPath = root + QStringLiteral("/.vscode/c_cpp_properties.json");
        const QJsonObject configuration =
            QJsonDocument::fromJson(code::CodeLibrary::readFile(propertiesPath).toUtf8())
                .object()
                .value(QStringLiteral("configurations"))
                .toArray()
                .first()
                .toObject();
        QCOMPARE(configuration.value(QStringLiteral("name")).toString(), QStringLiteral("TraceLab"));
        QCOMPARE(configuration.value(QStringLiteral("includePath")).toArray().first().toString(),
                 include);
        // A file of the user's stays; a missing one comes back.
        QVERIFY(code::CodeLibrary::writeFile(propertiesPath, QStringLiteral("{}\n")));
        QVERIFY(code::CodeLibrary::writeEditorConfig(&error));
        QCOMPARE(code::CodeLibrary::readFile(propertiesPath), QStringLiteral("{}\n"));
        QVERIFY(QFile::remove(propertiesPath));
        QVERIFY(code::CodeLibrary::writeEditorConfig(&error));
        QVERIFY(code::CodeLibrary::readFile(propertiesPath).contains(QLatin1String("TraceLab")));
    }
};

QTEST_MAIN(TestLabPanel)
#include "test_labpanel.moc"
