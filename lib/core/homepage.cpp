#include "core/homepage.h"

#include <QBoxLayout>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHash>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolButton>
#include <algorithm>
#include <functional>

#include "core/touchscroll.h"
#include "theme/dialogchrome.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace traceview {

namespace {
// Two columns from this viewport width on; below it one (tablets, phones).
constexpr int kTwoColumnMinWidth = 980;
// Below this width button rows stack one per line and the header stacks.
constexpr int kCompactMaxWidth = 620;
constexpr int kMaxContentWidth = 1160;
constexpr int kMaxRecent = 5;
constexpr int kLinkIconSize = 14;
constexpr int kAvatarSize = 34;
constexpr int kProjectIconSize = 48;
constexpr int kProjectIconGlyph = 26;
constexpr int kGalleryIconSize = 44;
constexpr int kGalleryIconGlyph = 24;
// Gallery cards per row from this viewport width on.
// Narrowest a gallery card gets before the grid drops a column.
constexpr int kGalleryCardMinWidth = 290;
constexpr int kAvatarIconSize = 18;
constexpr int kButtonIconSize = 16;
constexpr int kRowIconSize = 16;

constexpr char kWordmarkResource[] = ":/branding/wordmark.svg";
// The SVG's own light text color -- swapped for the theme's text color.
constexpr char kWordmarkLightColor[] = "#F5F7FA";

QString linkSummary(const NearbyRobot& robot) {
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
           (!device.tcpHost.isEmpty() &&
            device.tcpHost.compare(robot.tcpHost, Qt::CaseInsensitive) == 0) ||
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

// Puts `widgets` into `grid` `columns` to a row, equal widths.
void reflow(QGridLayout* grid, const QList<QWidget*>& widgets, int columns) {
    for (QWidget* widget : widgets) {
        grid->removeWidget(widget);
    }
    for (int c = 0; c < 4; ++c) {
        grid->setColumnStretch(c, 0);
    }
    QList<QWidget*> shown;
    for (QWidget* widget : widgets) {
        if (!(widget->isHidden() && widget->property("homeSkip").toBool())) {
            shown.append(widget);
        }
    }
    // A lone button takes the whole row.
    columns = qMax(1, qMin(columns, int(shown.size())));
    for (int index = 0; index < shown.size(); ++index) {
        grid->addWidget(shown[index], index / columns, index % columns);
    }
    for (int c = 0; c < columns; ++c) {
        grid->setColumnStretch(c, 1);
    }
}

void repolish(QWidget* widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}
}  // namespace

// A one-line name that ends in "..." when it doesn't fit, instead of making
// the page wider: names often have no spaces to wrap at (robot_lab_2), and
// on a phone one long name would push every card off the screen. The full
// text stays in the tooltip.
class HomeNameLabel : public QLabel {
public:
    explicit HomeNameLabel(const QString& text, QWidget* parent) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setFullText(text);
    }
    void setFullText(const QString& text) {
        m_text = text;
        setToolTip(text);
        updateGeometry();
        refit();
    }
    QSize minimumSizeHint() const override {
        return QSize(fontMetrics().averageCharWidth() * 6, QLabel::minimumSizeHint().height());
    }
    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(m_text) + 2, QLabel::sizeHint().height());
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        refit();
    }
    void changeEvent(QEvent* event) override {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange) {
            refit();
        }
    }

private:
    void refit() {
        setText(fontMetrics().elidedText(m_text, Qt::ElideRight, qMax(0, width())));
    }
    QString m_text;
};

// One dashboard of the gallery: the whole card is the click target (and
// Enter/Space when focused); a right-click opens the same menu as the "..."
// button, when there is one (stylesheet.cpp, "QFrame#homeGalleryCard").
class HomeGalleryCard : public QFrame {
public:
    explicit HomeGalleryCard(QWidget* parent) : QFrame(parent) {
        setObjectName(QStringLiteral("homeGalleryCard"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        // Width comes from the grid's equal column stretch, never from the
        // content: a card with badges must not squeeze its neighbours.
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    void setMenu(QMenu* menu) {
        m_menu = menu;
    }
    std::function<void()> onActivate;

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            if (onActivate) {
                onActivate();
            }
            return;
        }
        QFrame::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            if (onActivate) {
                onActivate();
            }
            return;
        }
        QFrame::keyPressEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (m_menu == nullptr) {
            event->ignore();
            return;
        }
        m_menu->popup(event->globalPos());
    }

private:
    QMenu* m_menu = nullptr;
};

// The TraceView wordmark, recolored to the theme's text color.
class HomeWordmark : public QWidget {
public:
    explicit HomeWordmark(QWidget* parent) : QWidget(parent) {
        QFile file(kWordmarkResource);
        if (file.open(QIODevice::ReadOnly)) {
            m_template = file.readAll();
        }
        m_renderer = new QSvgRenderer(this);
        setColor(ThemeManager::instance().currentTheme().textPrimary);
    }
    void setColor(const QColor& color) {
        QByteArray svg = m_template;
        svg.replace(kWordmarkLightColor, color.name(QColor::HexRgb).toUtf8());
        m_renderer->load(svg);
        update();
    }
    void setDisplayHeight(int height) {
        const QSizeF box = m_renderer->isValid() ? m_renderer->viewBoxF().size() : QSizeF(520, 200);
        setFixedSize(qRound(height * box.width() / box.height()), height);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (!m_renderer->isValid()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_renderer->render(&painter, QRectF(rect()));
    }

private:
    QByteArray m_template;
    QSvgRenderer* m_renderer = nullptr;
};

HomePage::HomePage(NearbyRobotsService* nearby, QWidget* parent)
    : QWidget(parent), m_nearby(nearby) {
    setObjectName(QStringLiteral("homePage"));
    buildUi();
    retranslate();
    refreshIcons();

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
            [this](const ThemePalette& palette) {
                m_wordmark->setColor(palette.textPrimary);
                refreshIcons();
                refreshProjectCard();
                refreshProjectIcon();
                refreshProjectsCard();
                refreshGalleryCard();
                refreshRobots(/*force=*/true);
            });
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
    canvasLayout->setContentsMargins(16, 16, 16, 20);

    // Centered, never wider than kMaxContentWidth on a big monitor.
    auto* content = new QWidget(canvas);
    content->setMaximumWidth(kMaxContentWidth);
    content->setMinimumWidth(0);
    canvasLayout->addStretch(1);
    canvasLayout->addWidget(content, 100);
    canvasLayout->addStretch(1);

    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(DialogChrome::kBodySpacing);

    // --- Header -------------------------------------------------------------
    m_hero = new QFrame(content);
    m_hero->setObjectName(QStringLiteral("homeHero"));
    m_heroLayout = new QBoxLayout(QBoxLayout::LeftToRight, m_hero);
    m_heroLayout->setContentsMargins(20, 16, 20, 16);
    m_heroLayout->setSpacing(18);

    auto* brand = new QVBoxLayout();
    brand->setSpacing(4);
    m_wordmark = new HomeWordmark(m_hero);
    m_wordmark->setDisplayHeight(52);
    m_title = new QLabel(m_hero);
    m_title->setObjectName(QStringLiteral("dialogTitle"));
    m_title->setFont(scaledFont(font(), DialogChrome::kPageTitleScale));
    m_title->setWordWrap(true);
    m_subtitle = new QLabel(m_hero);
    m_subtitle->setObjectName(QStringLiteral("dialogSubtitle"));
    m_subtitle->setWordWrap(true);
    brand->addWidget(m_wordmark);
    brand->addWidget(m_title);
    brand->addWidget(m_subtitle);
    m_heroLayout->addLayout(brand, 1);

    auto* stats = new QHBoxLayout();
    stats->setSpacing(10);
    for (int i = 0; i < 3; ++i) {
        auto* tile = new QFrame(m_hero);
        tile->setObjectName(QStringLiteral("homeTile"));
        auto* tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(14, 10, 14, 10);
        tileLayout->setSpacing(0);
        m_statValue[i] = new QLabel(QStringLiteral("0"), tile);
        m_statValue[i]->setObjectName(QStringLiteral("homeTileValue"));
        m_statValue[i]->setFont(scaledFont(font(), 1.7));
        m_statCaption[i] = new QLabel(tile);
        m_statCaption[i]->setObjectName(QStringLiteral("dialogHint"));
        m_statCaption[i]->setWordWrap(true);
        tileLayout->addWidget(m_statValue[i]);
        tileLayout->addWidget(m_statCaption[i]);
        stats->addWidget(tile, 1);
    }
    m_heroLayout->addLayout(stats, 1);
    layout->addWidget(m_hero);

    m_columns = new QBoxLayout(QBoxLayout::LeftToRight);
    m_columns->setSpacing(DialogChrome::kBodySpacing);
    layout->addLayout(m_columns);
    layout->addStretch();

    m_leftColumn = new QVBoxLayout();
    m_leftColumn->setSpacing(DialogChrome::kSectionSpacing);
    m_rightColumn = new QVBoxLayout();
    m_rightColumn->setSpacing(DialogChrome::kSectionSpacing);
    m_columns->addLayout(m_leftColumn, 3);
    m_columns->addLayout(m_rightColumn, 2);

    // --- Continue -----------------------------------------------------------
    m_projectCard = new DialogCard(QString(), content);
    m_projectName = new HomeNameLabel(QString(), m_projectCard);
    m_projectName->setFont(scaledFont(font(), DialogChrome::kPageTitleScale));
    m_projectDetail = DialogChrome::hintLabel(QString(), m_projectCard);
    m_openDashboardButton = new QPushButton(m_projectCard);
    m_openDashboardButton->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
    DialogChrome::setButtonVariant(m_openDashboardButton, "primary");
    connect(m_openDashboardButton, &QPushButton::clicked, this, &HomePage::openDashboardRequested);
    m_continueRow = new QBoxLayout(QBoxLayout::LeftToRight);
    m_continueRow->setSpacing(10);
    // A plain dashboard glyph on an accent tile beside the name: the
    // project at a glance, without picturing the dashboard itself.
    m_projectIcon = new QLabel(m_projectCard);
    m_projectIcon->setObjectName(QStringLiteral("homeProjectIcon"));
    m_projectIcon->setFixedSize(kProjectIconSize, kProjectIconSize);
    m_projectIcon->setAlignment(Qt::AlignCenter);
    auto* continueText = new QVBoxLayout();
    continueText->setSpacing(2);
    continueText->addWidget(m_projectName);
    continueText->addWidget(m_projectDetail);
    // Icon and name stay side by side even when the row stacks (compact).
    auto* continueIdentity = new QHBoxLayout();
    continueIdentity->setSpacing(12);
    continueIdentity->addWidget(m_projectIcon, 0, Qt::AlignVCenter);
    continueIdentity->addLayout(continueText, 1);
    m_continueRow->addLayout(continueIdentity, 1);
    m_continueRow->addWidget(m_openDashboardButton, 0, Qt::AlignVCenter);
    m_projectCard->body()->addLayout(m_continueRow);

    m_workspacesCaption = DialogChrome::hintLabel(QString(), m_projectCard);
    m_projectCard->body()->addWidget(m_workspacesCaption);
    m_workspaceGrid = new QGridLayout();
    m_workspaceGrid->setHorizontalSpacing(8);
    m_workspaceGrid->setVerticalSpacing(8);
    m_projectCard->body()->addLayout(m_workspaceGrid);
    m_leftColumn->addWidget(m_projectCard);

    // --- Gallery (both modes) ----------------------------------------------
    m_galleryCard = new DialogCard(QString(), content);
    m_galleryCount = DialogChrome::hintLabel(QString(), m_galleryCard);
    m_galleryCard->addTitleWidget(m_galleryCount);
    m_galleryGrid = new QGridLayout();
    m_galleryGrid->setHorizontalSpacing(10);
    m_galleryGrid->setVerticalSpacing(10);
    m_galleryCard->body()->addLayout(m_galleryGrid);
    m_galleryCard->setVisible(false);
    m_leftColumn->addWidget(m_galleryCard);

    // --- Projects (Developer) ----------------------------------------------
    m_projectsCard = new DialogCard(QString(), content);
    m_recentCaption = DialogChrome::hintLabel(QString(), m_projectsCard);
    m_recentList = new QVBoxLayout();
    m_recentList->setSpacing(0);
    m_projectsCard->body()->addWidget(m_recentCaption);
    m_projectsCard->body()->addLayout(m_recentList);

    m_newButton = new QPushButton(m_projectsCard);
    m_openButton = new QPushButton(m_projectsCard);
    m_openLogButton = new QPushButton(m_projectsCard);
    m_manageGalleryButton = new QPushButton(m_projectsCard);
    for (QPushButton* button : {m_newButton, m_openButton, m_openLogButton, m_manageGalleryButton}) {
        button->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
    }
    connect(m_newButton, &QPushButton::clicked, this, &HomePage::newProjectRequested);
    connect(m_openButton, &QPushButton::clicked, this, &HomePage::openProjectRequested);
    connect(m_openLogButton, &QPushButton::clicked, this, &HomePage::openLogRequested);
    connect(m_manageGalleryButton, &QPushButton::clicked, this,
            &HomePage::manageGalleryRequested);
    m_projectButtons = new QGridLayout();
    m_projectButtons->setHorizontalSpacing(8);
    m_projectButtons->setVerticalSpacing(6);
    m_projectsCard->body()->addLayout(m_projectButtons);
    m_leftColumn->addWidget(m_projectsCard);
    m_leftColumn->addStretch();

    // --- Nearby robots ------------------------------------------------------
    m_robotsCard = new DialogCard(QString(), content);
    m_robotsList = new QVBoxLayout();
    m_robotsList->setSpacing(2);
    m_robotsCard->body()->addLayout(m_robotsList);
    m_robotsStatus = DialogChrome::hintLabel(QString(), m_robotsCard);
    m_robotsCard->body()->addWidget(m_robotsStatus);

    m_bluetoothButton = new QPushButton(m_robotsCard);
    m_bluetoothButton->setVisible(NearbyRobotsService::bluetoothAvailable());
    m_bluetoothButton->setProperty("homeSkip", true);
    m_bluetoothButton->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
    connect(m_bluetoothButton, &QPushButton::clicked, this, [this] { m_nearby->scanBluetooth(); });
    m_deepScanButton = new QPushButton(m_robotsCard);
    m_deepScanButton->setProperty("homeSkip", true);
    m_deepScanButton->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
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
    m_scanButtons = new QGridLayout();
    m_scanButtons->setHorizontalSpacing(8);
    m_scanButtons->setVerticalSpacing(6);
    m_robotsCard->body()->addLayout(m_scanButtons);
    m_deepScanProgress = new QProgressBar(m_robotsCard);
    m_deepScanProgress->setTextVisible(false);
    m_deepScanProgress->setFixedHeight(6);
    m_deepScanProgress->setVisible(false);
    m_robotsCard->body()->addWidget(m_deepScanProgress);
    m_deepScanHint = DialogChrome::hintLabel(QString(), m_robotsCard);
    m_robotsCard->body()->addWidget(m_deepScanHint);
    m_rightColumn->addWidget(m_robotsCard);
    m_rightColumn->addStretch();

    m_projectsCard->setVisible(m_developer);
}

void HomePage::retranslate() {
    m_title->setText(tr("Welcome to TraceView"));
    m_subtitle->setText(tr("Version %1 · Pick up your dashboard or connect a robot.").arg(kVersion));
    m_statCaption[0]->setText(tr("Robots connected"));
    m_statCaption[1]->setText(tr("Workspaces"));
    m_statCaption[2]->setText(tr("Widgets"));
    m_projectCard->setTitle(tr("Continue where you left off"));
    m_openDashboardButton->setText(tr("Open dashboard"));
    m_workspacesCaption->setText(tr("Workspaces"));
    m_galleryCard->setTitle(tr("Gallery"));
    m_projectsCard->setTitle(tr("Projects"));
    m_recentCaption->setText(tr("Recent"));
    m_newButton->setText(tr("New project"));
    m_openButton->setText(tr("Open project..."));
    m_openLogButton->setText(tr("Open log..."));
    m_manageGalleryButton->setText(tr("Manage gallery..."));
    m_robotsCard->setTitle(tr("Nearby robots"));
    m_bluetoothButton->setText(tr("Scan Bluetooth"));
    refreshStats();
    refreshProjectCard();
    refreshProjectsCard();
    refreshGalleryCard();
    refreshRobots(/*force=*/true);
    refreshScanControls();
}

void HomePage::refreshIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const auto icon = [&palette](const char* id, const QColor& color) {
        return IconLibrary::instance().icon(QString::fromLatin1(id), color, kButtonIconSize);
    };
    // White on the filled accent of a "primary" button, like its label.
    m_openDashboardButton->setIcon(icon("lucide:layout-dashboard", Qt::white));
    refreshProjectIcon();
    m_newButton->setIcon(icon("lucide:file-plus", palette.textPrimary));
    m_openButton->setIcon(icon("lucide:folder-open", palette.textPrimary));
    m_openLogButton->setIcon(icon("lucide:scroll-text", palette.textPrimary));
    m_manageGalleryButton->setIcon(icon("lucide:images", palette.textPrimary));
    m_bluetoothButton->setIcon(icon("lucide:bluetooth", palette.textPrimary));
    m_deepScanButton->setIcon(icon("lucide:radar", palette.textPrimary));
}

void HomePage::refreshProjectIcon() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QString id = IconLibrary::instance().contains(m_project.icon)
                           ? m_project.icon
                           : QStringLiteral("lucide:layout-dashboard");
    m_projectIcon->setPixmap(IconLibrary::instance()
                                 .icon(id, palette.accent, kProjectIconGlyph)
                                 .pixmap(kProjectIconGlyph, kProjectIconGlyph));
}

void HomePage::setDeveloperMode(bool developer) {
    m_developer = developer;
    m_projectsCard->setVisible(developer);
    refreshProjectCard();
    refreshGalleryCard();
    refreshRobots(/*force=*/true);
}

void HomePage::setProjectSummary(const HomeProjectSummary& summary) {
    m_project = summary;
    refreshProjectIcon();
    refreshProjectCard();
    refreshStats();
}

void HomePage::setDevices(const QVector<HomeDevice>& devices) {
    m_devices = devices;
    refreshStats();
    refreshRobots();
}

void HomePage::setRecentProjects(const QStringList& paths) {
    if (paths.mid(0, kMaxRecent) == m_recent) {
        return;
    }
    m_recent = paths.mid(0, kMaxRecent);
    refreshProjectsCard();
}

void HomePage::setGallery(const QVector<GalleryEntryInfo>& entries, const QString& defaultName,
                          const QString& openPath) {
    const auto same = [](const GalleryEntryInfo& a, const GalleryEntryInfo& b) {
        return a.name == b.name && a.path == b.path && a.icon == b.icon &&
               a.iconIsCustom == b.iconIsCustom && a.workspaces == b.workspaces &&
               a.widgets == b.widgets;
    };
    if (defaultName == m_galleryDefault && openPath == m_galleryOpenPath &&
        std::equal(entries.begin(), entries.end(), m_gallery.begin(), m_gallery.end(), same)) {
        return;
    }
    m_gallery = entries;
    m_galleryDefault = defaultName;
    m_galleryOpenPath = openPath;
    refreshGalleryCard();
}

void HomePage::refreshStats() {
    int connected = 0;
    for (const HomeDevice& device : std::as_const(m_devices)) {
        if (device.connected) {
            ++connected;
        }
    }
    int widgets = 0;
    for (const HomeWorkspace& workspace : std::as_const(m_project.workspaces)) {
        widgets += workspace.widgets;
    }
    m_statValue[0]->setText(QStringLiteral("%1/%2").arg(connected).arg(m_devices.size()));
    m_statValue[1]->setText(QString::number(m_project.workspaces.size()));
    m_statValue[2]->setText(QString::number(widgets));
    m_statValue[0]->setProperty("tone", connected > 0 ? "success" : "");
    repolish(m_statValue[0]);
}

void HomePage::refreshProjectCard() {
    m_projectName->setFullText(m_project.title.isEmpty() ? tr("Untitled project")
                                                         : m_project.title);
    m_projectDetail->setText(tr("%n device(s)", nullptr, m_project.devices));
    // The file path only as a tooltip: a path has no spaces to wrap at, and
    // on a phone it would push the whole page wider than the screen.
    const bool onDisk = !m_project.path.isEmpty() && !m_project.path.startsWith(QLatin1Char(':'));
    if (m_developer && onDisk) {
        m_projectName->setToolTip(QDir::toNativeSeparators(m_project.path));
    }

    clearLayout(m_workspaceGrid);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QList<QWidget*> chips;
    for (const HomeWorkspace& workspace : std::as_const(m_project.workspaces)) {
        auto* chip = new QPushButton(workspace.name);
        chip->setObjectName(QStringLiteral("homeChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setToolTip(QStringLiteral("%1 · %2")
                             .arg(workspace.name, tr("%n widget(s)", nullptr, workspace.widgets)));
        const QString iconId = IconLibrary::instance().contains(workspace.icon)
                                   ? workspace.icon
                                   : QStringLiteral("lucide:layout-dashboard");
        chip->setIcon(IconLibrary::instance().icon(iconId, palette.accent, kRowIconSize));
        chip->setIconSize(QSize(kRowIconSize, kRowIconSize));
        // A long name is cut short (whole in the tooltip), never widens the
        // page past a phone's screen.
        chip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(chip, &QPushButton::clicked, this,
                [this, id = workspace.id] { emit openWorkspaceRequested(id); });
        chips.append(chip);
    }
    reflow(m_workspaceGrid, chips, m_compact ? 2 : 3);
    m_workspacesCaption->setVisible(m_project.workspaces.size() > 1);
    for (QWidget* chip : std::as_const(chips)) {
        chip->setVisible(m_project.workspaces.size() > 1);
    }
}

QPushButton* HomePage::listRow(const QString& iconId, const QString& text,
                               const QString& tooltip) {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    auto* button = new QPushButton(text);
    // A list row, not a framed button (stylesheet.cpp, "QPushButton#homeLink").
    button->setObjectName(QStringLiteral("homeLink"));
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip.isEmpty() ? text : tooltip);
    button->setIcon(IconLibrary::instance().icon(iconId, palette.textSecondary, kRowIconSize));
    button->setIconSize(QSize(kRowIconSize, kRowIconSize));
    button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    return button;
}

void HomePage::refreshProjectsCard() {
    clearLayout(m_recentList);
    // Two recent files of the same name (copies in different folders) say
    // which folder each one is in.
    QHash<QString, int> nameCount;
    for (const QString& path : std::as_const(m_recent)) {
        ++nameCount[QFileInfo(path).completeBaseName()];
    }
    for (const QString& path : std::as_const(m_recent)) {
        const QFileInfo info(path);
        QString text = info.completeBaseName();
        if (nameCount.value(text) > 1) {
            text += QStringLiteral("  —  ") + info.dir().dirName();
        }
        QPushButton* button = listRow(QStringLiteral("lucide:file"), text,
                                      QDir::toNativeSeparators(path));
        connect(button, &QPushButton::clicked, this, [this, path] { emit openPathRequested(path); });
        m_recentList->addWidget(button);
    }
    if (m_recent.isEmpty()) {
        m_recentList->addWidget(DialogChrome::hintLabel(tr("No recent projects yet."), nullptr));
    }
}

int HomePage::galleryColumns() const {
    if (m_compact) {
        return 1;
    }
    // Count from the width the gallery itself gets: on a wide page it sits
    // in the left column (3 parts of 5), and each card needs room for its
    // icon, name and the Open/Default badges side by side.
    int width = m_scroll->viewport()->width();
    if (m_wideState == 1) {
        width = width * 3 / 5;
    }
    return qBound(1, width / kGalleryCardMinWidth, 3);
}

void HomePage::refreshGalleryCard() {
    clearLayout(m_galleryGrid);
    m_galleryCard->setVisible(!m_gallery.isEmpty());
    m_galleryCount->setText(tr("%n dashboard(s)", nullptr, int(m_gallery.size())));
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QList<QWidget*> cards;
    for (const GalleryEntryInfo& entry : std::as_const(m_gallery)) {
        const QString name = entry.name;
        const bool builtIn = DashboardGallery::isBuiltInExample(entry.path);
        const bool isOpen = !entry.path.isEmpty() && entry.path == m_galleryOpenPath;
        const bool isDefault = name == m_galleryDefault;

        auto* card = new HomeGalleryCard(m_galleryCard);
        card->setProperty("current", isOpen);
        card->setToolTip(name);
        card->onActivate = [this, name] { emit openGalleryRequested(name); };
        auto* layout = new QHBoxLayout(card);
        layout->setContentsMargins(12, 12, 8, 12);
        layout->setSpacing(12);

        // Same tile as the Continue card's.
        auto* tile = new QLabel(card);
        tile->setObjectName(QStringLiteral("homeProjectIcon"));
        tile->setFixedSize(kGalleryIconSize, kGalleryIconSize);
        tile->setAlignment(Qt::AlignCenter);
        const QString iconId = IconLibrary::instance().contains(entry.icon)
                                   ? entry.icon
                                   : QStringLiteral("lucide:layout-dashboard");
        tile->setPixmap(IconLibrary::instance()
                            .icon(iconId, palette.accent, kGalleryIconGlyph)
                            .pixmap(kGalleryIconGlyph, kGalleryIconGlyph));
        layout->addWidget(tile, 0, Qt::AlignTop);

        auto* text = new QVBoxLayout();
        text->setSpacing(2);
        auto* title = new HomeNameLabel(name, card);
        QFont titleFont = title->font();
        titleFont.setWeight(QFont::DemiBold);
        title->setFont(titleFont);
        text->addWidget(title);
        text->addWidget(DialogChrome::hintLabel(
            tr("%n workspace(s)", nullptr, entry.workspaces) + QStringLiteral(" · ") +
                tr("%n widget(s)", nullptr, entry.widgets),
            card));
        if (isOpen || isDefault) {
            auto* badges = new QHBoxLayout();
            badges->setSpacing(6);
            if (isOpen) {
                auto* badge = new StatusBadge(card);
                badge->setStatus(tr("Open"), StatusBadge::Tone::Accent);
                badges->addWidget(badge);
            }
            if (isDefault) {
                auto* badge = new StatusBadge(card);
                badge->setStatus(tr("Default"), StatusBadge::Tone::Neutral);
                badges->addWidget(badge);
            }
            badges->addStretch();
            text->addLayout(badges);
        }
        layout->addLayout(text, 1);

        if (m_developer) {
            auto* menu = new QMenu(card);
            QAction* icon = menu->addAction(tr("Change icon…"));
            // Back to the first workspace's icon (or the generic glyph).
            if (entry.iconIsCustom && !builtIn) {
                QAction* automatic = menu->addAction(tr("Use automatic icon"));
                connect(automatic, &QAction::triggered, this,
                        [this, name] { emit resetGalleryIconRequested(name); });
            }
            QAction* rename = menu->addAction(tr("Rename…"));
            QAction* makeDefault = menu->addAction(tr("Set as default"));
            menu->addSeparator();
            QAction* remove = menu->addAction(tr("Delete…"));
            icon->setEnabled(!builtIn);
            rename->setEnabled(!builtIn);
            makeDefault->setEnabled(!isDefault);
            remove->setEnabled(!builtIn);
            connect(icon, &QAction::triggered, this,
                    [this, name] { emit changeGalleryIconRequested(name); });
            connect(rename, &QAction::triggered, this,
                    [this, name] { emit renameGalleryRequested(name); });
            connect(makeDefault, &QAction::triggered, this,
                    [this, name] { emit setDefaultGalleryRequested(name); });
            connect(remove, &QAction::triggered, this,
                    [this, name] { emit deleteGalleryRequested(name); });
            card->setMenu(menu);

            auto* more = new QToolButton(card);
            more->setObjectName(QStringLiteral("homeGalleryMenu"));
            more->setAutoRaise(true);
            more->setCursor(Qt::PointingHandCursor);
            more->setPopupMode(QToolButton::InstantPopup);
            more->setMenu(menu);
            more->setToolTip(tr("Dashboard options"));
            more->setIcon(IconLibrary::instance().icon(QStringLiteral("lucide:ellipsis"),
                                                       palette.textSecondary, kRowIconSize));
            more->setIconSize(QSize(kRowIconSize, kRowIconSize));
            layout->addWidget(more, 0, Qt::AlignTop);
        }
        cards.append(card);
    }
    m_galleryColumns = galleryColumns();
    reflow(m_galleryGrid, cards, m_galleryColumns);
}

QWidget* HomePage::robotRow(const QString& title, const QString& detail,
                            const QString& avatarIcon, const QStringList& linkIcons,
                            const QString& badgeText, int badgeTone, QPushButton* action) {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    auto* row = new QFrame();
    row->setObjectName(QStringLiteral("homeRobotRow"));
    row->setAttribute(Qt::WA_Hover);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(12);

    // A round avatar tinted by state (stylesheet.cpp, "QLabel#homeAvatar").
    const auto tone = StatusBadge::Tone(badgeTone);
    auto* avatar = new QLabel(row);
    avatar->setObjectName(QStringLiteral("homeAvatar"));
    avatar->setFixedSize(kAvatarSize, kAvatarSize);
    avatar->setAlignment(Qt::AlignCenter);
    QColor glyph = palette.textSecondary;
    const char* toneName = "neutral";
    if (tone == StatusBadge::Tone::Success) {
        glyph = palette.success;
        toneName = "success";
    } else if (tone == StatusBadge::Tone::Warning) {
        glyph = palette.warning;
        toneName = "warning";
    } else if (tone == StatusBadge::Tone::Accent) {
        glyph = palette.accent;
        toneName = "accent";
    }
    avatar->setProperty("tone", toneName);
    avatar->setPixmap(IconLibrary::instance()
                          .icon(avatarIcon, glyph, kAvatarIconSize)
                          .pixmap(kAvatarIconSize, kAvatarIconSize));
    layout->addWidget(avatar, 0, Qt::AlignTop);

    // Name, where it is, then the state -- stacked, so the row stays narrow
    // enough for a phone with the action button beside it.
    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* name = new HomeNameLabel(title, row);
    QFont nameFont = name->font();
    nameFont.setWeight(QFont::DemiBold);
    name->setFont(nameFont);
    text->addWidget(name);
    if (!detail.isEmpty() || !linkIcons.isEmpty()) {
        auto* where = new QHBoxLayout();
        where->setSpacing(4);
        for (const QString& id : linkIcons) {
            auto* icon = new QLabel(row);
            icon->setPixmap(IconLibrary::instance()
                                .icon(id, palette.textSecondary, kLinkIconSize)
                                .pixmap(kLinkIconSize, kLinkIconSize));
            where->addWidget(icon);
        }
        QLabel* detailLabel = DialogChrome::hintLabel(detail, row);
        where->addWidget(detailLabel, 1);
        text->addLayout(where);
    }
    if (!badgeText.isEmpty()) {
        auto* badge = new StatusBadge(row);
        badge->setStatus(badgeText, tone);
        auto* badgeRow = new QHBoxLayout();
        badgeRow->addWidget(badge);
        badgeRow->addStretch();
        text->addLayout(badgeRow);
    }
    layout->addLayout(text, 1);

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
        const QString detail = seen != nullptr ? linkSummary(*seen) : tr("In this project");
        m_robotsList->addWidget(robotRow(device.name, detail, QStringLiteral("lucide:bot"),
                                         seen != nullptr ? linkIconIds(*seen) : QStringList(),
                                         badge, int(tone), action));
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
        m_robotsList->addWidget(robotRow(
            title, linkSummary(robot),
            robot.dongle ? QStringLiteral("lucide:usb") : QStringLiteral("lucide:bot"),
            linkIconIds(robot), robot.dongle ? QString() : tr("New"),
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
    reflow(m_scanButtons, {m_bluetoothButton, m_deepScanButton}, m_compact ? 1 : 2);
}

void HomePage::applyWidth() {
    const int width = m_scroll->viewport()->width();
    const bool wide = width >= kTwoColumnMinWidth;
    const bool compact = width < kCompactMaxWidth;

    if (int(wide) != m_wideState) {
        m_wideState = int(wide);
        m_columns->setDirection(wide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        // One column (tablet, phone): the robots come right after
        // "Continue", before the project lists -- connecting is what a
        // phone is mostly used for.
        m_leftColumn->removeWidget(m_robotsCard);
        m_rightColumn->removeWidget(m_robotsCard);
        if (wide) {
            m_rightColumn->insertWidget(0, m_robotsCard);
        } else {
            m_leftColumn->insertWidget(1, m_robotsCard);
        }
    }

    if (compact != m_compact || m_projectButtons->isEmpty()) {
        m_compact = compact;
        // Tighter margins on a phone: every pixel of width counts there.
        m_scroll->widget()->layout()->setContentsMargins(compact ? 10 : 16, compact ? 12 : 16,
                                                         compact ? 10 : 16, 20);
        m_heroLayout->setContentsMargins(compact ? 14 : 20, compact ? 12 : 16, compact ? 14 : 20,
                                         compact ? 12 : 16);
        // Header: brand over the numbers on a phone.
        m_heroLayout->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        m_wordmark->setDisplayHeight(compact ? 40 : 52);
        for (QLabel* value : m_statValue) {
            value->setFont(scaledFont(font(), compact ? 1.35 : 1.7));
        }
        // "Continue": the button under the name, full width.
        m_continueRow->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        reflow(m_projectButtons,
               {m_newButton, m_openButton, m_openLogButton, m_manageGalleryButton},
               compact ? 1 : 2);
        refreshProjectCard();
        refreshScanControls();
    }
    // After m_compact is up to date: the cards per row depend on it.
    if (galleryColumns() != m_galleryColumns) {
        refreshGalleryCard();
    }
}

void HomePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    applyWidth();
    m_nearby->setActive(true);
}

void HomePage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    m_nearby->setActive(false);
}

void HomePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    applyWidth();
}

void HomePage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    } else if (event->type() == QEvent::FontChange) {
        m_title->setFont(scaledFont(font(), DialogChrome::kPageTitleScale));
        m_projectName->setFont(scaledFont(font(), DialogChrome::kPageTitleScale));
        for (QLabel* value : m_statValue) {
            value->setFont(scaledFont(font(), m_compact ? 1.35 : 1.7));
        }
    }
}

}  // namespace traceview
