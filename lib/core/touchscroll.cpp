#include "core/touchscroll.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScroller>
#include <QScrollerProperties>

namespace traceview {

namespace {

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
constexpr bool kTouchPlatform = true;
#else
constexpr bool kTouchPlatform = false;
#endif

// Set by setTouchScroll(): the app-wide hook skips a scroll area carrying it
// (its owner decides when a drag scrolls), even if Qt polishes it later.
constexpr const char* kManualProperty = "traceviewTouchScrollManual";

void grab(QAbstractScrollArea* scrollArea) {
    QWidget* viewport = scrollArea->viewport();
    // LeftMouseButtonGesture, not TouchGesture: Android/iOS hand a widget
    // app touches that nothing accepted as synthesized mouse events, and the
    // children filling a viewport (list rows, dashboard widgets, form rows)
    // are what receive them -- a TouchGesture on the viewport itself mostly
    // never saw the drag at all. The gesture manager does watch mouse events
    // bound for a viewport's children, and cancels the child's press once
    // the finger has moved far enough to count as a scroll, so a tap still
    // clicks.
    QScroller::grabGesture(viewport, QScroller::LeftMouseButtonGesture);

    QScrollerProperties props = QScroller::scroller(viewport)->scrollerProperties();
    // No rubber-band past the ends: Qt Widgets paint the overshoot as the
    // whole page sliding away from its frame, which reads as a glitch here.
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    QScroller::scroller(viewport)->setScrollerProperties(props);

    // Item views scroll a whole row per step by default, so a finger drag
    // would move in row-sized jumps instead of following the finger.
    if (auto* view = qobject_cast<QAbstractItemView*>(scrollArea)) {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
}

class TouchScrollFilter : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override {
        // Polish arrives once per widget, right before it's first shown --
        // late enough that the viewport exists, early enough that the very
        // first drag already scrolls.
        if (event->type() == QEvent::Polish) {
            auto* scrollArea = qobject_cast<QAbstractScrollArea*>(watched);
            if (scrollArea != nullptr && !scrollArea->property(kManualProperty).toBool()) {
                grab(scrollArea);
            }
            // Android keyboards keep a "composing" word while predictive text
            // is on, and Qt restarts the input connection when a Backspace
            // edits it -- the keyboard visibly drops and comes back on every
            // delete. Without predictions there is nothing composing, so
            // deletes are plain key events. SerialTerminalWidget (a
            // QPlainTextEdit) answers ImHints itself, so this is moot there.
            if (qobject_cast<QLineEdit*>(watched) != nullptr ||
                qobject_cast<QPlainTextEdit*>(watched) != nullptr) {
                auto* widget = static_cast<QWidget*>(watched);
                widget->setInputMethodHints(widget->inputMethodHints() |
                                            Qt::ImhNoPredictiveText);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

}  // namespace

void installTouchScrolling() {
    if constexpr (kTouchPlatform) {
        qApp->installEventFilter(new TouchScrollFilter(qApp));
    }
}

void setTouchScroll(QAbstractScrollArea* scrollArea, bool enabled) {
    if constexpr (kTouchPlatform) {
        scrollArea->setProperty(kManualProperty, true);
        if (enabled) {
            grab(scrollArea);
        } else {
            QScroller::ungrabGesture(scrollArea->viewport());
        }
    } else {
        Q_UNUSED(scrollArea);
        Q_UNUSED(enabled);
    }
}

}  // namespace traceview
