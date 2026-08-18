#include "widgetconfigeditor.h"

#include <QComboBox>
#include <QCoreApplication>

namespace traceview {

void populateDeviceCombo(QComboBox* combo, const QVector<DeviceOption>& devices) {
    const QString previousId = combo->currentData().toString();

    combo->clear();
    combo->addItem(QCoreApplication::translate("WidgetConfigEditor", "(No device)"), QString());
    for (const DeviceOption& device : devices) {
        combo->addItem(device.name.isEmpty() ? QCoreApplication::translate("WidgetConfigEditor", "(Unnamed device)")
                                              : device.name,
                        device.id);
    }

    const int idx = combo->findData(previousId);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

} // namespace traceview
