#pragma once

#include <QColor>
#include <QString>

namespace traceview {

// The three BTP output channels every device block exposes (see
// btpbackend.h / commandclient.h / telemetryfieldrouter.h): command
// (COMMAND_REQUEST/RESULT), terminal (console byte stream, TERMINAL_IN/OUT)
// and telemetry (subscribed field samples). A connection may only join two
// ports of the same kind -- DiagramScene enforces this when a drag is
// released on a candidate target.
enum class DiagramPortKind { Command, Terminal, Telemetry };

// Every port comes in an Output (this block produces this kind of traffic)
// and Input (this block accepts it from elsewhere) flavor. A connection
// always runs Output -> Input; DiagramScene rejects Output-to-Output and
// Input-to-Input drags, and a block cannot connect to itself.
enum class DiagramPortDirection { Output, Input };

// Shared by DiagramPortItem (fill color) and DiagramConnectionItem (stroke
// color) so a connection always reads as the same color as the two ports it
// joins.
inline QColor diagramPortKindColor(DiagramPortKind kind) {
    switch (kind) {
        case DiagramPortKind::Command:
            return QColor(0xE0, 0x7A, 0x2E);   // orange
        case DiagramPortKind::Terminal:
            return QColor(0x4A, 0x90, 0xD9);   // blue
        case DiagramPortKind::Telemetry:
            return QColor(0x4C, 0xAF, 0x50);   // green
    }
    return QColor(Qt::gray);
}

inline const char* diagramPortKindLabel(DiagramPortKind kind) {
    switch (kind) {
        case DiagramPortKind::Command:
            return "Command";
        case DiagramPortKind::Terminal:
            return "Terminal";
        case DiagramPortKind::Telemetry:
            return "Telemetry";
    }
    return "";
}

// Reverse of diagramPortKindLabel(), for reading a saved .tvproj connection
// back -- DiagramPage::fromJson() is the only caller. Returns false (leaving
// *kind untouched) for anything else, so a connection referencing a kind
// this build no longer knows is just skipped rather than misread as
// Command (kind 0).
inline bool diagramPortKindFromLabel(const QString& label, DiagramPortKind* kind) {
    if (label == QLatin1String("Command")) {
        *kind = DiagramPortKind::Command;
    } else if (label == QLatin1String("Terminal")) {
        *kind = DiagramPortKind::Terminal;
    } else if (label == QLatin1String("Telemetry")) {
        *kind = DiagramPortKind::Telemetry;
    } else {
        return false;
    }
    return true;
}

}  // namespace traceview
