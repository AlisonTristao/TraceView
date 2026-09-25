#pragma once

class QWidget;

namespace traceview {

// Temporary on-screen trace of soft-keyboard traffic (focus changes, input
// panel show/hide, keyboard geometry, key and input-method events), drawn
// as a click-through overlay on `host`. Exists to diagnose the Android
// keyboard dropping and reappearing on Backspace without adb: reproduce,
// screenshot, read the trace. Developer mode only (see MainWindow).
void setInputDiagnosticsEnabled(QWidget* host, bool enabled);

}  // namespace traceview
