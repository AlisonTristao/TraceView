#include <QtTest>
#include <memory>

#include "dashboard/scenes/diffdrivescene.h"
#include "dashboard/scenes/furutascene.h"
#include "dashboard/scenes/sceneregistry.h"

using namespace traceview;

namespace {

// The smallest scene there is, to tell instances apart.
class EmptyScene final : public SceneRenderer {
public:
    explicit EmptyScene(int tag) : m_tag(tag) {}

    QList<SceneInput> inputs() const override {
        return {};
    }
    QList<SceneParam> params() const override {
        return {};
    }
    void setParam(const QString&, const QVariant&) override {}
    void update(const SceneFrame&) override {}
    void paint(QPainter&, const SceneView&) override {}

    int tag() const {
        return m_tag;
    }

private:
    int m_tag;
};

}  // namespace

class TestSceneRegistry : public QObject {
    Q_OBJECT

private slots:
    void registersAndCreatesInOrder();
    void ignoresDuplicatesAndInvalidTypes();
    void builtinScenesIncludeTheLineFollower();
};

void TestSceneRegistry::registersAndCreatesInOrder() {
    SceneRegistry registry;
    registry.registerType({"b", "Bee", [] { return std::make_unique<EmptyScene>(2); }});
    registry.registerType({"a", "Ay", [] { return std::make_unique<EmptyScene>(1); }});

    QCOMPARE(registry.types().size(), 2);
    QCOMPARE(registry.types().at(0).typeId, QString("b"));
    QCOMPARE(registry.types().at(1).typeId, QString("a"));
    QVERIFY(registry.contains("a"));
    QCOMPARE(registry.displayName("a"), QString("Ay"));

    const std::unique_ptr<SceneRenderer> scene = registry.create("a");
    QVERIFY(scene);
    QCOMPARE(static_cast<EmptyScene*>(scene.get())->tag(), 1);

    QVERIFY(!registry.contains("missing"));
    QVERIFY(registry.displayName("missing").isEmpty());
    QVERIFY(!registry.create("missing"));
}

void TestSceneRegistry::ignoresDuplicatesAndInvalidTypes() {
    SceneRegistry registry;
    registry.registerType({"a", "First", [] { return std::make_unique<EmptyScene>(1); }});
    registry.registerType({"a", "Second", [] { return std::make_unique<EmptyScene>(2); }});
    registry.registerType({"", "No id", [] { return std::make_unique<EmptyScene>(3); }});
    registry.registerType({"c", "No factory", nullptr});

    QCOMPARE(registry.types().size(), 1);
    QCOMPARE(registry.displayName("a"), QString("First"));
    QCOMPARE(static_cast<EmptyScene*>(registry.create("a").get())->tag(), 1);
}

void TestSceneRegistry::builtinScenesIncludeTheLineFollower() {
    SceneRegistry registry;
    registerBuiltinScenes(registry);
    registerBuiltinScenes(registry);  // idempotent

    QCOMPARE(registry.types().size(), 2);
    QVERIFY(registry.create(FurutaScene::kTypeId));
    const std::unique_ptr<SceneRenderer> scene = registry.create(DiffDriveScene::kTypeId);
    QVERIFY(scene);

    QStringList names;
    for (const SceneInput& input : scene->inputs()) {
        names.append(input.name);
        if (input.name == QLatin1String("x")) {
            QCOMPARE(input.defaultTopic, QString("sim.truth"));
            QCOMPARE(input.defaultField, QString("x"));
            QVERIFY(!input.optional);
        }
        if (input.name == QLatin1String("track")) {
            QVERIFY(input.kind == SceneInput::Kind::Text);
            QCOMPARE(input.defaultTopic, QString("sim.track"));
        }
    }
    for (const char* expected : {"x", "y", "theta", "tick", "next_point", "track", "preview_x",
                                 "preview_y", "pred_x", "pred_y"}) {
        QVERIFY2(names.contains(QLatin1String(expected)), expected);
    }
    QCOMPARE(names.removeDuplicates(), 0);
}

QTEST_GUILESS_MAIN(TestSceneRegistry)
#include "test_sceneregistry.moc"
