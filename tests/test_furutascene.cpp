// The Furuta pendulum scene (dashboard/scenes/furutascene.h): its inputs bound by
// name to the plant's sim.truth theta1/theta2 and the controller's ctl.diag,
// the angles and trail taken from the records (a tick going back clears the
// trail), the mass drawn where the angles put it (upright above the pivot,
// hanging below it, moving with theta1), the P / O / R keys asking for
// sim.disturb, and a full frame painted. With TRACELAB_FURUTA_SCENE_PNG set,
// the frame is also saved there to look at.

#include <QImage>
#include <QPainter>
#include <QtTest>
#include <cmath>

#include "dashboard/scenes/furutascene.h"

using namespace traceview;

namespace {

SceneRecord angles(double theta1, double theta2, qint64 tick) {
    SceneRecord record;
    record.values.insert(QStringLiteral("theta1"), theta1);
    record.values.insert(QStringLiteral("theta2"), theta2);
    record.values.insert(QStringLiteral("tick"), double(tick));
    return record;
}

SceneView view() {
    SceneView v;
    v.size = QSizeF(960, 720);
    return v;
}

}  // namespace

class TestFurutaScene : public QObject {
    Q_OBJECT

private slots:
    void inputsBindToThePlantAndController() {
        FurutaScene scene;
        QHash<QString, SceneInput> byName;
        for (const SceneInput& input : scene.inputs()) {
            QVERIFY2(!byName.contains(input.name), qPrintable(input.name));
            byName.insert(input.name, input);
        }
        QCOMPARE(byName.value("theta1").defaultTopic, QString("sim.truth"));
        QCOMPARE(byName.value("theta2").defaultField, QString("theta2"));
        QVERIFY(!byName.value("theta1").optional);
        QCOMPARE(byName.value("u").defaultTopic, QString("loop.output"));
        QCOMPARE(byName.value("pred_theta1").defaultField, QString("pred_x"));
        QCOMPARE(byName.value("theta1_ref").defaultField, QString("theta1_r"));
        QVERIFY(byName.value("controller").kind == SceneInput::Kind::Text);
        QCOMPARE(scene.param("arm_length_m"), 0.085);
    }

    void anglesAndTrail() {
        FurutaScene scene;
        QVERIFY(!scene.hasAngles());
        SceneFrame frame;
        for (int k = 1; k <= 100; ++k) {
            frame.records.append(angles(0.01 * k, M_PI - 0.01 * k, k));
        }
        scene.update(frame);
        QVERIFY(scene.hasAngles());
        QCOMPARE(scene.theta1(), 1.0);
        QCOMPARE(scene.trailSize(), int(FurutaScene::kTrailLength));
        // sim.reset: the tick goes back, the trail starts over.
        SceneFrame reset;
        reset.records.append(angles(0.0, M_PI, 0));
        scene.update(reset);
        QCOMPARE(scene.trailSize(), 1);
    }

    void massWhereTheAnglesPutIt() {
        FurutaScene scene;
        const auto massAt = [&](double theta1, double theta2) {
            SceneFrame frame;
            frame.records.append(angles(theta1, theta2, 1));
            scene.update(frame);
            return scene.massOnScreen(view());
        };
        const QPointF up = massAt(0.0, 0.0);
        const QPointF down = massAt(0.0, M_PI);
        // Upright is above hanging (screen y grows down) by about 2 l2.
        QVERIFY(up.y() < down.y() - 100);
        QVERIFY(std::fabs(up.x() - down.x()) < 1.0);
        // Tilting the pendulum or turning the arm moves the mass.
        QVERIFY((massAt(0.0, 0.5) - up).manhattanLength() > 20);
        QVERIFY((massAt(1.0, 0.0) - up).manhattanLength() > 20);
    }

    void keysAskForADisturbance() {
        FurutaScene scene;
        const std::optional<SceneAction> p = scene.keyAction(Qt::Key_P);
        QVERIFY(p);
        QCOMPARE(p->action, QString("sim.disturb"));
        QCOMPARE(p->argument, 20.0);
        QCOMPARE(scene.keyAction(Qt::Key_O)->argument, 0.0);
        QCOMPARE(scene.keyAction(Qt::Key_O)->argument, -20.0);
        QCOMPARE(scene.keyAction(Qt::Key_R)->argument, 0.0);
        QVERIFY(!scene.keyAction(Qt::Key_A));
    }

    void paintsAFrame() {
        FurutaScene scene;
        SceneFrame frame;
        for (int k = 1; k <= 30; ++k) {
            frame.records.append(angles(0.6 + 0.01 * k, 0.35 - 0.005 * k, k));
        }
        frame.values.insert(QStringLiteral("t"), 12.34);
        frame.values.insert(QStringLiteral("u"), 0.31);
        frame.values.insert(QStringLiteral("theta1_ref"), M_PI);
        frame.values.insert(QStringLiteral("solve_ms"), 0.23);
        frame.values.insert(QStringLiteral("qps"), 1);
        QVector<double> p1, p2;
        for (int j = 0; j < 40; ++j) {
            p1.append(0.9 + 0.02 * j);
            p2.append(0.2 * std::exp(-0.1 * j));
        }
        frame.arrays.insert(QStringLiteral("pred_theta1"), p1);
        frame.arrays.insert(QStringLiteral("pred_theta2"), p2);
        scene.update(frame);
        scene.document(QStringLiteral("controller"), "qLPV NMPC (Furuta)");

        QImage image(view().size.toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            scene.paint(painter, view());
        }
        // White paper, black ink somewhere in the middle (the mechanism).
        QCOMPARE(image.pixelColor(2, 2), QColor(Qt::white));
        int ink = 0;
        for (int y = 100; y < 650; y += 3) {
            for (int x = 200; x < 760; x += 3) {
                ink += image.pixelColor(x, y).lightness() < 80 ? 1 : 0;
            }
        }
        QVERIFY2(ink > 200, qPrintable(QString::number(ink)));
        const QString png = qEnvironmentVariable("TRACELAB_FURUTA_SCENE_PNG");
        if (!png.isEmpty()) {
            QVERIFY(image.save(png));
            // More poses, for the eye: hanging at rest; the arm behind the
            // motor; the pendulum leaning toward the camera; the figure's
            // annotations on.
            const struct {
                const char* name;
                double theta1, theta2, labels;
            } poses[] = {{"rest", 0.0, M_PI, 0},
                         {"behind", 2.4, 0.4, 0},
                         {"toward", -0.8, -0.9, 0},
                         {"labels", 0.9, 0.35, 1}};
            for (const auto& pose : poses) {
                FurutaScene other;
                other.setParam(QStringLiteral("labels"), pose.labels);
                other.setParam(QStringLiteral("torque_arrow"), pose.labels);
                SceneFrame f;
                f.records.append(angles(pose.theta1, pose.theta2, 1));
                f.values.insert(QStringLiteral("u"), 0.3);
                other.update(f);
                QImage shot(view().size.toSize(), QImage::Format_ARGB32_Premultiplied);
                QPainter painter(&shot);
                other.paint(painter, view());
                painter.end();
                QVERIFY(shot.save(QString(png).replace(QLatin1String(".png"),
                                                       QStringLiteral("_%1.png").arg(pose.name))));
            }
            // Close-ups, four times larger: the joint and the base's top.
            FurutaScene close;
            SceneFrame f;
            f.records.append(angles(0.35, 0.35, 1));
            close.update(f);
            SceneView big = view();
            big.size *= 4.0;
            QImage large(big.size.toSize(), QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&large);
            close.paint(painter, big);
            painter.end();
            const QPoint pivot = close.pivotOnScreen(big).toPoint();
            QVERIFY(large.copy(QRect(pivot - QPoint(300, 300), QSize(600, 600)))
                        .save(QString(png).replace(QLatin1String(".png"), QLatin1String("_joint.png"))));
            // The joint while the arm turns a full lap (theta2 = 0.4): eight
            // close-ups side by side.
            QImage sweep(4 * 300, 2 * 300, QImage::Format_ARGB32_Premultiplied);
            sweep.fill(Qt::white);
            QPainter sheet(&sweep);
            for (int k = 0; k < 8; ++k) {
                FurutaScene turning;
                SceneFrame g;
                g.records.append(angles(k * M_PI / 4, 0.4, 1));
                turning.update(g);
                QImage frameImage(big.size.toSize(), QImage::Format_ARGB32_Premultiplied);
                QPainter p(&frameImage);
                turning.paint(p, big);
                p.end();
                const QPoint at = turning.pivotOnScreen(big).toPoint();
                sheet.drawImage(QPoint((k % 4) * 300, (k / 4) * 300),
                                frameImage.copy(QRect(at - QPoint(150, 150), QSize(300, 300))));
            }
            sheet.end();
            QVERIFY(sweep.save(QString(png).replace(QLatin1String(".png"), QLatin1String("_sweep.png"))));
            const QPoint base(int(big.size.width() / 2), int(big.size.height() * 0.62));
            QVERIFY(large.copy(QRect(base - QPoint(450, 450), QSize(900, 900)))
                        .save(QString(png).replace(QLatin1String(".png"), QLatin1String("_base.png"))));
        }
    }
};

QTEST_MAIN(TestFurutaScene)
#include "test_furutascene.moc"
