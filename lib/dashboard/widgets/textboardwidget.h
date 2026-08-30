#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

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
// Painting uses a fixed-pitch font and derives its pixel size from both the
// longest line and the line count; resizing the dashboard cell therefore
// scales the complete report instead of wrapping or adding scroll bars. The
// document is drawn as one block centred in the cell, with lines left-aligned
// within the block so the columns stay aligned.
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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // The document split for layout: lines by '\n', with a single trailing
    // newline dropped so a producer's terminating '\n' doesn't reserve a
    // phantom blank last line in the fit math.
    QStringList layoutLines() const;

    TextBoardConfig m_config;
    QString m_text;
    bool m_hasLiveText = false;

    // fittedFontPixelSize() runs a binary search over font metrics; cache its
    // result so a repaint that changed neither the text nor the widget size
    // (theme change, partial expose) reuses it.
    mutable QString m_fittedForText;
    mutable QSize m_fittedForSize;
    mutable int m_fittedPixelSize = 0;
};

}  // namespace traceview
