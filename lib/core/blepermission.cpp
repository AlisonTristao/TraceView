#include "blepermission.h"

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QtGlobal>

#include <utility>

// Nested rather than one `&&`: on Qt < 6.5, QT_CONFIG(permissions) expands
// to a division by zero that some preprocessors reject even unevaluated.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#if QT_CONFIG(permissions)
#define TRACEVIEW_HAS_QT_PERMISSIONS 1
#endif
#endif

namespace traceview {

#ifdef TRACEVIEW_HAS_QT_PERMISSIONS

namespace {

struct PendingCaller {
    QPointer<QObject> context;
    std::function<void(bool)> onResult;
};

// Non-empty exactly while one requestPermission() is outstanding.
QList<PendingCaller>& pendingCallers() {
    static QList<PendingCaller> callers;
    return callers;
}

QBluetoothPermission centralPermission() {
    QBluetoothPermission permission;
    // Central role only (scan + connect). The default also asks for
    // Advertise, which Android maps to BLUETOOTH_ADVERTISE -- a permission
    // this app neither declares nor needs, so the request would fail.
    permission.setCommunicationModes(QBluetoothPermission::Access);
    return permission;
}

}  // namespace

void withBluetoothPermission(QObject* context, std::function<void(bool granted)> onResult) {
    const QBluetoothPermission permission = centralPermission();
    switch (qApp->checkPermission(permission)) {
        case Qt::PermissionStatus::Granted:
            onResult(true);
            return;
        case Qt::PermissionStatus::Denied:
            onResult(false);
            return;
        case Qt::PermissionStatus::Undetermined:
            break;
    }

    const bool requestAlreadyUp = !pendingCallers().isEmpty();
    pendingCallers().append(PendingCaller{context, std::move(onResult)});
    if (requestAlreadyUp) {
        return;
    }
    // qApp as the context, not the caller's: the answer has to reach every
    // joined caller even if the one that opened the prompt is gone by then.
    qApp->requestPermission(permission, qApp, [](const QPermission& answered) {
        const bool granted = answered.status() == Qt::PermissionStatus::Granted;
        const QList<PendingCaller> callers = std::exchange(pendingCallers(), {});
        for (const PendingCaller& caller : callers) {
            if (caller.context) {
                caller.onResult(granted);
            }
        }
    });
}

#else

void withBluetoothPermission(QObject* context, std::function<void(bool granted)> onResult) {
    Q_UNUSED(context);
    onResult(true);
}

#endif

}  // namespace traceview
