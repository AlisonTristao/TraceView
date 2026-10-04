// The TraceLab tab (lab/module/labpanel.h) on a live line follower: the loop
// drawn as plant > filters > controller cards; "+ Filter" and "+ Controller"
// adding blocks from the templates through the host; picking a block showing
// its code, parameters and signals; Save turning an edited template into a
// file of the user's (the template untouched, the origin line on top) and
// "Back to the original" restoring it; every configuration change reaching
// the project through ModuleHost::updateModuleDeviceConfig(). Offscreen;
// with TRACELAB_PANEL_SHOTS=<folder> it also saves screenshots there.

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QtTest>
#include <memory>

#include "dashboard/widgetregistry.h"
#include "lab/benchmarks/linefollower/code/lftemplates.h"
#include "lab/benchmarks/linefollower/device/lfplantdevice.h"
#include "lab/code/codefiles.h"
#include "lab/control/controllerhost.h"
#include "lab/control/labdevices.h"
#include "lab/module/blockeditor.h"
#include "lab/module/codeeditor.h"
#include "lab/module/labcard.h"
#include "lab/module/labpanel.h"
#include "lab/module/plantpage.h"
#include "modules/appmodule.h"

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

QToolButton* addButton(QWidget* panel, const QString& text) {
    for (QToolButton* button : panel->findChildren<QToolButton*>(QStringLiteral("labAddBlock"))) {
        if (button->text() == text && button->isVisible()) {
            return button;
        }
    }
    return nullptr;
}

QList<LabCard*> cards(QWidget* panel) {
    QList<LabCard*> list;
    for (LabCard* card : panel->findChildren<LabCard*>()) {
        if (card->isVisible()) {
            list.append(card);
        }
    }
    std::sort(list.begin(), list.end(),
              [](LabCard* a, LabCard* b) { return a->mapToGlobal(QPoint()).x() < b->mapToGlobal(QPoint()).x(); });
    return list;
}

}  // namespace

class TestLabPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
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
        QTRY_COMPARE(cards(&panel).size(), 1);  // the plant
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
        QTRY_COMPARE(cards(&panel).size(), 2);
        auto* editor = panel.findChild<BlockEditor*>();
        auto* pages = panel.findChild<QStackedWidget*>();
        QCOMPARE(pages->currentWidget(), editor);  // the new block is picked
        auto* code = editor->findChild<CodeEditor*>();
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
        QTRY_COMPARE(cards(&panel).size(), 3);
        QCOMPARE(editor->block(), 0);
        QVERIFY(host.m_configs.value(QStringLiteral("ctl1"))
                    .value(QStringLiteral("filters"))
                    .toArray()
                    .size() == 1);
        QTest::qWait(300);
        editor->refreshLive();
        shoot(&panel, QStringLiteral("3-filter"));

        // Back to the controller; edit the template and save: a file of the
        // user's, starting with the origin line, now the block's source.
        cards(&panel).last()->clicked();
        QCOMPARE(editor->block(), int(LabControllerDevice::kController));
        QString text = code->toPlainText();
        text.replace(QStringLiteral("TL_PARAM(base, 60,"), QStringLiteral("TL_PARAM(base, 55,"));
        code->setPlainText(text);
        QVERIFY(editor->hasUnsavedChanges());
        shoot(&panel, QStringLiteral("4-edited"));
        auto* save = editor->findChild<QPushButton*>();
        for (QPushButton* button : editor->findChildren<QPushButton*>()) {
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
        QVERIFY(!editor->hasUnsavedChanges());
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
        shoot(&panel, QStringLiteral("5-saved"));

        // A broken save shows its line; the previous code keeps the loop.
        code->setPlainText(code->toPlainText() + QStringLiteral("\nint broken = ;\n"));
        save->click();
        QTRY_VERIFY(!controller->messages(LabControllerDevice::kController).isEmpty());
        QVERIFY(controller->host()->configured());
        editor->refreshLive();
        shoot(&panel, QStringLiteral("6-error"));

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
        cards(&panel).first()->clicked();
        QCOMPARE(pages->currentWidget(), panel.findChild<PlantPage*>());
        shoot(&panel, QStringLiteral("7-plant-page"));
        plant.pause();
    }
};

QTEST_MAIN(TestLabPanel)
#include "test_labpanel.moc"
