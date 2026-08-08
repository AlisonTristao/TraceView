#include "buttonpanelwidget.h"

#include <QGridLayout>
#include <QPushButton>

namespace traceview {

namespace {
constexpr int kRows = 2;
constexpr int kColumns = 3;
} // namespace

ButtonPanelWidget::ButtonPanelWidget(QWidget* parent) : DashboardWidget(parent) {
    auto* layout = new QGridLayout(this);
    int index = 1;
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col, ++index) {
            layout->addWidget(new QPushButton(QString("Button %1").arg(index), this), row, col);
        }
    }
}

} // namespace traceview
