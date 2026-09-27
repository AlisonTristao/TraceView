#include <QJsonObject>
#include <QtTest>
#include <cmath>

#include "dashboard/widgets/audiospectrumwidget.h"
#include "dashboard/widgets/audiostream.h"

using traceview::AudioSpectrumWidget;
using traceview::AudioStreamBuffer;
using traceview::SpectrumAnalyzer;
using traceview::TelemetryFieldBinding;

namespace {

constexpr double kPi = 3.14159265358979323846;

QVector<float> sine(double hz, double rate, int count, double amplitude, int offset = 0) {
    QVector<float> out(count);
    for (int i = 0; i < count; ++i) {
        out[i] = float(amplitude * std::sin(2.0 * kPi * hz * (offset + i) / rate));
    }
    return out;
}

// Frequency of `samples` from its rising zero crossings.
double measuredFrequency(const QVector<float>& samples, double rate) {
    int first = -1;
    int last = -1;
    int crossings = 0;
    for (int i = 1; i < samples.size(); ++i) {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
            if (first < 0) {
                first = i;
            } else {
                ++crossings;
            }
            last = i;
        }
    }
    return crossings > 0 ? crossings * rate / (last - first) : 0.0;
}

class TestAudioStream : public QObject {
    Q_OBJECT

private slots:
    void spectrumPeaksAtTheToneInDbfs();
    void spectrumSizeIsAPowerOfTwoInRange();
    void bufferWaitsForItsTargetThenPlays();
    void bufferResamplesToTheOutputRate();
    void bufferCountsUnderrunsAndCatchesUp();
    void widgetDecodesAStreamAndCountsLostBlocks();
};

void TestAudioStream::spectrumPeaksAtTheToneInDbfs() {
    SpectrumAnalyzer analyzer(4096);
    const double rate = 48000.0;
    const int bin = 256;  // exactly on a bin: 3000 Hz
    const QVector<float> tone = sine(bin * rate / 4096, rate, 4096, 0.5);
    QVector<float> db(analyzer.binCount());
    analyzer.compute(tone.constData(), db.data());

    const int loudest = int(std::max_element(db.cbegin(), db.cend()) - db.cbegin());
    QCOMPARE(loudest, bin);
    QVERIFY(std::fabs(db[bin] - (-6.02f)) < 0.2f);  // half scale
    QVERIFY(db[bin + 10] < db[bin] - 60.0f);        // Hann sidelobes fall fast
}

void TestAudioStream::spectrumSizeIsAPowerOfTwoInRange() {
    SpectrumAnalyzer analyzer;
    analyzer.setSize(5000);
    QCOMPARE(analyzer.size(), 4096);
    analyzer.setSize(10);
    QCOMPARE(analyzer.size(), SpectrumAnalyzer::kMinSize);
    analyzer.setSize(1 << 20);
    QCOMPARE(analyzer.size(), SpectrumAnalyzer::kMaxSize);
    QCOMPARE(analyzer.binCount(), SpectrumAnalyzer::kMaxSize / 2 + 1);
}

void TestAudioStream::bufferWaitsForItsTargetThenPlays() {
    AudioStreamBuffer buffer;
    buffer.setOutputRate(48000);
    buffer.setLatency(80, 400);
    QVector<float> out(480);

    // 40 ms buffered, target 80: still silent.
    const QVector<float> tone = sine(1000, 48000, 1920, 0.5);
    buffer.push(tone.constData(), tone.size(), 48000);
    buffer.pull(out.data(), out.size());
    QVERIFY(std::all_of(out.cbegin(), out.cend(), [](float v) { return v == 0.0f; }));

    // Past the target: the tone comes out.
    const QVector<float> more = sine(1000, 48000, 2880, 0.5, 1920);
    buffer.push(more.constData(), more.size(), 48000);
    buffer.pull(out.data(), out.size());
    const float loudest = *std::max_element(out.cbegin(), out.cend());
    QVERIFY(loudest > 0.45f && loudest <= 0.51f);
    QCOMPARE(buffer.stats().underruns, quint64(0));
}

void TestAudioStream::bufferResamplesToTheOutputRate() {
    AudioStreamBuffer buffer;
    buffer.setOutputRate(48000);
    const double inputRate = 83333.0;
    int produced = 0;
    QVector<float> collected;
    // Blocks of 1024 at 83.3 kHz, pulled in 10 ms chunks at 48 kHz, in step
    // (one block is ~12.3 ms): a steady stream, like the real one.
    QVector<float> chunk(480);
    double inputTimeMs = 0.0;
    for (double t = 0.0; t < 2000.0; t += 10.0) {
        while (inputTimeMs <= t + 100.0) {
            const QVector<float> block = sine(1000, inputRate, 1024, 0.5, produced);
            buffer.push(block.constData(), block.size(), inputRate);
            produced += 1024;
            inputTimeMs += 1024 * 1000.0 / inputRate;
        }
        buffer.pull(chunk.data(), chunk.size());
        if (t > 500.0) {
            collected += chunk;
        }
    }
    const double hz = measuredFrequency(collected, 48000.0);
    QVERIFY2(std::fabs(hz - 1000.0) < 10.0, qPrintable(QString::number(hz)));
    QCOMPARE(buffer.stats().underruns, quint64(0));
    QVERIFY(std::fabs(buffer.stats().drift - 1.0) <= AudioStreamBuffer::kMaxDrift + 1e-9);
}

void TestAudioStream::bufferCountsUnderrunsAndCatchesUp() {
    AudioStreamBuffer buffer;
    buffer.setOutputRate(48000);
    buffer.setLatency(80, 400);
    const QVector<float> block = sine(500, 48000, 4800, 0.3);  // 100 ms
    buffer.push(block.constData(), block.size(), 48000);
    QVector<float> out(9600);  // 200 ms: runs dry
    buffer.pull(out.data(), out.size());
    QCOMPARE(buffer.stats().underruns, quint64(1));

    // A second's burst is cut back to the target.
    for (int i = 0; i < 10; ++i) {
        buffer.push(block.constData(), block.size(), 48000);
    }
    QVERIFY(buffer.stats().overflows >= 1);
    QVERIFY(buffer.stats().bufferedMs <= 400.0);
}

void TestAudioStream::widgetDecodesAStreamAndCountsLostBlocks() {
    AudioSpectrumWidget widget;
    QJsonObject config;
    config["sourceId"] = QStringLiteral("0x11223344");
    config["topicId"] = QStringLiteral("0x0101");
    config["fullScale"] = 2048.0;
    config["view"] = QJsonObject{{"play", false}, {"fft", "1024"}};  // no sound card in tests
    widget.setConfig(config);

    TelemetryFieldBinding binding;
    binding.sourceId = 0x11223344;
    binding.topicId = 0x0101;
    const double rate = 32000.0;
    auto block = [&](quint32 seq, int offset) {
        binding.fieldId = 1;
        widget.onFieldSample(binding, 0, seq);
        binding.fieldId = 2;
        widget.onFieldSample(binding, 0, rate);
        binding.fieldId = 3;
        QVector<float> counts = sine(2000, rate, 1024, 1024.0, offset);  // half scale
        widget.onArraySample(binding, 0, counts);
    };
    block(10, 0);
    block(11, 1024);
    block(14, 2048);  // 12 and 13 lost
    QCOMPARE(widget.receivedBlocks(), quint64(3));
    QCOMPARE(widget.lostBlocks(), quint64(2));
    QCOMPARE(widget.sampleRate(), rate);

    widget.refreshSpectrum();
    const QVector<float>& db = widget.spectrumDb();
    QCOMPARE(db.size(), 513);
    const int loudest = int(std::max_element(db.cbegin(), db.cend()) - db.cbegin());
    QCOMPARE(loudest, 64);  // 2000 Hz at 32000 / 1024 per bin
    QVERIFY(db[loudest] > -8.0f && db[loudest] < -5.0f);

    // Another topic's blocks are not this widget's.
    binding.topicId = 0x0102;
    widget.onArraySample(binding, 0, QVector<float>(1024, 0.0f));
    QCOMPARE(widget.receivedBlocks(), quint64(3));
}

}  // namespace

QTEST_MAIN(TestAudioStream)
#include "test_audiostream.moc"
