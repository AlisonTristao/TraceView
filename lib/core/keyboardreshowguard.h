#pragma once

namespace traceview {

// Keeps an editable text field from asking for the soft keyboard again on
// every key release. QLineEdit/QTextEdit/QPlainTextEdit::keyReleaseEvent()
// call QInputMethod::show() whenever QApplication::autoSipEnabled() is on,
// and on Android that re-runs Qt's whole show sequence (refocus the hidden
// QtEditText, take control of the IME insets animation, show the IME) even
// with the keyboard already up. Typed letters arrive as IME text and never
// get there, but Backspace and Shift/Caps Lock arrive as raw key events, so
// the keyboard visibly dropped and came back on each of those (seen on a
// Xiaomi phone). While a key release is delivered autoSipEnabled is off;
// a tap on a field still opens the keyboard. That skipped show() used to be
// what reopened the keyboard if anything else closed it on that key, so a
// moment later a focused field whose keyboard is gone asks for it again --
// at worst the old drop-and-return, never a keyboard that stays gone.
//
// Installed once from main() on Android, after the QApplication exists.
void installKeyboardReshowGuard();

}  // namespace traceview
