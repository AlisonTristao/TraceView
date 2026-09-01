#pragma once

#include <QWidgetAction>

#include "traceview/font.h"

namespace traceview {

// Custom QWidgetAction that renders the font name using the font itself.
// For example, "Consolas" is rendered in the Consolas font.
class FontMenuAction final : public QWidgetAction {
    Q_OBJECT

public:
    explicit FontMenuAction(const FontOption& font, QObject* parent = nullptr);

    const FontOption& font() const { return m_font; }

protected:
    QWidget* createWidget(QWidget* parent) override;

private:
    FontOption m_font;
};

}  // namespace traceview
