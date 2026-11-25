#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QIcon>
#include <QPointer>
#include <QStyleFactory>
#include <QTimer>
#include <cstring>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

#include "agent/agentbridgeserver.h"
#include "agent/mcpstdiorelay.h"
#include "core/applog.h"
#include "core/mainwindow.h"
#include "core/touchscroll.h"
#include "crash/crashhandler.h"
#include "crash/crashreportdialog.h"
#include "crash/sessionmarker.h"
#include "crash/splashscreen.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace {

// QLineEdit, QTextEdit and QPlainTextEdit ask for the input panel again on
// every key *release* (keyReleaseEvent -> handleSoftwareInputPanel ->
// QInputMethod::show()). On Android that re-runs Qt's whole "show keyboard"
// sequence, which takes over the IME insets animation and plays the show
// animation again -- so an already open keyboard drops and slides back up.
// Letters never get there (a soft keyboard commits them as text, with no key
// event), but a keyboard that sends Backspace as a real key does, on every
// delete. A key release never has a reason to open the panel -- it comes
// either from the panel itself, already up, or from a hardware keyboard --
// so auto-SIP is switched off for the duration of that one event.
//
// This deliberately does not ask QInputMethod::isVisible() whether the panel
// is up: on Android that answer sticks at "hidden" after any hide request
// that does not end up closing the keyboard (QtInputDelegate's
// m_isKeyboardHidingAnimationOngoing is only cleared by a visibility
// *change*) -- e.g. leaving a password field, where the IME swaps its secure
// keyboard for the normal one without ever going away.
class Application : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override {
        if (event->type() != QEvent::KeyRelease || !autoSipEnabled()) {
            return QApplication::notify(receiver, event);
        }
        setAutoSipEnabled(false);
        const bool handled = QApplication::notify(receiver, event);
        setAutoSipEnabled(true);
        return handled;
    }
};

QIcon loadAppIcon() {
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/app_%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

}  // namespace

int main(int argc, char* argv[]) {
    // An MCP client launched us as its stdio server: relay to the running app
    // (see agent/mcpstdiorelay.h) -- no window, no log file, no QApplication.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], traceview::kMcpRelayArgument) == 0) {
            return traceview::runMcpStdioRelay(argc, argv);
        }
    }

    // Fusion draws every widget itself instead of asking the native theme
    // to -- this app already paints its whole look on top via ThemeManager's
    // QSS (theme/stylesheet.cpp), and anything that QSS doesn't cover (e.g.
    // QPlainTextEdit's frame, see SerialTerminalWidget) previously fell back
    // to whatever the OS/Qt-build's own native style drew by default, which
    // is exactly what made the same widget look different depending on
    // which Qt distribution compiled the app (the official installer's kit
    // locally vs. MSYS2's Qt6 packages in CI). Fusion has no OS/build
    // dependency at all, so every gap in the QSS now renders identically
    // everywhere instead of silently drifting.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    Application app(argc, argv);
    QApplication::setApplicationName("TraceView");
#if defined(Q_OS_LINUX)
    QApplication::setDesktopFileName("io.github.alisontristao.TraceView");
#endif
    QApplication::setOrganizationName("AlisonTristao");
    QApplication::setApplicationVersion(traceview::kVersion);
    QApplication::setWindowIcon(loadAppIcon());

    // After setApplicationName/setOrganizationName (logDirectory() resolves
    // against them) and before MainWindow, so every device connection it
    // creates logs through an already-open file.
    traceview::AppLog::install();

    // Right after the log (the marker records its path, and the crash
    // handler writes next to it): did the last run quit cleanly? A crash is
    // reported by the dialog below, once MainWindow is up -- never inside the
    // crash itself.
    auto& sessionMarker = traceview::SessionMarker::instance();
    sessionMarker.begin();
    traceview::CrashHandler::install(sessionMarker.currentCrashDirectory());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        traceview::SessionMarker::instance().end();
    });

    // Language first: ThemeManager/FontManager build their display names
    // (QCoreApplication::translate) in their constructors, so the translator
    // has to be installed before those singletons are first touched.
    traceview::LanguageManager::instance().applyCurrentLanguage();
    traceview::ThemeManager::instance().applyCurrentTheme();
    traceview::FontManager::instance().applyCurrentFont();

    // Before MainWindow: its scroll areas are polished (and so hooked) on
    // first show, which has to find the filter already installed.
    traceview::installTouchScrolling();

    // After the theme and language, so it is drawn in the user's look; before
    // MainWindow, whose construction is the slow part of startup.
    auto* splash = new traceview::SplashScreen(sessionMarker.previousSession().updatedFrom);
    splash->show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    traceview::MainWindow window;
    window.show();
    splash->finish(&window);

    if (sessionMarker.previousSessionCrashed()) {
        QTimer::singleShot(0, &window, [&window, &sessionMarker] {
            traceview::CrashReportDialog dialog(
                traceview::CrashReport::fromCrashedSession(sessionMarker.previousSession()),
                &window);
            dialog.exec();
        });
    }

#if defined(Q_OS_ANDROID)
    // Keeps the soft keyboard from dropping and reopening when an IME
    // briefly reports itself hidden while resizing (see the Java class).
    QJniObject::callStaticMethod<void>(
        "io/github/alisontristao/traceview/KeyboardInsetsDebounce", "install",
        "(Landroid/app/Activity;)V",
        QNativeInterface::QAndroidApplication::context().object<jobject>());

    // Locking the screen destroys the Activity's surface; the one Android
    // hands back on unlock starts out empty and stayed black for good, since
    // nothing asked the widgets to paint into it again. Floating panels
    // (PanelDockController's Qt::Tool top-levels) each own a surface of
    // their own too, and one coming back blank on top of the main window
    // blacks it out the same way -- so they're hidden while suspended and
    // shown again once the main surface is back.
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, &window,
                     [&window](Qt::ApplicationState state) {
                         static QList<QPointer<QWidget>> hiddenPanels;
                         if (state == Qt::ApplicationSuspended) {
                             for (QWidget* w : QApplication::topLevelWidgets()) {
                                 if (w != &window && w->isVisible() &&
                                     w->windowType() == Qt::Tool &&
                                     !qobject_cast<QDialog*>(w)) {
                                     hiddenPanels.append(w);
                                     w->hide();
                                 }
                             }
                         } else if (state == Qt::ApplicationActive) {
                             for (const QPointer<QWidget>& w : std::as_const(hiddenPanels)) {
                                 if (w) {
                                     w->show();
                                 }
                             }
                             hiddenPanels.clear();
                             for (QWidget* w : QApplication::topLevelWidgets()) {
                                 if (w->isVisible()) {
                                     w->update();
                                 }
                             }
                         }
                     });
#endif

    return QApplication::exec();
}
