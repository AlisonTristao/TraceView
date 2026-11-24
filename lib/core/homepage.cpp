#include "core/homepage.h"

#include <QBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>

#include "core/touchscroll.h"
#include "theme/dialogchrome.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace traceview {

namespace {
// Below this width the two columns stack (phones, narrow windows).
constexpr int kTwoColumnMinWidth = 760;
constexpr int kMaxContentWidth = 1100;
constexpr int kMaxRecent = 5;
constexpr int kLinkIconSize = 16;

QString linkIconsTooltip(const NearbyRobot& robot) {
    QStringList where;
    if (!robot.serialPort.isEmpty()) {
        where << robot.serialPort;
    }
    if (!robot.tcpHost.isEmpty()) {
        where << (robot.tcpPort != 0 && robot.tcpPort != 44300
                      ? QStringLiteral("%1:%2").arg(robot.tcpHost).arg(robot.tcpPort)
                      : robot.tcpHost);
    }
    if (!robot.bleAddress.isEmpty()) {
        where << QStringLiteral("Bluetooth");
    }
    return where.join(QStringLiteral(" · "));
}

QStringList linkIconIds(const NearbyRobot& robot) {
    QStringList ids;
    if (!robot.serialPort.isEmpty()) {
        ids << QStringLiteral("lucide:usb");
    }
    if (!robot.tcpHost.isEmpty()) {
        ids << QStringLiteral("lucide:wifi");
    }
    if (!robot.bleAddress.isEmpty()) {
        ids << QStringLiteral("lucide:bluetooth");
    }
    return ids;
}

// Whether `robot` is the robot `device` describes: same name, or the same
// port/host/address.
bool sameRobot(const HomeDevice& device, const NearbyRobot& robot) {
    const QString name = (device.robotName.isEmpty() ? device.name : device.robotName).toLower();
    if (!robot.name.isEmpty() && robot.key == name.trimmed()) {
        return true;
    }
    return (!device.serialPort.isEmpty() && device.serialPort == robot.serialPort) ||
           (!device.tcpHost.isEmpty() && device.tcpHost.compare(robot.tcpHost, Qt::CaseInsensitive) == 0) ||
           (!device.bleAddress.isEmpty() && device.bleAddress == robot.bleAddress);
}

void clearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        } else if (QLayout* child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}
}  // namespace

HomePage::HomePage(NearbyRobotsService* nearby, QWidget* parent)
    : QWidget(parent), m_nearby(nearby) {
    setObjectName(QStringLiteral("homePage"));
    buildUi();
    retranslate();

    connect(m_nearby, &NearbyRobotsService::robotsChanged, this, [this] { refreshRobots(); });
    connect(m_nearby, &NearbyRobotsService::bluetoothScanningChanged, this,
            [this] { refreshScanControls(); });
    connect(m_nearby, &NearbyRobotsService::deepScanProgress, this, [this](int done, int total) {
        m_scanProgressDone = done;
        m_scanProgressTotal = total;
        refreshScanControls();
    });
    connect(m_nearby, &NearbyRobotsService::deepScanFinished, this, [this](int found) {
        m_lastScanResult = found == 0 ? tr("The deep scan found nothing new.")
                                      : tr("The deep scan found %n robot(s).", nullptr, found);
        refreshScanControls();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { refreshRobots(/*force=*/true); });
}

void HomePage::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTouchScroll(m_scroll, true);
    outer->addWidget(m_scroll);

    auto* canvas = new QWidget(m_scroll);
    canvas->setObjectName(QStringLiteral("homeCanvas"));
    m_scroll->setWidget(canvas);
    auto* canvasLayout = new QHBoxLayout(canvas);
    canvasLayout->setContentsMargins(16, 20, 16, 20);

    // Centered, never wider than kMaxContentWidth on a big monitor.
    auto* content = new QWidget(canvas);
    content->setMaximumWidth(kMaxContentWidth);
    canvasLayout->addStretch();
    canvasLayout->addWidget(content, 100);
    canvasLayout->addStretch();

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(DialogChrome::kBodySpacing);

    m_title = new QLabel(content);
    m_title->setObjectName(QStringLiteral("dialogTitle"));
    m_title->setFont(scaledFont(font(), DialogChrome::kTitleScale));
    m_subtitle = new QLabel(content);
    m_subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    m_subtitle->setWordWrap(true);
    layout->addWidget(m_title);
    layout->addWidget(m_subtitle);

    m_columns = new QBoxLayout(QBoxLayout::LeftToRight);
    m_columns->setSpacing(DialogChrome::kBodySpacing);
    layout->addLayout(m_columns);
    layout->addStretch();

    auto* left = new QVBoxLayout();
    left->setSpacing(DialogChrome::kSectionSpacing);
    auto* right = new QVBoxLayout();
    right->setSpacing(DialogChrome::kSectionSpacing);
    m_columns->addLayout(left, 3);
    m_columns->addLayout(right, 2);
    m_leftColumn = left;
    m_rightColumn = right;

    // --- Continue -----------------------------------------------------------
    m_projectCard = new DialogCard(QString(), content);
    m_projectName = new QLabel(m_projectCard);
    m_projectName->setFont(scaledFont(font(), DialogChrome::kPageTitleScale));
    m_projectName->setWordWrap(true);
    m_projectDetail = DialogChrome::hintLabel(QString(), m_projectCard);
    m_openDashboardButton = new QPushButton(m_projectCard);
    DialogChrome::setButtonVariant(m_openDashboardButton, "primary");
    connect(m_openDashboardButton, &QPushButton::clicked, this, &HomePage::openDashboardRequested);
    auto* continueRow = new QHBoxLayout();
    auto* continueText = new QVBoxLayout();
    continueText->setSpacing(2);
    continueText->addWidget(m_projectName);
    continueText->addWidget(m_projectDetail);
    continueRow->addLayout(continueText, 1);
    continueRow->addWidget(m_openDashboardButton, 0, Qt::AlignVCenter);
    m_projectCard->body()->addLayout(continueRow);
    left->addWidget(m_projectCard);

    // --- Projects (Developer) ----------------------------------------------
    m_projectsCard = new DialogCard(QString(), content);
    m_recentCaption = DialogChrome::hintLabel(QString(), m_projectsCard);
    m_recentList = new QVBoxLayout();
    m_recentList->setSpacing(0);
    m_galleryCaption = DialogChrome::hintLabel(QString(), m_projectsCard);
    m_galleryList = new QVBoxLayout();
    m_galleryList->setSpacing(0);
    m_projectsCard->body()->addWidget(m_recentCaption);
    m_projectsCard->body()->addLayout(m_recentList);
    m_projectsCard->body()->addWidget(m_galleryCaption);
    m_projectsCard->body()->addLayout(m_galleryList);

    m_newButton = new QPushButton(m_projectsCard);
    m_openButton = new QPushButton(m_projectsCard);
    m_openLogButton = new QPushButton(m_projectsCard);
    m_manageGalleryButton = new QPushButton(m_projectsCard);
    connect(m_newButton, &QPushButton::clicked, this, &HomePage::newProjectRequested);
    connect(m_openButton, &QPushButton::clicked, this, &HomePage::openProjectRequested);
    connect(m_openLogButton, &QPushButton::clicked, this, &HomePage::openLogRequested);
    connect(m_manageGalleryButton, &QPushButton::clicked, this,
            &HomePage::manageGalleryRequested);
    // Two rows of two: four buttons side by side don't fit a phone.
    auto* projectButtons = new QVBoxLayout();
    projectButtons->setSpacing(6);
    auto* projectButtonsTop = new QHBoxLayout();
    projectButtonsTop->addWidget(m_newButton);
    projectButtonsTop->addWidget(m_openButton);
    projectButtonsTop->addStretch();
    auto* projectButtonsBottom = new QHBoxLayout();
    projectButtonsBottom->addWidget(m_openLogButton);
    projectButtonsBottom->addWidget(m_manageGalleryButton);
    projectButtonsBottom->addStretch();
    projectButtons->addLayout(projectButtonsTop);
    projectButtons->addLayout(projectButtonsBottom);
    m_projectsCard->body()->addLayout(projectButtons);
    left->addWidget(m_projectsCard);
    left->addStretch();

    // --- Nearby robots ------------------------------------------------------
    m_robotsCard = new DialogCard(QString(), content);
    m_robotsStatus = DialogChrome::hintLabel(QString(), m_robotsCard);
    m_robotsList = new QVBoxLayout();
    m_robotsList->setSpacing(0);
    m_robotsCard->body()->addLayout(m_robotsList);
    m_robotsCard->body()->addWidget(m_robotsStatus);

    m_bluetoothButton = new QPushButton(m_robotsCard);
    m_bluetoothButton->setVisible(NearbyRobotsService::bluetoothAvailable());
    connect(m_bluetoothButton, &QPushButton::clicked, this, [this] { m_nearby->scanBluetooth(); });
    m_deepScanButton = new QPushButton(m_robotsCard);
    connect(m_deepScanButton, &QPushButton::clicked, this, [this] {
        if (m_nearby->deepScanRunning()) {
            m_nearby->cancelDeepScan();
        } else {
            m_lastScanResult.clear();
            m_scanProgressDone = 0;
            m_scanProgressTotal = 0;
            m_nearby->startDeepScan();
        }
        refreshScanControls();
    });
    auto* scanButtons = new QHBoxLayout();
    scanButtons->addWidget(m_bluetoothButton);
    scanButtons->addWidget(m_deepScanButton);
    scanButtons->addStretch();
    m_robotsCard->body()->addLayout(scanButtons);
    m_deepScanProgress = new QProgressBar(m_robotsCard);
    m_deepScanProgress->setTextVisible(false);
    m_deepScanProgress->setFixedHeight(6);
    m_deepScanProgress->setVisible(false);
    m_robotsCard->body()->addWidget(m_deepScanProgress);
    m_deepScanHint = DialogChrome::hintLabel(QString(), m_robotsCard);
    m_robotsCard->body()->addWidget(m_deepScanHint);
    right->addWidget(m_robotsCard);
    right->addStretch();

    m_projectsCard->setVisible(m_developer);
}

void HomePage::retranslate() {
    m_title->setText(tr("Welcome to TraceView"));
    m_subtitle->setText(tr("Version %1 · Pick up your dashboard or connect a robot.").arg(kVersion));
    m_projectCard->setTitle(tr("Continue where you left off"));
    m_openDashboardButton->setText(tr("Open dashboard"));
    m_projectsCard->setTitle(tr("Projects"));
    m_recentCaption->setText(tr("Recent"));
    m_galleryCaption->setText(tr("Gallery"));
    m_newButton->setText(tr("New project"));
    m_openButton->setText(tr("Open project..."));
    m_openLogButton->setText(tr("Open log..."));
    m_manageGalleryButton->setText(tr("Manage gallery..."));
    m_robotsCard->setTitle(tr("Nearby robots"));
    m_bluetoothButton->setText(tr("Scan Bluetooth"));
    m_deepScanHint->setToolTip(QString());
    refreshProjectCard();
    refreshProjectsCard();
    refreshRobots(/*force=*/true);
    refreshScanControls();
}

void HomePage::setDeveloperMode(bool developer) {
    m_developer = developer;
    m_projectsCard->setVisible(developer);
    refreshProjectCard();
    refreshRobots(/*force=*/true);
}

void HomePage::setProjectSummary(const HomeProjectSummary& summary) {
    m_project = summary;
    refreshProjectCard();
}

void HomePage::setDevices(const QVector<HomeDevice>& devices) {
    m_devices = devices;
    refreshRobots();
}

void HomePage::setRecentProjects(const QStringList& paths) {
    m_recent = paths.mid(0, kMaxRecent);
    refreshProjectsCard();
}

void HomePage::setGallery(const QStringList& names, const QString& defaultName) {
    m_gallery = names;
    m_galleryDefault = defaultName;
    refreshProjectsCard();
}

void HomePage::refreshProjectCard() {
    m_projectName->setText(m_project.title.isEmpty() ? tr("Untitled project") : m_project.title);
    QStringList parts;
    parts << tr("%n workspace(s)", nullptr, m_project.workspaces);
    parts << tr("%n device(s)", nullptr, m_project.devices);
    QString detail = parts.join(QStringLiteral(" · "));
    if (m_developer && !m_project.path.isEmpty() && !m_project.path.startsWith(QLatin1Char(':'))) {
        detail += QLatin1Char('\n') + QDir::toNativeSeparators(m_project.path);
    }
    m_projectDetail->setText(detail);
}

QPushButton* HomePage::linkButton(const QString& text, const QString& tooltip) {
    auto* button = new QPushButton(text);
    // A list row, not a framed button (stylesheet.cpp, "QPushButton#homeLink").
    button->setObjectName(QStringLiteral("homeLink"));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    return button;
}

void HomePage::refreshProjectsCard() {
    clearLayout(m_recentList);
    clearLayout(m_galleryList);
    for (const QString& path : std::as_const(m_recent)) {
        QPushButton* button = linkButton(QFileInfo(path).completeBaseName(),
                                         QDir::toNativeSeparators(path));
        connect(button, &QPushButton::clicked, this, [this, path] { emit openPathRequested(path); });
        m_recentList->addWidget(button);
    }
    if (m_recent.isEmpty()) {
        m_recentList->addWidget(DialogChrome::hintLabel(tr("No recent projects yet."), nullptr));
    }
    for (const QString& name : std::as_const(m_gallery)) {
        const QString text =
            name == m_galleryDefault ? tr("%1 (opens at startup)").arg(name) : name;
        QPushButton* button = linkButton(text, QString());
        connect(button, &QPushButton::clicked, this,
                [this, name] { emit openGalleryRequested(name); });
        m_galleryList->addWidget(button);
    }
    m_galleryCaption->setVisible(!m_gallery.isEmpty());
}

QWidget* HomePage::robotRow(const QString& title, const QString& detail,
                            const QStringList& linkIcons, const QString& badgeText, int badgeTone,
                            QPushButton* action) {
    auto* row = new QWidget();
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(10);

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    auto* icons = new QHBoxLayout();
    icons->setSpacing(2);
    const QStringList ids = linkIcons.isEmpty() ? QStringList{QStringLiteral("lucide:bot")} : linkIcons;
    for (const QString& id : ids) {
        auto* icon = new QLabel(row);
        icon->setPixmap(IconLibrary::instance()
                            .icon(id, linkIcons.isEmpty() ? palette.textDisabled : palette.textSecondary,
                                  kLinkIconSize)
                            .pixmap(kLinkIconSize, kLinkIconSize));
        icons->addWidget(icon);
    }
    layout->addLayout(icons);

    auto* text = new QVBoxLayout();
    text->setSpacing(0);
    auto* name = new QLabel(title, row);
    name->setWordWrap(true);
    text->addWidget(name);
    if (!detail.isEmpty()) {
        text->addWidget(DialogChrome::hintLabel(detail, row));
    }
    layout->addLayout(text, 1);

    if (!badgeText.isEmpty()) {
        auto* badge = new StatusBadge(row);
        badge->setStatus(badgeText, StatusBadge::Tone(badgeTone));
        layout->addWidget(badge, 0, Qt::AlignVCenter);
    }
    if (action != nullptr) {
        action->setParent(row);
        layout->addWidget(action, 0, Qt::AlignVCenter);
    }
    return row;
}

void HomePage::refreshRobots(bool force) {
    const QVector<NearbyRobot> nearby = m_nearby->robots();

    // Rebuilt only when something shown changed: the serial watch reports
    // every two seconds, and a rebuild under the cursor would eat clicks.
    QString signature = m_developer ? QStringLiteral("D|") : QStringLiteral("U|");
    for (const HomeDevice& device : std::as_const(m_devices)) {
        signature += QStringLiteral("%1,%2,%3,%4|")
                         .arg(device.id, device.name)
                         .arg(device.connected)
                         .arg(device.connecting);
    }
    for (const NearbyRobot& robot : nearby) {
        signature += QStringLiteral("%1,%2,%3,%4,%5|")
                         .arg(robot.key, robot.serialPort, robot.tcpHost, robot.bleAddress)
                         .arg(robot.dongle);
    }
    if (!force && signature == m_robotsSignature) {
        return;
    }
    m_robotsSignature = signature;
    clearLayout(m_robotsList);

    QSet<QString> matched;
    for (const HomeDevice& device : std::as_const(m_devices)) {
        const NearbyRobot* seen = nullptr;
        for (const NearbyRobot& robot : nearby) {
            if (sameRobot(device, robot)) {
                seen = &robot;
                matched.insert(robot.key);
                break;
            }
        }
        QString badge;
        StatusBadge::Tone tone = StatusBadge::Tone::Neutral;
        QString actionText;
        if (device.connected) {
            badge = tr("Connected");
            tone = StatusBadge::Tone::Success;
            actionText = tr("Disconnect");
        } else if (device.connecting) {
            badge = seen != nullptr ? tr("Connecting") : tr("Searching");
            tone = StatusBadge::Tone::Warning;
            actionText = tr("Stop");
        } else {
            badge = seen != nullptr ? tr("Nearby") : tr("Offline");
            tone = seen != nullptr ? StatusBadge::Tone::Accent : StatusBadge::Tone::Neutral;
            actionText = tr("Connect");
        }
        auto* action = new QPushButton(actionText);
        if (!device.connected && !device.connecting && seen != nullptr) {
            DialogChrome::setButtonVariant(action, "primary");
        }
        connect(action, &QPushButton::clicked, this,
                [this, id = device.id] { emit connectToggleRequested(id); });
        const QString detail = seen != nullptr ? linkIconsTooltip(*seen) : tr("In this project");
        m_robotsList->addWidget(robotRow(device.name, detail,
                                         seen != nullptr ? linkIconIds(*seen) : QStringList(), badge,
                                         int(tone), action));
    }

    int newCount = 0;
    for (const NearbyRobot& robot : nearby) {
        if (matched.contains(robot.key)) {
            continue;
        }
        ++newCount;
        QString title = robot.name;
        if (robot.dongle) {
            title = tr("Dongle");
        } else if (title.isEmpty()) {
            title = tr("Unnamed robot");
        }
        QPushButton* action = nullptr;
        if (m_developer) {
            action = new QPushButton(tr("Add"));
            action->setToolTip(tr("Add it to this project and connect"));
            connect(action, &QPushButton::clicked, this,
                    [this, robot] { emit addRobotRequested(robot); });
        }
        m_robotsList->addWidget(robotRow(title, linkIconsTooltip(robot), linkIconIds(robot),
                                         robot.dongle ? QString() : tr("New"),
                                         int(StatusBadge::Tone::Accent), action));
    }

    if (m_devices.isEmpty() && newCount == 0) {
        m_robotsList->addWidget(
            DialogChrome::hintLabel(tr("No robots found yet. Plug one in over USB, or put it on "
                                       "the same Wi-Fi as this computer."),
                                    nullptr));
    }
    refreshScanControls();
}

void HomePage::refreshScanControls() {
    // User mode cannot add what a deep scan finds -- and the scan can
    // restart boards -- so it is a Developer tool.
    m_deepScanButton->setVisible(m_developer);
    m_deepScanHint->setVisible(m_developer);
    const bool deep = m_nearby->deepScanRunning();
    const bool bluetooth = m_nearby->bluetoothScanning();
    m_bluetoothButton->setEnabled(!bluetooth);
    m_deepScanButton->setText(deep ? tr("Cancel scan") : tr("Deep scan"));
    m_deepScanProgress->setVisible(deep && m_developer);
    if (deep) {
        m_deepScanProgress->setMaximum(qMax(1, m_scanProgressTotal));
        m_deepScanProgress->setValue(m_scanProgressDone);
    }

    QStringList status;
    status << tr("Watching USB and Wi-Fi (mDNS).");
    if (bluetooth) {
        status << tr("Scanning Bluetooth...");
    }
    m_robotsStatus->setText(status.join(QLatin1Char(' ')));

    if (deep) {
        m_deepScanHint->setText(tr("Deep scan: opening USB serial ports and trying the local "
                                   "network (%1 of %2)...")
                                    .arg(m_scanProgressDone)
                                    .arg(m_scanProgressTotal));
    } else if (!m_lastScanResult.isEmpty()) {
        m_deepScanHint->setText(m_lastScanResult);
    } else {
        m_deepScanHint->setText(
            tr("Deep scan opens every unidentified USB serial port and tries port 44300 on "
               "your local network. Opening a port can restart a board that is plugged in."));
    }
}

void HomePage::updateColumns() {
    const bool wide = m_scroll->viewport()->width() >= kTwoColumnMinWidth;
    if (m_wide == wide) {
        return;
    }
    m_wide = wide;
    m_columns->setDirection(wide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    // One column (a phone): the robots come right after "Continue", before
    // the project lists -- connecting is what a phone is mostly used for.
    if (wide) {
        m_leftColumn->removeWidget(m_robotsCard);
        m_rightColumn->insertWidget(0, m_robotsCard);
    } else {
        m_rightColumn->removeWidget(m_robotsCard);
        m_leftColumn->insertWidget(1, m_robotsCard);
    }
}

void HomePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateColumns();
    m_nearby->setActive(true);
}

void HomePage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    m_nearby->setActive(false);
}

void HomePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateColumns();
}

void HomePage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    } else if (event->type() == QEvent::FontChange) {
        m_title->setFont(scaledFont(font(), DialogChrome::kTitleScale));
        m_projectName->setFont(scaledFont(font(), DialogChrome::kPageTitleScale));
    }
}

}  // namespace traceview
