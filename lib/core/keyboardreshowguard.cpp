#include "core/keyboardreshowguard.h"

#include <QApplication>
#include <QEvent>
#include <QTimer>

namespace traceview {

namespace {

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
        }
        return QObject::eventFilter(watched, event);
    }
};

}  // namespace

void installKeyboardReshowGuard() {
    qApp->installEventFilter(new KeyboardReshowGuard(qApp));
}

}  // namespace traceview
