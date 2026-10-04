#include <QApplication>
#include <QDialog>
#include <QIcon>
#include <QPointer>
#include <QStyleFactory>
#include <memory>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#endif

#include <QSettings>
#include <QThread>
#include <QTimer>

#include "dashboard/perfprobe.h"
#include "core/applog.h"
#include "core/mainwindow.h"
#include "core/touchscroll.h"
#include "modules/moduleregistry.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

#if defined(TRACEVIEW_ENABLE_LAB)
#include "lab/module/labmodule.h"
#endif

namespace {

QIcon loadAppIcon() {
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/app_%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

// TEMP perf harness: times every top-level event by type and receiver.
class PerfApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        if (perf::enabled() && event->type() == QEvent::Paint &&
            QThread::currentThread() == thread()) {
            const QByteArray key = QByteArray("paint:") + receiver->metaObject()->className();
            const qint64 start = perf::nowNs();
            ++m_depth;
            const bool result = QApplication::notify(receiver, event);
            --m_depth;
            perf::add(key.constData(), perf::nowNs() - start);
            return result;
        }
        if (!perf::enabled() || m_depth > 0 || QThread::currentThread() != thread()) {
            return QApplication::notify(receiver, event);
        }
        QByteArray key = "ev." + QByteArray::number(int(event->type())) + ":" +
                         receiver->metaObject()->className();
        if (receiver->parent() != nullptr &&
            (event->type() == QEvent::Timer || qstrcmp(receiver->metaObject()->className(), "QTimer") == 0)) {
            key += "<" + QByteArray(receiver->parent()->metaObject()->className());
        }
        ++m_depth;
        const qint64 start = perf::nowNs();
        const bool result = QApplication::notify(receiver, event);
        const qint64 ns = perf::nowNs() - start;
        --m_depth;
        perf::add(key.constData(), ns);
        return result;
    }

private:
    int m_depth = 0;
};

}  // namespace

int main(int argc, char* argv[]) {
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

    // Qt's raster engine splits every fill of more than ~96 spans (a grid
    // line, a glyph run, a card background) into pieces for its own shared
    // GUI thread pool, behind one global mutex. The dashboard already renders
    // each chart and scene on a thread of its own, so those renders queued up
    // on that mutex and pool instead: about half of a line chart's render
    // was spent waiting there. Read once, on the first fill, so it has to be
    // set before anything paints; a value the user set is left alone.
    if (!qEnvironmentVariableIsSet("QT_NO_GUI_THREADPOOL")) {
        qputenv("QT_NO_GUI_THREADPOOL", "1");
    }

    PerfApplication app(argc, argv);
    QApplication::setApplicationName("TraceView");
#if defined(Q_OS_LINUX)
    QApplication::setDesktopFileName("io.github.alisontristao.TraceView");
#endif
    QApplication::setOrganizationName("AlisonTristao");
    QApplication::setApplicationVersion(traceview::kVersion);
    QApplication::setWindowIcon(loadAppIcon());

    // TEMP perf harness (not for commit).
    if (!qEnvironmentVariableIsEmpty("TRACEVIEW_PERF_SETTINGS_DIR")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           qEnvironmentVariable("TRACEVIEW_PERF_SETTINGS_DIR"));
    }

    // After setApplicationName/setOrganizationName (logDirectory() resolves
    // against them) and before MainWindow, so every device connection it
    // creates logs through an already-open file.
    traceview::AppLog::install();

    // Language first: ThemeManager/FontManager build their display names
    // (QCoreApplication::translate) in their constructors, so the translator
    // has to be installed before those singletons are first touched.
    traceview::LanguageManager::instance().applyCurrentLanguage();
    traceview::ThemeManager::instance().applyCurrentTheme();
    traceview::FontManager::instance().applyCurrentFont();

    // Before MainWindow: its scroll areas are polished (and so hooked) on
    // first show, which has to find the filter already installed.
    traceview::installTouchScrolling();

    // Optional modules (see docs/MODULES.md) register here, before --mode is
    // read and before MainWindow installs the enabled ones. Each is compiled
    // in behind its own CMake option.
#if defined(TRACEVIEW_ENABLE_LAB)
    traceview::ModuleRegistry::instance().registerModule(std::make_unique<tracelab::LabModule>());
#endif
    traceview::ModuleRegistry::instance().applyModeArgument(QCoreApplication::arguments());

    traceview::MainWindow window;
    window.show();

    // TEMP perf harness (not for commit).
    if (perf::enabled()) {
        perf::install();
        window.resize(qEnvironmentVariableIntValue("TRACEVIEW_PERF_W") > 0
                          ? qEnvironmentVariableIntValue("TRACEVIEW_PERF_W")
                          : 1920,
                      qEnvironmentVariableIntValue("TRACEVIEW_PERF_H") > 0
                          ? qEnvironmentVariableIntValue("TRACEVIEW_PERF_H")
                          : 1080);
        const QString project = qEnvironmentVariable("TRACEVIEW_PERF_PROJECT");
        if (!project.isEmpty()) {
            QTimer::singleShot(1000, &window, [&window, project] {
                ((traceview::ModuleHost*)&window)->openProject(project);
            });
        }
        const int seconds = qMax(1, qEnvironmentVariableIntValue("TRACEVIEW_PERF_SECONDS"));
        QTimer::singleShot(seconds * 1000, qApp, [&window] {
            const QString shot = qEnvironmentVariable("TRACEVIEW_PERF_SHOT");
            if (!shot.isEmpty()) {
                window.grab().save(shot);
            }
            QCoreApplication::exit(0);
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
