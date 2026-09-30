#include "core/keyboardreshowguard.h"

#include <QApplication>
#include <QEvent>
#include <QInputMethod>
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace traceview {

namespace {

// Longer than KeyboardInsetsDebounce's HIDE_DELAY_MS (250 ms), so a keyboard
// that really went away is already reported hidden by the time it's checked.
constexpr int kRecheckDelayMs = 400;

bool wantsKeyboard(const QWidget* widget) {
    return widget != nullptr && widget->testAttribute(Qt::WA_InputMethodEnabled) &&
           widget->inputMethodQuery(Qt::ImEnabled).toBool();
}

class KeyboardReshowGuard : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        // A key release is delivered synchronously once this returns (and
        // again to each parent it propagates to, by which point the flag is
        // already off), so turning it back on from the event loop covers the
        // whole delivery and nothing after it.
        if (event->type() == QEvent::KeyRelease && qApp->autoSipEnabled()) {
            qApp->setAutoSipEnabled(false);
            QTimer::singleShot(0, qApp, [] { qApp->setAutoSipEnabled(true); });
            scheduleRecheck(static_cast<QKeyEvent*>(event)->key());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    // The show() skipped above is also what used to bring the keyboard back
    // if something else had closed it on that key. Should that happen, the
    // field still has focus but the keyboard is gone for good -- so it's
    // asked for once more, as Qt would have, just not while it's up.
    // Enter/Back/Escape are left out: closing the keyboard is their job.
    static void scheduleRecheck(int key) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Back ||
            key == Qt::Key_Escape) {
            return;
        }
        QPointer<QWidget> field = QApplication::focusWidget();
        if (!wantsKeyboard(field)) {
            return;
        }
        QTimer::singleShot(kRecheckDelayMs, qApp, [field] {
            if (field && field == QApplication::focusWidget() && wantsKeyboard(field) &&
                !QGuiApplication::inputMethod()->isVisible()) {
                QGuiApplication::inputMethod()->show();
            }
        });
    }
};

}  // namespace

void installKeyboardReshowGuard() {
    qApp->installEventFilter(new KeyboardReshowGuard(qApp));
}

}  // namespace traceview
