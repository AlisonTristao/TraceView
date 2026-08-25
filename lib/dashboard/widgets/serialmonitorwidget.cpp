#include "serialmonitorwidget.h"

#include <QVBoxLayout>

#include "serialterminalwidget.h"

namespace traceview {

SerialMonitorWidget::SerialMonitorWidget(QWidget* parent) : DashboardWidget(parent) {
    m_terminal = new SerialTerminalWidget(this);
    m_terminal->setMinimumHeight(120);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_terminal, 1);

    // DashboardCell resizes this widget with a direct setGeometry() call
    // (not through a parent layout), so nothing stops it from being
    // squeezed below what the terminal actually needs. QWidget does
    // enforce setMinimumSize() on setGeometry() though, so pin the floor to
    // exactly what this layout requires; below that the widget now just
    // gets cleanly clipped by the cell instead of mangled.
    setMinimumSize(layout->minimumSize());

    connect(m_terminal, &SerialTerminalWidget::sendRequested, this,
            &SerialMonitorWidget::sendRequested);
}

void SerialMonitorWidget::appendData(const QByteArray& data) {
    m_terminal->appendData(data);
}

}  // namespace traceview
