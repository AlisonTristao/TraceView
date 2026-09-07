#include <QApplication>
#include <QIcon>

#include "core/mainwindow.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <windows.h>
#endif

namespace {

QIcon loadAppIcon() {
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/icons/app_%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

#ifdef Q_OS_WIN
// Some external process (an antivirus/EDR agent, a screen reader, or a UI
// automation tool) sends WM_GETOBJECT to query this window's accessibility
// tree. The first one received flips Qt's own
// QWindowsUiaAccessibility::m_accessibleActive latch for the rest of the
// process's life, and a Qt bug then crashes inside
// QAccessibleCache::idForObject() the next time a QComboBox closes its
// popup (reproduced: adding a Serial Monitor terminal tab and picking a
// device from its combo box). Discarding WM_GETOBJECT here, before Qt's
// Windows platform plugin ever sees it (QWindowsContext::filterNativeEvent()
// consults native event filters first), means the UI Automation bridge
// never activates and the bug never triggers -- at the cost of a real
// screen reader not being able to read this app, which nobody currently
// needs it for.
class BlockAccessibilityQueryFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override {
        if (eventType != "windows_generic_MSG") {
            return false;
        }
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_GETOBJECT) {
            if (result != nullptr) {
                *result = 0;
            }
            return true;
        }
        return false;
    }
};
#endif  // Q_OS_WIN

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("TraceView");
    QApplication::setOrganizationName("AlisonTristao");
    QApplication::setWindowIcon(loadAppIcon());

#ifdef Q_OS_WIN
    BlockAccessibilityQueryFilter accessibilityFilter;
    app.installNativeEventFilter(&accessibilityFilter);
#endif

    traceview::ThemeManager::instance().applyCurrentTheme();
    traceview::FontManager::instance().applyCurrentFont();
    traceview::LanguageManager::instance().applyCurrentLanguage();

    traceview::MainWindow window;
    window.show();

    return QApplication::exec();
}
