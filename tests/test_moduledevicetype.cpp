// In-process device types an optional module registers (core/
// moduledevicetype.h): the registry, a DeviceConnection built for a
// TransportType::Module device asking the type's factory for its transport
// and dialing it, an unregistered type never coming up, and the device's
// .tvproj round trip (type and settings kept, nothing link-specific written).

#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

#include "core/deviceconnection.h"
#include "core/moduledevicetype.h"
#include "devices/device.h"

using traceview::Device;
using traceview::DeviceConnection;
using traceview::ModuleDeviceType;
using traceview::ModuleDeviceTypeRegistry;
using traceview::ModuleTransport;
using traceview::TransportType;

namespace {

const char kTypeId[] = "test.fake";

// Records what the factory and the connection did with it.
class FakeModuleTransport final : public ModuleTransport {
public:
    FakeModuleTransport(QString deviceId, QJsonObject config, QObject* parent)
        : ModuleTransport(parent), deviceId(std::move(deviceId)), config(std::move(config)) {}

    void open() override {
        ++opens;
        if (!m_open) {
            m_open = true;
            emit connectionStateChanged(true);
        }
    }
    void close() override {
        if (m_open) {
            m_open = false;
            emit connectionStateChanged(false);
        }
    }
    bool isConnected() const override {
        return m_open;
    }
    bool write(const QByteArray& data) override {
        written.append(data);
        return m_open;
    }

    QString deviceId;
    QJsonObject config;
    int opens = 0;
    QList<QByteArray> written;

private:
    bool m_open = false;
};

FakeModuleTransport* g_lastTransport = nullptr;

ModuleDeviceType fakeType() {
    ModuleDeviceType type;
    type.id = QString::fromLatin1(kTypeId);
    type.displayName = QStringLiteral("Fake");
    type.createTransport = [](const QString& deviceId, const QJsonObject& config,
                              QObject* parent) -> ModuleTransport* {
        g_lastTransport = new FakeModuleTransport(deviceId, config, parent);
        return g_lastTransport;
    };
    return type;
}

Device moduleDevice() {
    Device device;
    device.id = QStringLiteral("dev-1");
    device.name = QStringLiteral("Simulated");
    device.transportType = TransportType::Module;
    device.moduleType = QString::fromLatin1(kTypeId);
    device.moduleConfig = QJsonObject{{"seed", 1112}, {"controller", "gpc"}};
    return device;
}

class TestModuleDeviceType : public QObject {
    Q_OBJECT

private slots:
    void cleanup() {
        ModuleDeviceTypeRegistry::instance().unregisterType(QString::fromLatin1(kTypeId));
        g_lastTransport = nullptr;
    }

    void registryRefusesDuplicatesAndIncompleteTypes() {
        auto& registry = ModuleDeviceTypeRegistry::instance();
        QVERIFY(registry.registerType(fakeType()));
        QVERIFY(!registry.registerType(fakeType()));
        ModuleDeviceType noFactory;
        noFactory.id = QStringLiteral("test.nofactory");
        QVERIFY(!registry.registerType(noFactory));
        QVERIFY(!registry.registerType(ModuleDeviceType{}));
        QVERIFY(registry.find(QString::fromLatin1(kTypeId)) != nullptr);
        QCOMPARE(registry.find(QString::fromLatin1(kTypeId))->displayName,
                 QStringLiteral("Fake"));
        QVERIFY(registry.find(QStringLiteral("test.nofactory")) == nullptr);
    }

    void connectionDialsTheFactoryTransport() {
        QVERIFY(ModuleDeviceTypeRegistry::instance().registerType(fakeType()));
        DeviceConnection connection(moduleDevice());
        QVERIFY(g_lastTransport != nullptr);
        QCOMPARE(g_lastTransport->deviceId, QStringLiteral("dev-1"));
        QCOMPARE(g_lastTransport->config.value("controller").toString(), QStringLiteral("gpc"));
        QCOMPARE(connection.transportType(), TransportType::Module);
        QVERIFY(connection.serialTransport() == nullptr);

        QSignalSpy connected(&connection, &DeviceConnection::connectionStateChanged);
        connection.connectToModule();
        QCOMPARE(g_lastTransport->opens, 1);
        QVERIFY(connection.isConnected());
        QVERIFY(connection.wantsConnection());
        QCOMPARE(connected.count(), 1);
        // A direct session: the backend's HELLO went straight out.
        QVERIFY(!g_lastTransport->written.isEmpty());

        connection.connectToModule();  // already up: nothing new
        QCOMPARE(g_lastTransport->opens, 1);
        connection.disconnectFrom();
        QVERIFY(!connection.isConnected());
        QVERIFY(!connection.wantsConnection());
    }

    void unregisteredTypeNeverComesUp() {
        DeviceConnection connection(moduleDevice());
        QVERIFY(g_lastTransport == nullptr);
        QSignalSpy errors(&connection, &DeviceConnection::errorOccurred);
        connection.connectToModule();
        QVERIFY(!connection.isConnected());
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains(QString::fromLatin1(kTypeId)));
    }

    void persistsTypeAndSettingsOnly() {
        Device device = moduleDevice();
        device.peerPassword = QStringLiteral("secret");
        device.cachePeerPassword = true;
        device.bleAddress = QStringLiteral("AA:BB:CC:DD:EE:FF");
        const QJsonObject json = traceview::deviceToJson(device);
        QCOMPARE(json.value("transportType").toInt(), 5);
        QCOMPARE(json.value("moduleType").toString(), QString::fromLatin1(kTypeId));
        for (const char* key : {"peerPassword", "cachePeerPassword", "bleAddress", "otaPassword",
                                "cacheOtaPassword", "portName", "tcpHost"}) {
            QVERIFY2(!json.contains(QLatin1String(key)), key);
        }

        bool ok = false;
        const Device loaded = traceview::deviceFromJson(json, &ok);
        QVERIFY(ok);
        QCOMPARE(loaded.transportType, TransportType::Module);
        QCOMPARE(loaded.moduleType, device.moduleType);
        QCOMPARE(loaded.moduleConfig, device.moduleConfig);
        QCOMPARE(loaded.name, device.name);
        QVERIFY(loaded.peerPassword.isEmpty());
        QVERIFY(traceview::deviceLinkConfigured(traceview::deviceLinkAt(loaded, 0)));
        Device unset = loaded;
        unset.moduleType.clear();
        QVERIFY(!traceview::deviceLinkConfigured(traceview::deviceLinkAt(unset, 0)));
    }
};

}  // namespace

QTEST_MAIN(TestModuleDeviceType)
#include "test_moduledevicetype.moc"
