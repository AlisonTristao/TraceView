#pragma once

#include <QRegularExpression>
#include <QString>

namespace traceview {

// Text a robot sent, made fit to show as plain text: ANSI colour/cursor
// sequences removed (a robot's shell and ESP-IDF's log colour their output),
// including a sequence cut short where the robot truncated the message, and
// carriage returns dropped. Shown raw, ESC[33m reads as garbage in a label,
// the status bar, a notification card or an AI assistant's log. Widgets that
// render the colours themselves (the serial terminal, the robot log) take
// the text as it came.
inline QString plainText(const QString& text) {
    static const QRegularExpression ansi(QStringLiteral("\\x1B(\\[[0-9;?]*[A-Za-z]?)?"));
    QString clean = text;
    clean.remove(ansi);
    clean.remove(QLatin1Char('\r'));
    return clean;
}

}  // namespace traceview
