#include "serialmonitorwidget.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "serialterminalwidget.h"
#include "terminaltabbar.h"

namespace traceview {

SerialMonitorWidget::SerialMonitorWidget(QWidget* parent) : DashboardWidget(parent) {
    m_tabBar = new TerminalTabBar(this);
    m_tabBar->hide();

    m_stack = new QStackedWidget(this);
    m_stack->setMinimumHeight(120);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabBar, 0);
    layout->addWidget(m_stack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) { showTab(index, true); });

    // One empty tab until the real config arrives -- keeps the widget in a
    // valid "single terminal, no tab strip" state if it's ever shown first.
    rebuildTabs({QString()});

    // DashboardCell resizes this widget with a bare setGeometry() (not through
    // a parent layout), so pin the floor to exactly what the layout needs;
    // below that the cell clips cleanly instead of mangling the terminal.
    setMinimumSize(layout->minimumSize());
}

void SerialMonitorWidget::setConfig(const QJsonObject& config) {
    QStringList deviceIds;
    if (config.value("tabs").isArray()) {
        const QJsonArray tabs = config.value("tabs").toArray();
        for (const QJsonValue& tab : tabs) {
            deviceIds.append(tab.toObject().value("deviceId").toString());
        }
    } else {
        // Pre-tabs config: a single tab from the old flat "deviceId", which
        // may be absent/empty (example.tvproj stores `config: {}`).
        deviceIds.append(config.value("deviceId").toString());
    }
    if (deviceIds.isEmpty()) {
        deviceIds.append(QString());
    }
    if (deviceIds == m_deviceIds) {
        return;
    }

    rebuildTabs(deviceIds);
    emit tabsChanged();
}

void SerialMonitorWidget::setEditModeHint(bool editMode) {
    m_editMode = editMode;
    for (SerialTerminalWidget* terminal : m_terminals) {
        terminal->setCursorBlinkEnabled(!editMode);
    }
}

void SerialMonitorWidget::setDeviceNames(const QHash<QString, QString>& namesById) {
    m_deviceNames = namesById;
    refreshTabLabels();
}

void SerialMonitorWidget::setDeviceConnectionStates(const QHash<QString, bool>& connectedById) {
    m_deviceConnected = connectedById;
    refreshTabConnectionDots();
}

void SerialMonitorWidget::feedDevice(const QString& deviceId, const QByteArray& data) {
    if (deviceId.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_terminals.size(); ++i) {
        if (m_deviceIds.value(i) == deviceId) {
            m_terminals[i]->appendData(data);
        }
    }
}

void SerialMonitorWidget::appendData(const QByteArray& data) {
    if (auto* terminal = qobject_cast<SerialTerminalWidget*>(m_stack->currentWidget())) {
        terminal->appendData(data);
    }
}

void SerialMonitorWidget::rebuildTabs(const QStringList& deviceIds) {
    for (SerialTerminalWidget* terminal : m_terminals) {
        delete terminal;  // also drops it from m_stack
    }
    m_terminals.clear();
    while (m_tabBar->count() > 0) {
        m_tabBar->removeTab(0);
    }

    m_deviceIds = deviceIds;

    for (int i = 0; i < deviceIds.size(); ++i) {
        auto* terminal = new SerialTerminalWidget(this);
        terminal->setMinimumHeight(120);
        terminal->setCursorBlinkEnabled(!m_editMode);

        connect(terminal, &SerialTerminalWidget::sendRequested, this,
                [this, terminal](const QByteArray& bytes) {
                    const int idx = m_terminals.indexOf(terminal);
                    if (idx >= 0) {
                        emit terminalInput(m_deviceIds.value(idx), bytes);
                    }
                });
        connect(terminal, &SerialTerminalWidget::previousTabRequested, this, [this] {
            if (!m_terminals.isEmpty()) {
                const int n = m_terminals.size();
                showTab((m_tabBar->currentIndex() - 1 + n) % n, true);
            }
        });
        connect(terminal, &SerialTerminalWidget::nextTabRequested, this, [this] {
            if (!m_terminals.isEmpty()) {
                const int n = m_terminals.size();
                showTab((m_tabBar->currentIndex() + 1) % n, true);
            }
        });

        m_terminals.append(terminal);
        m_stack->addWidget(terminal);
        m_tabBar->addTab(labelFor(deviceIds[i]));
    }

    m_tabBar->setVisible(m_terminals.size() >= 2);
    refreshTabConnectionDots();
    if (!m_terminals.isEmpty()) {
        showTab(0, false);
    }
}

void SerialMonitorWidget::showTab(int index, bool giveFocus) {
    if (index < 0 || index >= m_terminals.size()) {
        return;
    }
    if (m_tabBar->currentIndex() != index) {
        const QSignalBlocker blocker(m_tabBar);
        m_tabBar->setCurrentIndex(index);
    }
    m_stack->setCurrentIndex(index);
    if (giveFocus) {
        m_terminals[index]->setFocus();
    }
}

QString SerialMonitorWidget::labelFor(const QString& deviceId) const {
    if (deviceId.isEmpty()) {
        return tr("(no device)");
    }
    const QString name = m_deviceNames.value(deviceId);
    return name.isEmpty() ? tr("(unnamed device)") : name;
}

void SerialMonitorWidget::refreshTabLabels() {
    for (int i = 0; i < m_terminals.size(); ++i) {
        m_tabBar->setTabText(i, labelFor(m_deviceIds.value(i)));
    }
}

void SerialMonitorWidget::refreshTabConnectionDots() {
    for (int i = 0; i < m_terminals.size(); ++i) {
        m_tabBar->setTabConnected(i, m_deviceConnected.value(m_deviceIds.value(i), false));
    }
}

}  // namespace traceview
