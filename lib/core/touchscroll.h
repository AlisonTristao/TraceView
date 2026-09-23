#pragma once

class QAbstractScrollArea;

namespace traceview {

// Finger-drag ("kinetic") scrolling for every scroll area on a touch
// platform. Qt Widgets only scroll a QAbstractScrollArea through its
// scrollbar or a mouse wheel on their own -- on Android/iOS that left every
// list, settings page and dialog scrollable only by grabbing the thin
// scrollbar. Both functions are no-ops on desktop.
//
// installTouchScrolling() hooks the whole app once (from main(), after the
// QApplication exists): every QAbstractScrollArea gets its viewport wired to
// a QScroller the moment Qt polishes it, so new dialogs/lists pick it up
// without each one opting in.
void installTouchScrolling();

// For a scroll area whose drag gesture sometimes has to mean something else
// (the dashboard in edit mode, where a drag moves/resizes a widget): the
// app-wide hook leaves it alone from the first call on, and the caller turns
// scrolling on/off itself.
void setTouchScroll(QAbstractScrollArea* scrollArea, bool enabled);

}  // namespace traceview
