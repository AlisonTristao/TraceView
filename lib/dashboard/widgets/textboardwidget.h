#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include "dashboard/dashboardwidget.h"

namespace traceview {

struct TextBoardConfig {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    double sampleTimeMs = 3000.0;  // approximately 0.33 Hz
    QString initialText;
};

TextBoardConfig parseTextBoardConfig(const QJsonObject& json);

// A replace-in-place text surface for low-rate formatted telemetry. UTF-8
// samples replace the whole document, so a producer can send a table whose
// labels stay fixed and whose numeric fields appear to update in place.
// Painting normally uses a fixed-pitch font and derives its pixel size from
// both the longest line and the line count; resizing the dashboard cell
// therefore scales the complete report instead of wrapping or adding scroll
// bars. A rectangular run of binary rows typed as literal text (such as a
// camera frame headed by "FRAME" and its dimensions) is recognised
// automatically and painted as a monochrome raster instead: 1 is black and 0
// is white. In that mode the surrounding protocol/header lines are
// intentionally not shown. An OPAQUE_BYTES sample (onBinarySample()) bypasses
// text entirely: it is decoded straight into a grayscale raster (bpp=1 lands
// on pure black/white, same as the text form) -- see decodeMatrix() in the
// .cpp.
class TextBoardWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit TextBoardWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    const TextBoardConfig& config() const {
        return m_config;
    }

    QString text() const {
        return m_text;
    }
    void setText(const QString& text);
    void appendText(const QString& text);
    void clearText();

    // Exposed for the widget-level resize test and useful to visual harnesses.
    int fittedFontPixelSize() const;

    // The centred bounding box the document is painted into, at the fitted
    // font. Exposed for the placement test.
    QRectF textBlockRect() const;

public slots:
    void onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                      const QString& text);

    // Same (source, topic) filter as onTextSample(), for an OPAQUE_BYTES
    // topic instead of UTF8. `body` is unpacked as a camera.matrix-shaped
    // packed grid (see decodeMatrix() in the .cpp): body[0]=rows,
    // body[1]=cols, body[2]=bits_per_pixel (1, 2, 4 or 8), then
    // rows*ceil(cols*bits_per_pixel/8) bytes of MSB-first packed samples,
    // each row padded to its own byte (schema 1). Schema 2 adds encoding
    // at byte 3: 0=the same RAW layout, 1=RLE; see decodeMatrix().
    // Every sample is scaled up to an 8-bit
    // grayscale level before painting, so bits_per_pixel is purely a wire
    // efficiency knob (see esp32-cam_project's "cam set_depth"), not a
    // rendering mode. A body that doesn't fit that shape is ignored (the
    // board keeps showing whatever it last had) rather than guessed at.
    //
    // `fragmentCount` and `timestampUs` feed the info strip drawn above the
    // matrix (see paintEvent()): the fragment count is shown as-is, and
    // `timestampUs` (the producer's own monotonic clock, e.g. esp_timer_get_
    // time() on an ESP32) is compared against consecutive samples' *arrival*
    // time here to derive a jitter estimate -- NOT a capture-to-display
    // latency. Those two clocks share no known epoch (see model.md section
    // 6), so an absolute latency cannot be computed from them; a producer
    // reboot (visible here as timestampUs going backwards) resets the
    // baseline instead of producing a nonsensical jitter value.
    void onBinarySample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                        quint8 fragmentCount, const QByteArray& body, quint16 schemaVersion = 1);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // The document split for layout: lines by '\n', with a single trailing
    // newline dropped so a producer's terminating '\n' doesn't reserve a
    // phantom blank last line in the fit math.
    QStringList layoutLines() const;

    // Returns the largest rectangular run of binary rows in the document.
    // Empty means this sample is ordinary text and should use the text board
    // layout. Whitespace inside a binary row is ignored, allowing both
    // "0101" and "0 1 0 1" producers.
    QStringList binaryMatrixRows() const;

    // Installs a decoded OPAQUE_BYTES grid as the board's live content,
    // replacing any previous one (samples.size() must equal rows*cols,
    // row-major, each already scaled to an 8-bit grayscale level).
    void setGrayMatrix(int rows, int cols, QVector<quint8> samples);

    TextBoardConfig m_config;
    QString m_text;
    bool m_hasLiveText = false;

    // Set by setGrayMatrix()/onBinarySample() and cleared by anything that
    // makes m_text live again (setText/appendText/clearText, or rebinding
    // source/topic in setConfig) -- paintEvent() checks this before falling
    // back to the text/binaryMatrixRows() path.
    bool m_hasGrayMatrix = false;
    int m_grayRows = 0;
    int m_grayCols = 0;
    QVector<quint8> m_graySamples;

    // Info strip state (see onBinarySample()/paintEvent()): the fragment
    // count of the most recent OPAQUE_BYTES sample, and a jitter estimate
    // derived from comparing consecutive samples' producer timestamp deltas
    // against their arrival-time deltas here. Reset (m_hasJitterBaseline =
    // false) whenever the producer's timestamp goes backwards -- the only
    // locally-observable sign that it rebooted and restarted its monotonic
    // clock near zero.
    quint8 m_lastFragmentCount = 0;
    bool m_hasJitterBaseline = false;
    quint64 m_lastSampleTimestampUs = 0;
    qint64 m_lastSampleArrivalMs = 0;
    bool m_hasJitter = false;
    double m_lastJitterMs = 0.0;

    // fittedFontPixelSize() runs a binary search over font metrics; cache its
    // result so a repaint that changed neither the text nor the widget size
    // (theme change, partial expose) reuses it.
    mutable QString m_fittedForText;
    mutable QSize m_fittedForSize;
    mutable int m_fittedPixelSize = 0;
};

}  // namespace traceview
