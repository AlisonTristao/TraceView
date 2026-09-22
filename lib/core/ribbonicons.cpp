#include "ribbonicons.h"

#include "theme/iconutils.h"

namespace traceview {

// Icons now come from resources/icons/ui.qrc (monochrome SVGs) tinted to the
// requested color at load time -- see theme/iconutils.h -- instead of being
// hand-drawn with QPainter here.

QIcon makePlusIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/plus.svg", color, kRibbonIconSize);
}

QIcon makeMinusIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/minus.svg", color, kRibbonIconSize);
}

QIcon makeArrowIcon(const QColor& color, bool pointingLeft) {
    return loadTintedIcon(
        pointingLeft ? ":/icons/ribbon/arrow-left.svg" : ":/icons/ribbon/arrow-right.svg", color,
        kRibbonIconSize);
}

QIcon makeCopyIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/copy.svg", color, kRibbonIconSize);
}

QIcon makePasteIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/paste.svg", color, kRibbonIconSize);
}

QIcon makeFullscreenIcon(const QColor& color, bool active) {
    return loadTintedIcon(
        active ? ":/icons/ribbon/fullscreen-exit.svg" : ":/icons/ribbon/fullscreen-enter.svg",
        color, kRibbonIconSize);
}

QIcon makeBringToFrontIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/bring-to-front.svg", color, kRibbonIconSize);
}

QIcon makeBringForwardIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/bring-forward.svg", color, kRibbonIconSize);
}

QIcon makeSendBackwardIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/send-backward.svg", color, kRibbonIconSize);
}

QIcon makeSendToBackIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/send-to-back.svg", color, kRibbonIconSize);
}

QIcon makeGroupIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/group.svg", color, kRibbonIconSize);
}

QIcon makeUngroupIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/ungroup.svg", color, kRibbonIconSize);
}

QIcon makeLockIcon(const QColor& color, bool locked) {
    return loadTintedIcon(
        locked ? ":/icons/ribbon/lock-locked.svg" : ":/icons/ribbon/lock-unlocked.svg", color,
        kRibbonIconSize);
}

QIcon makePanelsIcon(const QColor& color, bool visible) {
    return loadTintedIcon(
        visible ? ":/icons/ribbon/panels-visible.svg" : ":/icons/ribbon/panels-hidden.svg", color,
        kRibbonIconSize);
}

QIcon makeWorkspaceIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/workspace.svg", color, kRibbonIconSize);
}

QIcon makeTrashIcon(const QColor& color, int size) {
    return loadTintedIcon(":/icons/ribbon/trash.svg", color, size);
}

QIcon makeFolderIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/folder.svg", color, kRibbonIconSize);
}

QIcon makePhoneIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/screen-phone.svg", color, kRibbonIconSize);
}

QIcon makeTabletIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/screen-tablet.svg", color, kRibbonIconSize);
}

QIcon makeNotebookIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/screen-notebook.svg", color, kRibbonIconSize);
}

QIcon makeOptionsIcon(const QColor& color) {
    return loadTintedIcon(":/icons/ribbon/options.svg", color, kRibbonIconSize);
}

}  // namespace traceview
