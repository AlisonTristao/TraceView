#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include "devices/deviceconfigdialog.h"
#include "devices/robotlinkspanel.h"

using namespace traceview;

class TestDeviceConfigDialog : public QObject {
    Q_OBJECT
private slots:
    void nameAndTransportsPersist() {
        Device initial;
        initial.id = "robot";
        DeviceConfigDialog dialog(initial);
        auto* panel = dialog.findChild<RobotLinksPanel*>();
        QVERIFY(panel);
        // The robot name is the panel's first combo; every link row's target
        // is an editable combo too, left on Automatic here.
        const auto combos = panel->findChildren<QComboBox*>();
        QVERIFY(!combos.isEmpty());
        combos.first()->setCurrentText("  Robo 2  ");
        for (auto* check : panel->findChildren<QCheckBox*>()) check->setChecked(true);
        const Device result = dialog.result();
        QCOMPARE(result.name, QString("Robo 2"));
        QCOMPARE(result.robotName, QString("Robo 2"));
        QVERIFY(deviceLinkCount(result) >= 2);
        for (int i = 0; i < deviceLinkCount(result); ++i) {
            const auto link = deviceLinkAt(result, i);
            QVERIFY(link.enabled);
            QVERIFY(link.autoTarget);
        }
        for (auto* combo : combos.mid(1)) {
            QVERIFY(combo->isEditable());
            QVERIFY(combo->currentText().startsWith("Automatic"));
        }
        bool ok = false;
        const auto restored = deviceFromJson(deviceToJson(result), &ok);
        QVERIFY(ok);
        QCOMPARE(deviceLinksSignature(restored), deviceLinksSignature(result));
        for (auto* check : panel->findChildren<QCheckBox*>()) check->setChecked(false);
        const auto disabled = dialog.result();
        for (int i = 0; i < deviceLinkCount(disabled); ++i)
            QVERIFY(!deviceLinkUsable(disabled, deviceLinkAt(disabled, i)));
    }

    void asynchronousContentDoesNotResizeWindow() {
        Device initial;
        initial.robotName = "Robot";
        DeviceConfigDialog dialog(initial);
        dialog.show();
        QTest::qWait(50);
        const QSize size = dialog.size();
        for (int i = 0; i < 5; ++i) {
            dialog.setAvailablePorts({{"COM5", QString(300, 'x'), "Robot"}});
            dialog.addDiscoveredBleDevice(QString(300, 'b'), QString::number(i));
            dialog.setReportedInfo({{"name", "Name", QString(2000, 'x')}});
            dialog.setAvailableHubPeers({});
            QTest::qWait(20);
            QCOMPARE(dialog.size(), size);
        }
        for (auto* button : dialog.findChildren<QToolButton*>()) {
            if (button->text() == "Advanced") button->setChecked(true);
        }
        QTest::qWait(20);
        QCOMPARE(dialog.size(), size);
    }

    void manualConfigurationIsPreserved() {
        Device initial;
        initial.id = "existing";
        initial.name = "My robot";
        initial.transportType = TransportType::Tcp;
        initial.tcpHost = "192.168.4.1";
        initial.tcpPort = 1234;
        DeviceConfigDialog dialog(initial);
        const auto result = dialog.result();
        QCOMPARE(result.name, initial.name);
        QCOMPARE(result.tcpHost, initial.tcpHost);
        QCOMPARE(result.tcpPort, initial.tcpPort);
        QVERIFY(!result.autoTarget);
    }

    void cachedDescriptionFillsAnOfflineDialogUntilLiveDataArrives() {
        Device initial;
        initial.id = "offline";
        DeviceConfigDialog dialog(initial);
        auto* catalog = dialog.findChild<QPlainTextEdit*>();
        QVERIFY(catalog);
        const auto infoText = [&dialog]() {
            for (auto* label : dialog.findChildren<QLabel*>()) {
                if (label->text().contains("Firmware") || label->text().contains("nothing reported"))
                    return label->text();
            }
            return QString();
        };

        CatalogTopicInfo cachedTopic;
        cachedTopic.sourceId = 0x9F442484;
        cachedTopic.topicId = 1;
        cachedTopic.name = "robot.state";
        dialog.setCachedDescription({cachedTopic}, {{"fw_version", "Firmware", "1dd9fc5"}});
        QVERIFY(catalog->toPlainText().contains("saved from the last connection"));
        QVERIFY(catalog->toPlainText().contains("robot.state"));
        QVERIFY(infoText().contains("saved from the last connection"));
        QVERIFY(infoText().contains("1dd9fc5"));

        // Live data replaces the cache, and gives way back to it when it goes.
        CatalogTopicInfo liveTopic = cachedTopic;
        liveTopic.name = "robot.live";
        dialog.setCatalogTopics({liveTopic});
        dialog.setReportedInfo({{"fw_version", "Firmware", "abc1234"}});
        QVERIFY(!catalog->toPlainText().contains("saved from the last connection"));
        QVERIFY(catalog->toPlainText().contains("robot.live"));
        QVERIFY(infoText().contains("abc1234"));
        dialog.setCatalogTopics({});
        dialog.setReportedInfo({});
        QVERIFY(catalog->toPlainText().contains("robot.state"));

        // The cache is display-only: never handed back as the device's own.
        QVERIFY(dialog.result().reportedInfo.isEmpty());
    }
};

QTEST_MAIN(TestDeviceConfigDialog)
#include "test_deviceconfigdialog.moc"
