#include "serialmonitorwidget.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "serialterminalwidget.h"
#include "terminaltabbar.h"

namespace traceview {

SerialMonitorWidget::SerialMonitorWidget(QWidget* parent) : DashboardWidget(parent) {
    m_tabBar = new TerminalTabBar(this);
    m_tabBar->hide();

    m_stack = new QStackedWidget(this);
    m_stack->setMinimumHeight(120);

    // Wipes the visible tab's scrollback. Right-aligned in the header row so it
    // stays reachable whether or not the tab strip is showing; flat until
    // hovered so it doesn't compete with the terminal for attention.
    m_clearButton = new QToolButton(this);
    m_clearButton->setText(tr("Clear"));
    m_clearButton->setToolTip(tr("Clear this terminal"));
    m_clearButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_clearButton->setAutoRaise(true);
    m_clearButton->setFocusPolicy(Qt::NoFocus);
    connect(m_clearButton, &QToolButton::clicked, this, [this] {
        if (auto* terminal = qobject_cast<SerialTerminalWidget*>(m_stack->currentWidget())) {
            terminal->clearTerminal();
        }
    });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(0);
    header->addWidget(m_tabBar, 0);
    header->addStretch(1);
    header->addWidget(m_clearButton, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header, 0);
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

}  // namespace traceview
