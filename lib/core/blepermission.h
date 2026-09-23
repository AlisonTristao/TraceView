#pragma once

#include <functional>

class QObject;

namespace traceview {

// Calls `onResult(granted)` once the app may use Bluetooth LE, asking the
// user first if the platform wants that (Android 12+ BLUETOOTH_SCAN/
// BLUETOOTH_CONNECT, or location on Android 9-11; macOS and iOS TCC).
// Without this, Qt's Bluetooth backend fails every scan/connect on those
// platforms -- and on macOS/iOS touching CoreBluetooth before the user has
// answered is what the OS itself refuses.
//
// Desktop Linux/Windows have no Bluetooth permission to ask for: Qt reports
// Granted there and `onResult(true)` runs synchronously, before this
// returns, which is exactly the behavior BleDiscoveryService/BleTransport had
// before this existed. On Qt < 6.5 (no QPermission API -- Ubuntu 22.04's Qt
// 6.2, which the AppImage is built against) the same synchronous `true`.
//
// While a prompt is up, further calls (DeviceConnection's reconnect timer
// re-opening a BleTransport, a second scan) join that one prompt instead of
// stacking another dialog; every caller gets the same answer. `context`
// bounds each caller: if it is destroyed before the answer, its `onResult`
// is dropped. Callers still guard against their own state having moved on
// in the meantime (a stop()/close() while the prompt was up) -- see their
// generation counters.
void withBluetoothPermission(QObject* context, std::function<void(bool granted)> onResult);

}  // namespace traceview
