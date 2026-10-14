// The line-follower track generator (lab/benchmarks/linefollower/plant)
// against line_follower_simulator's Python one: CPython's random bit for bit,
// then whole tracks point by point -- the fixtures in tests/data/linefollower
// (written by the Python generator for several types, seeds and parameters)
// and, when the Python simulator is checked out next to this repository,
// every runs/*_track.csv it saved.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QtTest/QtTest>
#include <cmath>

#include "lab/benchmarks/linefollower/plant/pyrandom.h"
#include "lab/benchmarks/linefollower/plant/trackgenerator.h"

using namespace tracelab::lf;

namespace {

// Tolerance of a track point (m): the spline is solved in a different (but
// equivalent) form than FITPACK's, so only rounding differs.
constexpr double kTolerance = 1e-9;

struct CsvTrack {
    QVector<int> index;
    QVector<double> x;
    QVector<double> y;
};

// "index,x,y" (fixtures) or "x,y" (run_logger.py), '#' comments skipped.
bool readTrackCsv(const QString& path, CsvTrack* out, QString* header) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    bool withIndex = false;
    bool first = true;
    int row = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QLatin1Char('#'))) {
            if (header) {
                header->append(line + QLatin1Char('\n'));
            }
            continue;
        }
        if (first) {
            withIndex = line.startsWith(QLatin1String("index"));
            first = false;
            continue;
        }
        const QStringList cells = line.split(QLatin1Char(','));
        if (withIndex) {
            out->index.append(cells.value(0).toInt());
            out->x.append(cells.value(1).toDouble());
            out->y.append(cells.value(2).toDouble());
        } else {
            out->index.append(row);
            out->x.append(cells.value(0).toDouble());
            out->y.append(cells.value(1).toDouble());
        }
        ++row;
    }
    return true;
}

double maxError(const Track& track, const CsvTrack& expected) {
    double error = 0.0;
    for (int i = 0; i < expected.index.size(); ++i) {
        const std::size_t k = std::size_t(expected.index[i]);
        if (k >= track.size()) {
            return qInf();
        }
        error = qMax(error, std::fabs(track.x[k] - expected.x[i]));
        error = qMax(error, std::fabs(track.y[k] - expected.y[i]));
    }
    return error;
}

}  // namespace

class TestLfTrackGenerator : public QObject {
    Q_OBJECT

private slots:
    void cpythonRandom() {
        // random.seed(42); random.random()
        PyRandom r(42);
        QCOMPARE(r.random(), 0.6394267984578837);
        QCOMPARE(r.random(), 0.025010755222666936);
        // random.seed(1112); random.randint(0, 2**32 - 1) -- the seed
        // generate_track() draws for the default track.
        PyRandom s(1112);
        QCOMPARE(s.randint(0, 4294967295LL), 1208752851LL);
        // random.seed(0); random.uniform(1, 2); random.getrandbits(40)
        PyRandom t(0);
        QCOMPARE(t.uniform(1.0, 2.0), 1.8444218515250481);
        QCOMPARE(t.getrandbits(40), std::uint64_t(978212965548ULL));
        // random.seed(2**40 + 5): a two-word key.
        PyRandom u((1ULL << 40) + 5);
        QCOMPARE(u.random(), 0.5043802970418443);
    }

    void fixtures_data() {
        QTest::addColumn<QString>("path");
        const QDir dir(QStringLiteral(TRACELAB_LF_TESTDATA));
        const QStringList files = dir.entryList({QStringLiteral("track_*.csv")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString& f : files) {
            QTest::newRow(qPrintable(f)) << dir.filePath(f);
        }
    }

    void fixtures() {
        QFETCH(QString, path);
        CsvTrack expected;
        QString header;
        QVERIFY(readTrackCsv(path, &expected, &header));
        const QRegularExpression re(QStringLiteral(
            "type=(\\d+) seed=(-?\\d+) checkpoints=(\\d+) resolution=(\\d+) track_rad=([0-9.]+) "
            "noise_level=([0-9.]+) points=(\\d+)"));
        const QRegularExpressionMatch m = re.match(header);
        QVERIFY2(m.hasMatch(), qPrintable(header));
        TrackParams params;
        params.type = m.captured(1).toInt() == 1 ? TrackType::Circle : TrackType::Lemniscate;
        params.seed = m.captured(2).toLongLong();
        params.checkpoints = m.captured(3).toInt();
        params.resolution = m.captured(4).toInt();
        params.trackRad = m.captured(5).toDouble();
        params.noiseLevel = m.captured(6).toDouble();
        const Track track = generateTrack(params);
        QCOMPARE(int(track.size()), m.captured(7).toInt());
        const double error = maxError(track, expected);
        qInfo("%s: %d points compared, max error %.3g m", qPrintable(QFileInfo(path).fileName()),
              int(expected.index.size()), error);
        QVERIFY2(error < kTolerance, qPrintable(QString::number(error)));
    }

    // Every track the Python simulator saved (runs/ and runs/compare/), with
    // the seed and type of its _meta.json and config.py's other defaults.
    void savedPythonRuns() {
        const QDir root(QStringLiteral(TRACELAB_LF_PYTHON_DIR));
        QStringList files;
        for (const QString& sub : {QStringLiteral("runs"), QStringLiteral("runs/compare")}) {
            const QDir dir(root.filePath(sub));
            for (const QString& f : dir.entryList({QStringLiteral("*_track.csv")}, QDir::Files)) {
                files.append(dir.filePath(f));
            }
        }
        if (files.isEmpty()) {
            QSKIP("line_follower_simulator/runs not found (set TRACELAB_LF_PYTHON_DIR)");
        }
        for (const QString& path : files) {
            QString metaPath = path;
            metaPath.replace(QLatin1String("_track.csv"), QLatin1String("_meta.json"));
            QFile metaFile(metaPath);
            QVERIFY2(metaFile.open(QIODevice::ReadOnly), qPrintable(metaPath));
            const QJsonObject meta = QJsonDocument::fromJson(metaFile.readAll()).object();
            TrackParams params;
            QJsonObject config = meta.value(QLatin1String("config")).toObject();
            QJsonObject trackConfig = config.value(QLatin1String("track")).toObject();
            if (!trackConfig.isEmpty()) {  // newer runs: the whole config
                params.seed = trackConfig.value(QLatin1String("seed")).toInteger(1112);
                params.type = trackConfig.value(QLatin1String("track_type")).toInt() == 1
                                  ? TrackType::Circle
                                  : TrackType::Lemniscate;
                params.checkpoints = trackConfig.value(QLatin1String("checkpoints")).toInt(36);
                params.resolution = trackConfig.value(QLatin1String("resolution")).toInt(500);
                params.trackRad = trackConfig.value(QLatin1String("track_rad")).toDouble(30);
                params.noiseLevel = trackConfig.value(QLatin1String("noise_level")).toDouble(0.225);
            } else {
                params.seed = meta.value(QLatin1String("track_seed")).toInteger(1112);
                params.type = meta.value(QLatin1String("track_type")).toInt() == 1
                                  ? TrackType::Circle
                                  : TrackType::Lemniscate;
            }
            CsvTrack expected;
            QVERIFY(readTrackCsv(path, &expected, nullptr));
            const Track track = generateTrack(params);
            QCOMPARE(int(track.size()), int(expected.x.size()));
            const double error = maxError(track, expected);
            qInfo("%s (seed %lld, type %d): %d points, max error %.3g m",
                  qPrintable(root.relativeFilePath(path)), (long long)params.seed,
                  int(params.type), int(expected.x.size()), error);
            QVERIFY2(error < kTolerance, qPrintable(path));
        }
    }

    void randomSeedDiffers() {
        TrackParams params;
        params.seed = -1;
        params.checkpoints = 8;
        params.resolution = 10;
        const Track a = generateTrack(params);
        const Track b = generateTrack(params);
        QCOMPARE(int(a.size()), 80);
        QVERIFY(a.x != b.x || a.innerSeed != b.innerSeed);
    }

    void periodicSplineInterpolates() {
        // A closed curve: the spline passes through the distinct points and
        // ends where it starts.
        std::vector<Point2> points;
        for (int i = 0; i < 8; ++i) {
            const double t = 2 * M_PI * i / 8;
            points.push_back({std::cos(t) * 3, std::sin(t) * 2});
        }
        std::vector<double> x;
        std::vector<double> y;
        periodicSpline(points, 101, &x, &y);
        QVERIFY(std::fabs(x.front() - x.back()) < 1e-12);
        QVERIFY(std::fabs(y.front() - y.back()) < 1e-12);
        QVERIFY(std::fabs(x.front() - 3.0) < 1e-12);
    }
};

QTEST_GUILESS_MAIN(TestLfTrackGenerator)
#include "test_lftrackgenerator.moc"
