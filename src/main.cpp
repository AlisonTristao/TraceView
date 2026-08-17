#include <QApplication>
#include <QIcon>

#include "core/mainwindow.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

namespace {

QIcon loadAppIcon() {
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/app_%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("TraceView");
    QApplication::setOrganizationName("AlisonTristao");
    QApplication::setWindowIcon(loadAppIcon());

    traceview::ThemeManager::instance().applyCurrentTheme();
    traceview::FontManager::instance().applyCurrentFont();
    traceview::LanguageManager::instance().applyCurrentLanguage();

    traceview::MainWindow window;
    window.show();

    return QApplication::exec();
}
