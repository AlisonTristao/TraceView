#pragma once

class QString;
class QWidget;

namespace traceview {

// Temporary on-screen trace of soft-keyboard traffic (focus changes, input
// panel show/hide, keyboard geometry, key and input-method events), drawn
// as a click-through overlay on `host`. Exists to diagnose the Android
// keyboard dropping and reappearing on Backspace without adb: reproduce,
// screenshot, read the trace. Developer mode only (see MainWindow).
void setInputDiagnosticsEnabled(QWidget* host, bool enabled);

// Every line traced since the overlay was turned on (up to a few hundred,
// oldest first), for copying out -- the overlay itself only fits the last
// couple dozen and can't scroll, since it must not take touches from the
// keyboard being traced. Empty while the overlay is off.
QString inputDiagnosticsLog();

}  // namespace traceview
