#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

#include "core/applog.h"
#include "core/mainwindow.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace {

QIcon loadAppIcon() {
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/app_%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

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

    QApplication app(argc, argv);
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

    // Language first: ThemeManager/FontManager build their display names
    // (QCoreApplication::translate) in their constructors, so the translator
    // has to be installed before those singletons are first touched.
    traceview::LanguageManager::instance().applyCurrentLanguage();
    traceview::ThemeManager::instance().applyCurrentTheme();
    traceview::FontManager::instance().applyCurrentFont();

    traceview::MainWindow window;
    window.show();

    return QApplication::exec();
}
