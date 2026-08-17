#pragma once

#include <QWidget>

namespace traceview {

// Translucent highlight band PanelDockController shows over the edge of
// m_contentRow a dragged panel would snap to if released right now. Purely
// cosmetic -- transparent to mouse events so it never interferes with the
// drag it's illustrating.
class DockDropIndicator : public QWidget {
    Q_OBJECT

public:
    explicit DockDropIndicator(QWidget* parent);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace traceview
