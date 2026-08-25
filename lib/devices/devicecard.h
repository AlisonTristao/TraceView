#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include "devices/device.h"

namespace traceview {

// Fixed footprint every DeviceCard is placed at (DevicesGrid setGeometry()s
// each card to exactly this) -- comfortably fits the header's icon/status
// dot/title/gear plus a body of comm-type label, a word-wrapped
// description, and a reported BTP version/ID line, without crowding.
// `inline` (C++17 inline variable) so this single definition is shared
// across every TU that includes this header, same as devicesgrid.cpp's
// layout math needing the same constant.
inline const QSize kDeviceCardSize(260, 140);

// One device's card, painted in the same procedural-QPainter style as
// DashboardCell's header chrome (see dashboard/dashboardcell.cpp:
// drawTypeIcon(), the connection-status dot, gearButtonRect()) but far
// simpler: fixed size, no drag/resize -- this card never moves once
// DevicesGrid places it. It can be selected (a plain click, no Ctrl-click
// multi-select), but stays a dumb presentation widget otherwise: both the
// gear and a plain click only report signals, they never open a menu/dialog
// or flip m_selected themselves -- DevicesGrid owns that (see
// devicesgrid.h).
class DeviceCard : public QWidget {
    Q_OBJECT

public:
    explicit DeviceCard(QWidget* parent = nullptr);

    void setDevice(const Device& device);
    const Device& device() const { return m_device; }

    // Selection is owned by DevicesGrid (single source of truth, same split
    // as DashboardCell/DashboardGrid) -- this just reflects it visually and
    // reports click intent via selectRequested().
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

signals:
    void configRequested(const QString& deviceId);
    // Emitted on a plain click anywhere on the card except the gear button.
    // DevicesGrid owns turning this into an actual selection change.
    void selectRequested(const QString& deviceId);
    // Emitted on a click on the status dot -- lets a device be connected/
    // disconnected without opening DeviceConfigDialog. Purely a report, same
    // as configRequested/selectRequested: this card never flips its own dot
    // color from this, the real connection state driving it comes back
    // through setDevice().
    void connectToggleRequested(const QString& deviceId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QRect headerRect() const;
    // Same right-aligned corner placement math as DashboardCell::gearButtonRect().
    QRect gearButtonRect() const;
    // Fixed geometry (doesn't depend on paint-time text layout) so both
    // paintEvent() and mousePressEvent() share one source of truth.
    QRect statusDotRect() const;

    Device m_device;
    bool m_selected = false;
};

} // namespace traceview
