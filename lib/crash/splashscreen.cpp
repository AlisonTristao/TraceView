#include "crash/splashscreen.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace traceview {

namespace {
constexpr int kCardWidth = 360;
constexpr int kCardHeight = 220;
constexpr int kIconSize = 56;
}  // namespace

SplashScreen::SplashScreen(const QString& updatedFrom, QWidget* parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("dialogCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAutoFillBackground(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(8);

    auto* icon = new QLabel(this);
    icon->setAlignment(Qt::AlignCenter);
    const QIcon appIcon = QApplication::windowIcon();
    if (!appIcon.isNull()) {
        icon->setPixmap(appIcon.pixmap(kIconSize, kIconSize));
    }
    layout->addWidget(icon);

    auto* title = new QLabel(QStringLiteral("TraceView"), this);
    title->setObjectName(QStringLiteral("dialogTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* version = new QLabel(QCoreApplication::applicationVersion(), this);
    version->setObjectName(QStringLiteral("dialogSubtitle"));
    version->setAlignment(Qt::AlignCenter);
    layout->addWidget(version);

    if (!updatedFrom.isEmpty()) {
        auto* updated = new QLabel(tr("Updated from v%1 to v%2")
                                       .arg(updatedFrom, QCoreApplication::applicationVersion()),
                                   this);
        updated->setObjectName(QStringLiteral("dialogHint"));
        updated->setAlignment(Qt::AlignCenter);
        layout->addWidget(updated);
    }

    layout->addStretch(1);

    auto* bar = new QProgressBar(this);
    bar->setRange(0, 0);
    bar->setTextVisible(false);
    layout->addWidget(bar);

    auto* message = new QLabel(tr("Loading…"), this);
    message->setObjectName(QStringLiteral("splashMessage"));
    message->setAlignment(Qt::AlignCenter);
    layout->addWidget(message);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    setWindowState(Qt::WindowFullScreen);
#else
    resize(kCardWidth, kCardHeight);
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        move(screen->availableGeometry().center() - rect().center());
    }
#endif
}

void SplashScreen::setMessage(const QString& message) {
    if (auto* label = findChild<QLabel*>(QStringLiteral("splashMessage"))) {
        label->setText(message);
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashScreen::finish(QWidget* window) {
    if (window) {
        window->show();
    }
    QTimer::singleShot(0, this, &QWidget::close);
}

}  // namespace traceview
