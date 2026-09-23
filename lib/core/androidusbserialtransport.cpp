#include "androidusbserialtransport.h"

#include <QCoreApplication>
#include <QHash>
#include <QJniEnvironment>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include "core/applog.h"
#include "diagnostics/hexdump.h"
#include "preferences/appsettings.h"

namespace traceview {

namespace {

constexpr const char* kBridgeClass = "io/github/alisontristao/traceview/UsbSerialBridge";

// Live transports by the handle their Java bridge carries. The natives run on
// Java threads, so every lookup -- and the queued call it posts -- happens
// under this mutex; the destructor unregisters under it too, which is what
// makes posting to a looked-up pointer safe.
QMutex& registryMutex() {
    static QMutex mutex;
    return mutex;
}

QHash<qint64, AndroidUsbSerialTransport*>& registry() {
    static QHash<qint64, AndroidUsbSerialTransport*> transports;
    return transports;
}

qint64 nextHandle() {
    static qint64 handle = 0;
    return ++handle;
}

QString fromJString(JNIEnv* env, jstring string) {
    if (string == nullptr) {
        return QString();
    }
    const char* chars = env->GetStringUTFChars(string, nullptr);
    const QString result = QString::fromUtf8(chars);
    env->ReleaseStringUTFChars(string, chars);
    return result;
}

QJniObject androidContext() {
    return QNativeInterface::QAndroidApplication::context();
}

}  // namespace

// Static JNI entry points, one per UsbSerialBridge `native` method. Friend of
// the transport so each one can hand its payload to the private handler on
// the transport's own thread.
struct AndroidUsbSerialNatives {
    template <typename Call>
    static void post(jlong handle, Call call) {
        QMutexLocker lock(&registryMutex());
        AndroidUsbSerialTransport* transport = registry().value(handle);
        if (transport == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(
            transport, [transport, call = std::move(call)]() { call(transport); },
            Qt::QueuedConnection);
    }

    static void onOpened(JNIEnv*, jclass, jlong handle, jint attempt) {
        post(handle, [attempt](AndroidUsbSerialTransport* t) { t->handleOpened(attempt); });
    }

    static void onData(JNIEnv* env, jclass, jlong handle, jint attempt, jbyteArray data) {
        const jsize size = env->GetArrayLength(data);
        QByteArray bytes(size, Qt::Uninitialized);
        env->GetByteArrayRegion(data, 0, size, reinterpret_cast<jbyte*>(bytes.data()));
        post(handle, [attempt, bytes](AndroidUsbSerialTransport* t) {
            t->handleData(attempt, bytes);
        });
    }

    static void onError(JNIEnv* env, jclass, jlong handle, jint attempt, jstring message,
                        jboolean permissionDenied) {
        const QString text = fromJString(env, message);
        const bool denied = permissionDenied == JNI_TRUE;
        post(handle, [attempt, text, denied](AndroidUsbSerialTransport* t) {
            t->handleError(attempt, text, denied);
        });
    }

    static void onLost(JNIEnv* env, jclass, jlong handle, jint attempt, jstring message) {
        const QString text = fromJString(env, message);
        post(handle,
             [attempt, text](AndroidUsbSerialTransport* t) { t->handleLost(attempt, text); });
    }

    static void onDeviceAttached(JNIEnv*, jclass) {
        QMutexLocker lock(&registryMutex());
        for (AndroidUsbSerialTransport* transport : std::as_const(registry())) {
            QMetaObject::invokeMethod(
                transport, [transport]() { transport->handleDeviceAttached(); },
                Qt::QueuedConnection);
        }
    }

    static bool registerOnce() {
        static const bool registered = [] {
            const JNINativeMethod methods[] = {
                {"nativeOnOpened", "(JI)V", reinterpret_cast<void*>(&onOpened)},
                {"nativeOnData", "(JI[B)V", reinterpret_cast<void*>(&onData)},
                {"nativeOnError", "(JILjava/lang/String;Z)V", reinterpret_cast<void*>(&onError)},
                {"nativeOnLost", "(JILjava/lang/String;)V", reinterpret_cast<void*>(&onLost)},
                {"nativeOnDeviceAttached", "()V", reinterpret_cast<void*>(&onDeviceAttached)},
            };
            QJniEnvironment env;
            const bool ok = env.registerNativeMethods(kBridgeClass, methods,
                                                      sizeof(methods) / sizeof(methods[0]));
            if (!ok) {
                qCWarning(lcSerial) << "could not register UsbSerialBridge natives";
            }
            return ok;
        }();
        return registered;
    }
};

AndroidUsbSerialTransport::AndroidUsbSerialTransport(QObject* parent) : SerialTransport(parent) {
    AndroidUsbSerialNatives::registerOnce();
    {
        QMutexLocker lock(&registryMutex());
        m_handle = nextHandle();
        registry().insert(m_handle, this);
    }
    m_bridge = QJniObject(kBridgeClass, "(Landroid/content/Context;J)V",
                          androidContext().object(), jlong(m_handle));
    if (!m_bridge.isValid()) {
        qCWarning(lcSerial) << "could not create UsbSerialBridge";
    }
}

AndroidUsbSerialTransport::~AndroidUsbSerialTransport() {
    {
        QMutexLocker lock(&registryMutex());
        registry().remove(m_handle);
    }
    if (m_bridge.isValid()) {
        m_bridge.callMethod<void>("release", "()V");
    }
}

QVector<SerialPortOption> AndroidUsbSerialTransport::availablePorts() {
    AndroidUsbSerialNatives::registerOnce();
    QVector<SerialPortOption> ports;
    // Flat [key0, label0, key1, label1, ...] -- one JNI round trip, no helper
    // Java type to mirror here.
    const QJniObject list = QJniObject::callStaticObjectMethod(
        kBridgeClass, "listPorts", "(Landroid/content/Context;)[Ljava/lang/String;",
        androidContext().object());
    if (!list.isValid()) {
        return ports;
    }
    QJniEnvironment env;
    auto array = list.object<jobjectArray>();
    const jsize count = env->GetArrayLength(array);
    for (jsize i = 0; i + 1 < count; i += 2) {
        auto key = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        auto label = static_cast<jstring>(env->GetObjectArrayElement(array, i + 1));
        ports.append({fromJString(env.jniEnv(), key), fromJString(env.jniEnv(), label)});
        env->DeleteLocalRef(key);
        env->DeleteLocalRef(label);
    }
    return ports;
}

bool AndroidUsbSerialTransport::open(const QString& portName, qint32 baudRate) {
    close();
    m_portName = portName;
    m_baudRate = safeBaudRate(baudRate);
    qCInfo(lcSerial) << "opening" << portName << "at" << m_baudRate << "baud (USB host)";

    if (!m_bridge.isValid()) {
        emit errorOccurred(tr("USB serial is not available on this device"));
        return false;
    }
    if (!m_permissionDeniedFor.isEmpty() && m_permissionDeniedFor == portName) {
        // Asking again on every reconnect tick would keep throwing the
        // system dialog at a user who already said no.
        emit errorOccurred(
            tr("USB permission was denied for %1; unplug and replug it to be asked again")
                .arg(portName));
        return false;
    }
    if (m_baudRate != baudRate) {
        qCWarning(lcSerial) << "1200 baud requested on" << portName
                            << "-- folding up to 115200 to avoid a bootloader reset";
        emit errorOccurred(
            tr("1200 baud resets the ESP32-S3 into its bootloader; using 115200"));
    }

    ++m_attempt;
    m_opening = true;
    m_bridge.callMethod<void>("open", "(Ljava/lang/String;II)V",
                              QJniObject::fromString(portName).object<jstring>(),
                              jint(m_baudRate), jint(m_attempt));
    return true;
}

void AndroidUsbSerialTransport::close() {
    if (!m_connected && !m_opening) {
        return;
    }
    const bool wasConnected = m_connected;
    m_connected = false;
    m_opening = false;
    // Superseding the attempt drops any callback still in flight for it.
    ++m_attempt;
    if (m_bridge.isValid()) {
        m_bridge.callMethod<void>("close", "()V");
    }
    if (wasConnected) {
        qCInfo(lcSerial) << "closed" << m_portName;
        emit connectionStateChanged(false);
    }
}

bool AndroidUsbSerialTransport::isConnected() const {
    return m_connected;
}

QString AndroidUsbSerialTransport::portName() const {
    return m_portName;
}

qint32 AndroidUsbSerialTransport::baudRate() const {
    return m_baudRate;
}

bool AndroidUsbSerialTransport::write(const QByteArray& data) {
    if (!m_connected) {
        return false;
    }
    if (AppSettings::instance().verboseSerialLogging()) {
        qCDebug(lcSerial) << "write" << data.size() << "bytes:" << hexInline(data);
    }
    QJniEnvironment env;
    jbyteArray array = env->NewByteArray(jsize(data.size()));
    env->SetByteArrayRegion(array, 0, jsize(data.size()),
                            reinterpret_cast<const jbyte*>(data.constData()));
    const jboolean queued = m_bridge.callMethod<jboolean>("write", "([B)Z", array);
    env->DeleteLocalRef(array);
    return queued == JNI_TRUE;
}

bool AndroidUsbSerialTransport::drainWrites(int timeoutMs) {
    if (!m_connected || timeoutMs < 0) {
        return false;
    }
    return m_bridge.callMethod<jboolean>("drain", "(I)Z", jint(timeoutMs)) == JNI_TRUE;
}

void AndroidUsbSerialTransport::handleOpened(int attempt) {
    if (attempt != m_attempt || !m_opening) {
        return;
    }
    m_opening = false;
    m_connected = true;
    m_permissionDeniedFor.clear();
    qCInfo(lcSerial) << "opened" << m_portName;
    emit connectionStateChanged(true);
}

void AndroidUsbSerialTransport::handleData(int attempt, const QByteArray& data) {
    if (attempt != m_attempt || !m_connected) {
        return;
    }
    if (AppSettings::instance().verboseSerialLogging()) {
        qCDebug(lcSerial) << "read" << data.size() << "bytes:" << hexInline(data);
    }
    emit dataReceived(data);
}

void AndroidUsbSerialTransport::handleError(int attempt, const QString& message,
                                            bool permissionDenied) {
    if (attempt != m_attempt) {
        return;
    }
    m_opening = false;
    if (permissionDenied) {
        m_permissionDeniedFor = m_portName;
    }
    qCWarning(lcSerial) << "error on" << m_portName << ":" << message;
    emit errorOccurred(message);
}

void AndroidUsbSerialTransport::handleLost(int attempt, const QString& message) {
    if (attempt != m_attempt || !m_connected) {
        return;
    }
    // Same order SerialManager uses for a yanked device: become truthfully
    // disconnected first, then report why.
    m_connected = false;
    ++m_attempt;
    qCWarning(lcSerial) << "lost" << m_portName << ":" << message;
    emit connectionStateChanged(false);
    emit errorOccurred(message);
}

void AndroidUsbSerialTransport::handleDeviceAttached() {
    m_permissionDeniedFor.clear();
    emit deviceAttached();
}

}  // namespace traceview
