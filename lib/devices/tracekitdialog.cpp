#include "devices/tracekitdialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

#include "theme/dialogpresenter.h"
#include "theme/iconlibrary.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
constexpr int kNameColumn = 0;
constexpr int kValueColumn = 1;
constexpr int kDefaultColumn = 2;
constexpr int kResetColumn = 3;
constexpr int kColumnCount = 4;

// The device's answer may carry a prompt or blank lines around the JSON.
QJsonObject parseObject(const QString& output) {
    const int start = output.indexOf(QLatin1Char('{'));
    const int end = output.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start) return QJsonObject();
    return QJsonDocument::fromJson(output.mid(start, end - start + 1).toUtf8()).object();
}

bool isIntegerType(const QString& type) {
    return type == QLatin1String("u8") || type == QLatin1String("u16") ||
           type == QLatin1String("u32") || type == QLatin1String("i32");
}

QString valueText(const QJsonValue& value) {
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 9);
    return value.toString();
}
}  // namespace

bool isTraceKitDevice(const QVector<DeviceInfoRecord>& info) {
    for (const DeviceInfoRecord& entry : info) {
        if (entry.key == QLatin1String("tracekit")) return true;
    }
    return false;
}

TraceKitDialog::TraceKitDialog(const Device& device, Runner runner, QWidget* parent)
    : QDialog(parent), m_device(device), m_runner(std::move(runner)) {
    setWindowTitle(tr("%1 — TraceKit").arg(device.name));
    resize(880, 720);

    // Header: name, what it runs, and whether it is connected.
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("tracekitHeader"));
    m_title = new QLabel(header);
    m_title->setObjectName(QStringLiteral("tracekitTitle"));
    m_subtitle = new QLabel(header);
    m_subtitle->setObjectName(QStringLiteral("tracekitSubtitle"));
    m_linkBadge = new QLabel(header);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(2);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 0, 4, 4);
    headerLayout->addLayout(titles, 1);
    headerLayout->addWidget(m_linkBadge, 0, Qt::AlignTop);

    // Side list + pages.
    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("tracekitNav"));
    m_nav->setFixedWidth(176);
    m_nav->setIconSize(QSize(18, 18));
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages = new QStackedWidget(this);

    QWidget* settings = buildSettingsTab();
    QWidget* wifi = buildWifiTab();
    QWidget* actions = buildActionsTab();
    m_connectionPages = {settings, wifi, actions};
    m_pages->addWidget(settings);
    addNavItem(tr("Settings"), QStringLiteral("lucide:sliders-horizontal"));
    m_pages->addWidget(wifi);
    addNavItem(tr("Wi-Fi"), QStringLiteral("lucide:wifi"));
    m_pages->addWidget(actions);
    addNavItem(tr("Actions"), QStringLiteral("lucide:zap"));
    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    auto* body = new QHBoxLayout();
    body->setSpacing(14);
    body->addWidget(m_nav);
    body->addWidget(m_pages, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);
    layout->addWidget(header);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    applyTheme();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { applyTheme(); });
}

void TraceKitDialog::addNavItem(const QString& title, const QString& iconId, int row) {
    auto* item = new QListWidgetItem(title);
    item->setSizeHint(QSize(0, 36));
    if (row < 0) {
        m_nav->addItem(item);
        m_navIcons.append(iconId);
    } else {
        m_nav->insertItem(row, item);
        m_navIcons.insert(row, iconId);
    }
    refreshNavIcons();
}

int TraceKitDialog::addPage(QWidget* page, const QString& title, const QString& iconId) {
    // Before Actions, the last of the built-in ones.
    const int index = m_pages->count() - 1;
    const int current = m_nav->currentRow();
    m_pages->insertWidget(index, page);
    addNavItem(title, iconId, index);
    if (current >= index) m_nav->setCurrentRow(current + 1);
    return index;
}

void TraceKitDialog::showPage(int index) {
    if (index >= 0 && index < m_nav->count()) m_nav->setCurrentRow(index);
}

void TraceKitDialog::refreshNavIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    for (int row = 0; row < m_nav->count() && row < m_navIcons.size(); ++row) {
        m_nav->item(row)->setIcon(
            IconLibrary::instance().icon(m_navIcons.at(row), palette.textSecondary, palette.accent, 18));
    }
}

void TraceKitDialog::applyTheme() {
    const ThemePalette& p = ThemeManager::instance().currentTheme();
    setStyleSheet(QStringLiteral(R"(
QLabel#tracekitTitle { font-size: 15pt; font-weight: 600; color: %1; }
QLabel#tracekitSubtitle { color: %2; }
QListWidget#tracekitNav { background: transparent; border: none; outline: none; }
QListWidget#tracekitNav::item {
    color: %2; border-radius: 6px; padding: 4px 8px; margin: 1px 0;
}
QListWidget#tracekitNav::item:hover { background-color: %3; color: %1; }
QListWidget#tracekitNav::item:selected {
    background-color: %4; color: %1; font-weight: 600;
}
QLabel#tracekitState { color: %2; }
)")
                      .arg(p.textPrimary.name(), p.textSecondary.name(), p.surfaceAlt.name(),
                           QStringLiteral("rgba(%1, %2, %3, 0.16)")
                               .arg(p.accent.red())
                               .arg(p.accent.green())
                               .arg(p.accent.blue())));
    refreshNavIcons();
    sizeActionButtons();
    refreshHeader();
}

void TraceKitDialog::sizeActionButtons() {
    // Same rule as the Firmware page's buttons: one size, the longest text's.
    const QFontMetrics metrics(font());
    int width = 0;
    for (const QPushButton* button : m_actionButtons) {
        width = std::max(width, metrics.horizontalAdvance(button->text()));
    }
    const QSize size(width + 40, metrics.height() + 16);
    for (QPushButton* button : m_actionButtons) button->setFixedSize(size);
}

void TraceKitDialog::setDevice(const Device& device) {
    m_device = device;
    refreshHeader();
}

void TraceKitDialog::refreshHeader() {
    if (m_title == nullptr) return;
    const ThemePalette& p = ThemeManager::instance().currentTheme();
    m_title->setText(m_device.name);
    auto info = [this](const char* key) {
        for (const DeviceInfoRecord& entry : m_device.reportedInfo) {
            if (entry.key == QLatin1String(key)) return entry.value;
        }
        return QString();
    };
    QStringList parts;
    if (!info("fw_version").isEmpty()) parts << tr("Firmware %1").arg(info("fw_version"));
    if (!info("chip").isEmpty()) parts << info("chip");
    if (!info("tracekit").isEmpty()) parts << tr("TraceKit %1").arg(info("tracekit"));
    m_subtitle->setText(parts.isEmpty() ? tr("TraceKit device") : parts.join(QStringLiteral("  ·  ")));
    m_subtitle->setVisible(true);

    const bool connected = m_connected || m_device.connected;
    const QColor color = connected ? p.success : p.textSecondary;
    m_linkBadge->setText(QStringLiteral("●  ") + (connected ? tr("Connected") : tr("Not connected")));
    m_linkBadge->setStyleSheet(
        QStringLiteral("QLabel { background-color: rgba(%1, %2, %3, 0.16); color: %4; border-radius: 10px; "
                       "padding: 3px 10px; font-weight: 600; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.name()));
}

void TraceKitDialog::setConnected(bool connected) {
    const bool reconnected = connected && !m_connected;
    m_connected = connected;
    for (QWidget* page : m_connectionPages) page->setEnabled(connected);
    if (!connected) {
        m_settingsState->setText(tr("Not connected: connect the device to change it."));
        m_wifiState->setText(m_settingsState->text());
    }
    refreshHeader();
    if (reconnected) reloadAll();
}

// --- Settings -----------------------------------------------------------

QWidget* TraceKitDialog::buildSettingsTab() {
    auto* page = new QWidget(this);
    m_settingsState = new QLabel(page);
    m_settingsState->setObjectName(QStringLiteral("tracekitState"));
    m_settingsState->setWordWrap(true);

    m_settingsTable = new QTableWidget(0, kColumnCount, page);
    m_settingsTable->setHorizontalHeaderLabels({tr("Setting"), tr("Value"), tr("Default"), QString()});
    m_settingsTable->verticalHeader()->setVisible(false);
    m_settingsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_settingsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_settingsTable->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::ResizeToContents);
    m_settingsTable->horizontalHeader()->setSectionResizeMode(kValueColumn, QHeaderView::Stretch);
    m_settingsTable->horizontalHeader()->setSectionResizeMode(kDefaultColumn, QHeaderView::ResizeToContents);
    m_settingsTable->horizontalHeader()->setSectionResizeMode(kResetColumn, QHeaderView::ResizeToContents);

    auto* reload = new QPushButton(tr("Reload"), page);
    connect(reload, &QPushButton::clicked, this, &TraceKitDialog::reloadAll);
    m_applyButton = new QPushButton(tr("Apply"), page);
    m_applyButton->setToolTip(tr("Saves every changed value on the device (cfg -set)."));
    connect(m_applyButton, &QPushButton::clicked, this, &TraceKitDialog::applySettings);

    auto* row = new QHBoxLayout();
    row->addWidget(m_settingsState, 1);
    row->addWidget(reload);
    row->addWidget(m_applyButton);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(row);
    layout->addWidget(m_settingsTable);
    return page;
}

void TraceKitDialog::reloadAll() {
    ++m_generation;
    m_settings.clear();
    m_settingsTable->setRowCount(0);
    m_settingsState->setText(tr("Reading the settings\xE2\x80\xA6"));
    loadSetting(0);
    reloadWifi();
}

void TraceKitDialog::loadSetting(int index) {
    const int generation = m_generation;
    QPointer<TraceKitDialog> self(this);
    m_runner(QStringLiteral("cfg -json %1").arg(index),
             [self, generation, index](bool ok, const QString& output) {
                 if (!self || generation != self->m_generation) return;
                 if (!ok) {
                     self->m_settingsState->setText(
                         tr("Could not read the settings: %1").arg(output.trimmed()));
                     return;
                 }
                 const QJsonObject decl = parseObject(output);
                 const int count = decl.value("count").toInt(-1);
                 if (count < 0) {
                     self->m_settingsState->setText(
                         tr("The device did not describe its settings (TraceKit too old?)."));
                     return;
                 }
                 if (count == 0) {
                     self->m_settingsState->setText(tr("This device declares no settings."));
                     return;
                 }
                 self->addSettingRow(decl);
                 if (index + 1 < count) {
                     self->m_settingsState->setText(
                         tr("Reading the settings\xE2\x80\xA6 %1 of %2").arg(index + 1).arg(count));
                     self->loadSetting(index + 1);
                 } else {
                     self->m_settingsState->setText(
                         tr("%n setting(s), saved on the device.", nullptr, count));
                 }
             });
}

void TraceKitDialog::addSettingRow(const QJsonObject& decl) {
    const int row = m_settingsTable->rowCount();
    m_settingsTable->insertRow(row);
    const QString name = decl.value("name").toString();
    const QString type = decl.value("type").toString();
    const QString unit = decl.value("unit").toString();
    const QString help = decl.value("help").toString();
    const bool reboot = decl.value("reboot").toBool();

    auto* nameItem = new QTableWidgetItem(reboot ? name + QStringLiteral(" ↻") : name);
    QString tip = help;
    if (reboot) tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + tr("Takes effect after a reboot.");
    nameItem->setToolTip(tip);
    m_settingsTable->setItem(row, kNameColumn, nameItem);

    QString defaultText = valueText(decl.value("default"));
    if (!unit.isEmpty()) defaultText += QLatin1Char(' ') + unit;
    auto* defaultItem = new QTableWidgetItem(defaultText);
    if (decl.contains("min") && type != QLatin1String("text")) {
        defaultItem->setToolTip(tr("Range %1 to %2")
                                    .arg(valueText(decl.value("min")), valueText(decl.value("max"))));
    }
    m_settingsTable->setItem(row, kDefaultColumn, defaultItem);

    // An editor that matches the type: the range the device declared is
    // enforced here already, so a wrong value never makes the round trip.
    QWidget* editor = nullptr;
    const QJsonValue value = decl.value("value");
    if (type == QLatin1String("bool")) {
        auto* box = new QCheckBox(m_settingsTable);
        box->setChecked(value.toBool());
        editor = box;
    } else if (isIntegerType(type) && decl.value("max").toDouble() <= std::numeric_limits<int>::max() &&
               decl.value("min").toDouble() >= std::numeric_limits<int>::min()) {
        auto* spin = new QSpinBox(m_settingsTable);
        spin->setRange(int(decl.value("min").toDouble()), int(decl.value("max").toDouble()));
        spin->setValue(int(value.toDouble()));
        if (!unit.isEmpty()) spin->setSuffix(QLatin1Char(' ') + unit);
        editor = spin;
    } else if (type == QLatin1String("f32") && decl.contains("min")) {
        auto* spin = new QDoubleSpinBox(m_settingsTable);
        spin->setDecimals(4);
        spin->setRange(decl.value("min").toDouble(), decl.value("max").toDouble());
        spin->setValue(value.toDouble());
        if (!unit.isEmpty()) spin->setSuffix(QLatin1Char(' ') + unit);
        editor = spin;
    } else {
        // Text, or a number too wide for a spin box (a full-range u32).
        auto* line = new QLineEdit(valueText(value), m_settingsTable);
        if (type == QLatin1String("text")) {
            line->setMaxLength(decl.value("max").toInt(64));
        } else {
            line->setValidator(new QDoubleValidator(line));
        }
        editor = line;
    }
    editor->setToolTip(help);
    m_settingsTable->setCellWidget(row, kValueColumn, editor);

    auto* reset = new QPushButton(tr("Default"), m_settingsTable);
    reset->setToolTip(tr("Back to the default, now (cfg -reset)."));
    connect(reset, &QPushButton::clicked, this, [this, row]() { resetSetting(row); });
    m_settingsTable->setCellWidget(row, kResetColumn, reset);

    Setting setting;
    setting.decl = decl;
    setting.editor = editor;
    m_settings.append(setting);
    m_settings.last().loaded = editorText(m_settings.last());
}

QString TraceKitDialog::editorText(const Setting& setting) const {
    if (auto* box = qobject_cast<QCheckBox*>(setting.editor)) {
        return box->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (auto* spin = qobject_cast<QSpinBox*>(setting.editor)) return QString::number(spin->value());
    if (auto* spin = qobject_cast<QDoubleSpinBox*>(setting.editor)) {
        return QString::number(spin->value(), 'g', 7);
    }
    if (auto* line = qobject_cast<QLineEdit*>(setting.editor)) return line->text();
    return QString();
}

void TraceKitDialog::applySettings() {
    QVector<int> changed;
    for (int row = 0; row < m_settings.size(); ++row) {
        if (editorText(m_settings[row]) != m_settings[row].loaded) changed.append(row);
    }
    if (changed.isEmpty()) {
        m_settingsState->setText(tr("Nothing changed."));
        return;
    }
    m_applyButton->setEnabled(false);
    applyNext(changed, {});
}

void TraceKitDialog::applyNext(QVector<int> rows, QStringList report) {
    if (rows.isEmpty()) {
        m_applyButton->setEnabled(true);
        m_settingsState->setText(report.join(QStringLiteral("; ")));
        return;
    }
    const int row = rows.takeFirst();
    Setting& setting = m_settings[row];
    const QString name = setting.decl.value("name").toString();
    const QString text = editorText(setting);
    QPointer<TraceKitDialog> self(this);
    m_runner(QStringLiteral("cfg -set %1, %2").arg(name, text),
             [self, row, text, rows, report](bool ok, const QString& output) mutable {
                 if (!self) return;
                 if (ok && row < self->m_settings.size()) self->m_settings[row].loaded = text;
                 report.append(output.trimmed().isEmpty() ? (ok ? QStringLiteral("ok") : QStringLiteral("failed"))
                                                          : output.trimmed());
                 self->applyNext(rows, report);
             });
}

void TraceKitDialog::resetSetting(int row) {
    if (row < 0 || row >= m_settings.size()) return;
    const QString name = m_settings[row].decl.value("name").toString();
    QPointer<TraceKitDialog> self(this);
    m_runner(QStringLiteral("cfg -reset %1").arg(name), [self](bool, const QString& output) {
        if (!self) return;
        self->reloadAll();
        self->m_settingsState->setText(output.trimmed());
    });
}

// --- Wi-Fi --------------------------------------------------------------

QWidget* TraceKitDialog::buildWifiTab() {
    auto* page = new QWidget(this);
    m_wifiState = new QLabel(page);
    m_wifiState->setObjectName(QStringLiteral("tracekitState"));
    m_wifiState->setWordWrap(true);
    m_wifiList = new QListWidget(page);

    auto* add = new QPushButton(tr("Add\xE2\x80\xA6"), page);
    connect(add, &QPushButton::clicked, this, &TraceKitDialog::addWifi);
    auto* remove = new QPushButton(tr("Remove"), page);
    connect(remove, &QPushButton::clicked, this, &TraceKitDialog::removeWifi);
    auto* reboot = new QPushButton(tr("Reboot to use them"), page);
    connect(reboot, &QPushButton::clicked, this, [this]() { action(QStringLiteral("btp -reboot")); });

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(reboot);

    auto* note = new QLabel(tr("Passwords are sent to the device over the encrypted session and "
                               "never read back. Changes take effect after a reboot; the recovery "
                               "uses the same networks."),
                            page);
    note->setWordWrap(true);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_wifiState);
    layout->addWidget(m_wifiList);
    layout->addLayout(buttons);
    layout->addWidget(note);
    return page;
}

void TraceKitDialog::reloadWifi() {
    const int generation = m_generation;
    m_wifiState->setText(tr("Reading the networks\xE2\x80\xA6"));
    QPointer<TraceKitDialog> self(this);
    m_runner(QStringLiteral("btp -wifi_json"), [self, generation](bool ok, const QString& output) {
        if (!self || generation != self->m_generation) return;
        self->m_wifiList->clear();
        const QJsonObject reply = parseObject(output);
        if (!ok || !reply.contains("networks")) {
            self->m_wifiState->setText(tr("Could not read the networks: %1").arg(output.trimmed()));
            return;
        }
        const QJsonArray networks = reply.value("networks").toArray();
        for (const QJsonValue& entry : networks) {
            const QJsonObject network = entry.toObject();
            auto* item = new QListWidgetItem(network.value("ssid").toString(), self->m_wifiList);
            if (network.value("source").toString() == QLatin1String("config")) {
                item->setText(item->text() + tr("  (in the firmware)"));
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                item->setData(Qt::UserRole, QString());
            } else {
                item->setData(Qt::UserRole, network.value("ssid").toString());
            }
        }
        self->m_wifiState->setText(networks.isEmpty()
                                       ? tr("No networks: the device only works over a cable.")
                                       : tr("%1 of %2 networks; tried in this order.")
                                             .arg(networks.size())
                                             .arg(reply.value("max").toInt()));
    });
}

void TraceKitDialog::addWifi() {
    bool ok = false;
    const QString ssid =
        DialogPresenter::getText(this, tr("Add a network"), tr("Network name (SSID):"),
                                 QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || ssid.isEmpty()) return;
    if (ssid.contains(QLatin1Char(','))) {
        DialogPresenter::warning(this, tr("Add a network"), tr("A name with a comma cannot be added from here."));
        return;
    }
    const QString password = DialogPresenter::getText(this, tr("Add a network"),
                                                      tr("Password of %1 (empty: open network):").arg(ssid),
                                                      QLineEdit::Password, QString(), &ok);
    if (!ok) return;
    QPointer<TraceKitDialog> self(this);
    m_runner(QStringLiteral("btp -wifi_add %1, %2").arg(ssid, password), [self](bool, const QString& output) {
        if (!self) return;
        self->reloadWifi();
        self->log(output.trimmed());
    });
}

void TraceKitDialog::removeWifi() {
    QListWidgetItem* item = m_wifiList->currentItem();
    const QString ssid = item != nullptr ? item->data(Qt::UserRole).toString() : QString();
    if (ssid.isEmpty()) {
        DialogPresenter::information(this, tr("Remove a network"),
                                     tr("Pick a network from the list (the one in the firmware "
                                        "cannot be removed from here)."));
        return;
    }
    QPointer<TraceKitDialog> self(this);
    m_runner(QStringLiteral("btp -wifi_remove %1").arg(ssid), [self](bool, const QString& output) {
        if (!self) return;
        self->reloadWifi();
        self->log(output.trimmed());
    });
}

// --- Actions ------------------------------------------------------------

QWidget* TraceKitDialog::buildActionsTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout();

    auto addAction = [this, page, form](const QString& label, const QString& help, auto onClick) {
        auto* button = new QPushButton(label, page);
        m_actionButtons.append(button);
        connect(button, &QPushButton::clicked, this, onClick);
        auto* text = new QLabel(help, page);
        text->setWordWrap(true);
        form->addRow(button, text);
    };

    addAction(tr("Set the clock"), tr("From this computer, unless the device already has the time from the internet."),
              [this]() {
                  action(QStringLiteral("btp -time_sync %1").arg(QDateTime::currentSecsSinceEpoch()));
              });
    addAction(tr("Status"), tr("Link, memory, time, power and sleep."), [this]() {
        for (const char* line : {"btp -status", "btp -time", "btp -power", "btp -sleep"}) {
            action(QString::fromLatin1(line));
        }
    });
    addAction(tr("Keep awake 10 min"), tr("A sleeping sensor stays up for a session or an update."),
              [this]() { action(QStringLiteral("btp -awake 10")); });
    addAction(tr("Reboot"), tr("Restarts the application."), [this]() { action(QStringLiteral("btp -reboot")); });
    addAction(tr("Recovery"), tr("Restarts into the recovery, waiting for a firmware (Firmware page)."),
              [this]() { action(QStringLiteral("btp -recovery")); });
    addAction(tr("USB bootloader"),
              tr("ROM download mode for 30 s: flash over the cable without the BOOT button."),
              [this]() { action(QStringLiteral("btp -bootloader")); });
    addAction(tr("Factory reset\xE2\x80\xA6"), tr("Erases the application and the files; keeps key, Wi-Fi and settings."),
              [this]() {
                  if (DialogPresenter::confirm(this, tr("Factory reset"),
                                               tr("Erase the application and the files of %1? It "
                                                  "then waits in the recovery for a new firmware.")
                                                   .arg(m_device.name),
                                               tr("Erase"), tr("Cancel"))) {
                      action(QStringLiteral("btp -factory_reset yes"));
                  }
              });

    m_log = new QPlainTextEdit(page);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(form);
    layout->addWidget(m_log, 1);
    return page;
}

void TraceKitDialog::action(const QString& line) {
    log(QStringLiteral("> ") + line);
    QPointer<TraceKitDialog> self(this);
    m_runner(line, [self](bool ok, const QString& output) {
        if (!self) return;
        const QString text = output.trimmed();
        self->log(text.isEmpty() ? (ok ? tr("ok") : tr("failed")) : text);
    });
}

void TraceKitDialog::log(const QString& text) {
    if (m_log != nullptr && !text.isEmpty()) m_log->appendPlainText(text);
}

}  // namespace traceview
