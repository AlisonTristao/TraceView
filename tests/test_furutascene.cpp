// The Furuta pendulum scene (dashboard/scenes/furutascene.h): its only inputs,
// theta1 and theta2, bound by field name; the angles and trail taken from the
// records; the mass drawn where the angles put it (upright above the pivot,
// hanging below it, moving with theta1); the machine's body centred in the
// view; no key actions; and a full frame painted over the sky and the floor.
// With TRACELAB_FURUTA_SCENE_PNG set, the frame is also saved there to look
// at.

#include <QImage>
#include <QPainter>
#include <QtTest>
#include <cmath>

#include "dashboard/scenes/furutascene.h"

using namespace traceview;

namespace {

SceneRecord angles(double theta1, double theta2) {
    SceneRecord record;
    record.values.insert(QStringLiteral("theta1"), theta1);
    record.values.insert(QStringLiteral("theta2"), theta2);
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
    void onlyTheTwoAngles() {
        FurutaScene scene;
        const QList<SceneInput> inputs = scene.inputs();
        QCOMPARE(inputs.size(), 2);
        for (int i = 0; i < 2; ++i) {
            const QString name = i == 0 ? QStringLiteral("theta1") : QStringLiteral("theta2");
            QCOMPARE(inputs[i].name, name);
            QCOMPARE(inputs[i].unit, QString("rad"));
            QVERIFY(inputs[i].kind == SceneInput::Kind::Scalar);
            QVERIFY(!inputs[i].optional);
            // No topic of the simulator's: bound by the field's name.
            QVERIFY(inputs[i].defaultTopic.isEmpty());
            QCOMPARE(inputs[i].defaultField, name);
        }
        QCOMPARE(scene.param("arm_length_m"), 0.085);
        QVERIFY(std::isnan(scene.param("torque_max")));
        QVERIFY(std::isnan(scene.param("prediction")));
    }

    void anglesAndTrail() {
        FurutaScene scene;
        QVERIFY(!scene.hasAngles());
        SceneFrame frame;
        for (int k = 1; k <= 100; ++k) {
            frame.records.append(angles(0.01 * k, M_PI - 0.01 * k));
        }
        scene.update(frame);
        QVERIFY(scene.hasAngles());
        QCOMPARE(scene.theta1(), 1.0);
        QCOMPARE(scene.theta2(), M_PI - 1.0);
        QCOMPARE(scene.trailSize(), int(FurutaScene::kTrailLength));
        // A record without both angles is skipped.
        SceneFrame partial;
        SceneRecord only;
        only.values.insert(QStringLiteral("theta1"), 3.0);
        partial.records.append(only);
        scene.update(partial);
        QCOMPARE(scene.theta1(), 1.0);
    }

    void massWhereTheAnglesPutIt() {
        FurutaScene scene;
        const auto massAt = [&](double theta1, double theta2) {
            SceneFrame frame;
            frame.records.append(angles(theta1, theta2));
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

    void machineCentredInTheView() {
        // Upright and hanging, the pendulum's pivot (on the arm, beside the
        // body) stays near the middle: the body is centred, not the room the
        // pendulum sweeps.
        FurutaScene scene;
        for (const double theta2 : {0.0, M_PI}) {
            SceneFrame frame;
            frame.records.append(angles(0.0, theta2));
            scene.update(frame);
            const QPointF pivot = scene.pivotOnScreen(view());
            QVERIFY2(std::fabs(pivot.y() - 0.5 * view().size.height()) <
                         0.15 * view().size.height(),
                     qPrintable(QString::number(pivot.y())));
            // And the mass stays inside the view.
            const QPointF mass = scene.massOnScreen(view());
            QVERIFY(QRectF(QPointF(0, 0), view().size).contains(mass));
        }
    }

    void noKeyActions() {
        FurutaScene scene;
        for (const int key : {int(Qt::Key_P), int(Qt::Key_O), int(Qt::Key_R), int(Qt::Key_A)}) {
            QVERIFY(!scene.keyAction(key));
        }
    }

    void paintsAFrame() {
        FurutaScene scene;
        SceneFrame frame;
        scene.setParam(QStringLiteral("trail"), 1.0);
        for (int k = 1; k <= 30; ++k) {
            frame.records.append(angles(0.6 + 0.01 * k, 0.35 - 0.005 * k));
        }
        scene.update(frame);

        QImage image(view().size.toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            scene.paint(painter, view());
        }
        // An opaque, light sky darkening toward the floor, black ink somewhere
        // in the middle (the mechanism).
        const QColor top = image.pixelColor(2, 2);
        const QColor bottom = image.pixelColor(image.width() - 3, image.height() - 3);
        QCOMPARE(top.alpha(), 255);
        QVERIFY(top.lightness() > 240);
        QVERIFY(bottom.lightness() < top.lightness());
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
                SceneFrame f;
                f.records.append(angles(pose.theta1, pose.theta2));
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
            f.records.append(angles(0.35, 0.35));
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
                g.records.append(angles(k * M_PI / 4, 0.4));
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
            const QPoint base(int(big.size.width() / 2), int(big.size.height() * 0.5));
            QVERIFY(large.copy(QRect(base - QPoint(450, 450), QSize(900, 900)))
                        .save(QString(png).replace(QLatin1String(".png"), QLatin1String("_base.png"))));
        }
    }
};

QTEST_MAIN(TestFurutaScene)
#include "test_furutascene.moc"
