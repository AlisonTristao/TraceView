package io.github.alisontristao.traceview;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.SystemClock;
import android.util.Log;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

// Java half of lib/core/androidusbserialtransport.cpp: a minimal CDC-ACM
// driver over the Android USB Host API, one instance per
// AndroidUsbSerialTransport. Only CDC-ACM is handled (the dongle and ESP32
// boards with native USB); USB-serial bridge chips are out of scope.
//
// Threads: open/write/close run in order on a per-instance single-thread
// executor, reads on their own thread, and USB broadcasts arrive on the main
// looper. Every result goes back to C++ through the static natives below,
// tagged with the handle of the owning transport and the attempt number C++
// passed to open(); C++ drops anything from a superseded attempt.
//
// Line state follows docs/PROTOCOL.md ("Serial line state: DTR"): DTR+RTS
// asserted once the line is configured (the dongle's USB-CDC drops every byte
// it sends until DTR is up), and on close RTS lowered before DTR -- the CDC's
// line-state machine must never see `!dtr && rts`.
public class UsbSerialBridge {
    private static final String TAG = "TraceViewUsbSerial";
    private static final String ACTION_USB_PERMISSION =
            "io.github.alisontristao.traceview.USB_PERMISSION";

    // CDC class requests (USB CDC PSTN subclass spec, section 6.3).
    private static final int REQUEST_TYPE_CLASS_OUT = 0x21;
    private static final int SET_LINE_CODING = 0x20;
    private static final int SET_CONTROL_LINE_STATE = 0x22;
    private static final int LINE_DTR = 0x01;
    private static final int LINE_RTS = 0x02;
    private static final int CONTROL_TIMEOUT_MS = 1000;
    private static final int WRITE_TIMEOUT_MS = 2000;
    private static final int READ_TIMEOUT_MS = 200;
    private static final int MAX_WRITE_CHUNK = 16384;
    private static final int USB_SUBCLASS_ACM = 0x02;

    static native void nativeOnOpened(long handle, int attempt);
    static native void nativeOnData(long handle, int attempt, byte[] data);
    static native void nativeOnError(long handle, int attempt, String message,
                                     boolean permissionDenied);
    static native void nativeOnLost(long handle, int attempt, String message);
    static native void nativeOnDeviceAttached();

    private static final List<UsbSerialBridge> sInstances = new ArrayList<>();
    private static boolean sReceiverRegistered = false;

    private final Context mContext;
    private final UsbManager mUsbManager;
    private final long mHandle;
    private final ExecutorService mIo = Executors.newSingleThreadExecutor();

    // Guarded by `this`. The attempt the latest open() belongs to, and the
    // device it is waiting on a permission answer for (null when not waiting).
    private int mAttempt = 0;
    private UsbDevice mAwaitingPermission = null;
    private int mAwaitingBaud = 0;

    // Touched only on mIo, except mDeviceName (read by the detach broadcast)
    // and mReading (read by the reader thread).
    private UsbDeviceConnection mConnection;
    private UsbInterface mControlInterface;
    private UsbInterface mDataInterface;
    private UsbEndpoint mReadEndpoint;
    private UsbEndpoint mWriteEndpoint;
    private int mControlIndex;
    private int mOpenAttempt;
    private volatile String mDeviceName;
    private volatile boolean mReading;
    private Thread mReader;

    public UsbSerialBridge(Context context, long handle) {
        mContext = context.getApplicationContext();
        mUsbManager = (UsbManager) mContext.getSystemService(Context.USB_SERVICE);
        mHandle = handle;
        synchronized (sInstances) {
            sInstances.add(this);
            ensureReceiver(mContext);
        }
    }

    // Flat [key0, label0, key1, label1, ...] of attached CDC-ACM devices.
    public static String[] listPorts(Context context) {
        Context app = context.getApplicationContext();
        UsbManager manager = (UsbManager) app.getSystemService(Context.USB_SERVICE);
        synchronized (sInstances) {
            ensureReceiver(app);
        }
        List<String> out = new ArrayList<>();
        if (manager == null) {
            return new String[0];
        }
        for (UsbDevice device : manager.getDeviceList().values()) {
            if (!isCdcAcm(device)) {
                continue;
            }
            out.add(keyFor(manager, device));
            out.add(labelFor(device));
        }
        return out.toArray(new String[0]);
    }

    public void open(final String key, final int baud, final int attempt) {
        synchronized (this) {
            mAttempt = attempt;
            mAwaitingPermission = null;
        }
        mIo.execute(() -> {
            closeConnection(true);
            UsbDevice device = findDevice(key);
            if (device == null) {
                nativeOnError(mHandle, attempt, "USB device " + key + " not found", false);
                return;
            }
            if (!mUsbManager.hasPermission(device)) {
                requestPermission(device, baud, attempt);
                return;
            }
            openDevice(device, baud, attempt);
        });
    }

    public boolean write(final byte[] data) {
        if (mDeviceName == null) {
            return false;
        }
        mIo.execute(() -> {
            UsbDeviceConnection connection = mConnection;
            if (connection == null) {
                return;
            }
            int offset = 0;
            while (offset < data.length) {
                int length = Math.min(MAX_WRITE_CHUNK, data.length - offset);
                int written = connection.bulkTransfer(mWriteEndpoint, data, offset, length,
                                                      WRITE_TIMEOUT_MS);
                if (written <= 0) {
                    lose("USB write failed", mOpenAttempt);
                    return;
                }
                offset += written;
            }
        });
        return true;
    }

    // Everything queued before this call has been handed to the USB stack.
    public boolean drain(int timeoutMs) {
        Future<?> marker = mIo.submit(() -> { });
        try {
            marker.get(timeoutMs, TimeUnit.MILLISECONDS);
            return mConnection != null;
        } catch (Exception e) {
            return false;
        }
    }

    public void close() {
        synchronized (this) {
            mAttempt = -1;
            mAwaitingPermission = null;
        }
        mIo.execute(() -> closeConnection(true));
    }

    // The owning transport is going away: close and stop hearing broadcasts.
    public void release() {
        close();
        synchronized (sInstances) {
            sInstances.remove(this);
        }
        mIo.shutdown();
    }

    // ---- open ----

    private void requestPermission(UsbDevice device, int baud, int attempt) {
        synchronized (this) {
            if (attempt != mAttempt) {
                return;
            }
            mAwaitingPermission = device;
            mAwaitingBaud = baud;
        }
        // Explicit (setPackage) and mutable: UsbManager fills in the result
        // extras, and API 34+ refuses a mutable PendingIntent around an
        // implicit intent.
        Intent intent = new Intent(ACTION_USB_PERMISSION);
        intent.setPackage(mContext.getPackageName());
        int flags = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ? PendingIntent.FLAG_MUTABLE : 0;
        PendingIntent pending = PendingIntent.getBroadcast(mContext, 0, intent, flags);
        mUsbManager.requestPermission(device, pending);
    }

    private void onPermissionResult(UsbDevice device, boolean granted) {
        final int attempt;
        final int baud;
        synchronized (this) {
            if (mAwaitingPermission == null
                    || !mAwaitingPermission.getDeviceName().equals(device.getDeviceName())) {
                return;
            }
            mAwaitingPermission = null;
            attempt = mAttempt;
            baud = mAwaitingBaud;
        }
        if (!granted) {
            nativeOnError(mHandle, attempt, "USB permission denied", true);
            return;
        }
        mIo.execute(() -> {
            synchronized (this) {
                if (attempt != mAttempt) {
                    return;
                }
            }
            openDevice(device, baud, attempt);
        });
    }

    private void openDevice(UsbDevice device, int baud, int attempt) {
        UsbInterface control = null;
        UsbInterface data = null;
        UsbEndpoint in = null;
        UsbEndpoint out = null;
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface iface = device.getInterface(i);
            if (control == null && iface.getInterfaceClass() == UsbConstants.USB_CLASS_COMM
                    && iface.getInterfaceSubclass() == USB_SUBCLASS_ACM) {
                control = iface;
            } else if (data == null
                    && iface.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA) {
                UsbEndpoint[] endpoints = bulkEndpoints(iface);
                if (endpoints != null) {
                    data = iface;
                    in = endpoints[0];
                    out = endpoints[1];
                }
            }
        }
        if (data == null) {
            nativeOnError(mHandle, attempt, "Not a CDC-ACM serial device", false);
            return;
        }

        UsbDeviceConnection connection = mUsbManager.openDevice(device);
        if (connection == null) {
            nativeOnError(mHandle, attempt, "Could not open USB device", false);
            return;
        }
        if ((control != null && !connection.claimInterface(control, true))
                || !connection.claimInterface(data, true)) {
            connection.close();
            nativeOnError(mHandle, attempt, "USB interface is busy", false);
            return;
        }

        mConnection = connection;
        mControlInterface = control;
        mDataInterface = data;
        mReadEndpoint = in;
        mWriteEndpoint = out;
        mControlIndex = control != null ? control.getId() : 0;
        mOpenAttempt = attempt;

        // 8N1. The rate is cosmetic on a native USB-CDC link but some stacks
        // want a line coding before they pass data, so a failure here is
        // only logged.
        byte[] coding = {
                (byte) (baud & 0xff), (byte) ((baud >> 8) & 0xff),
                (byte) ((baud >> 16) & 0xff), (byte) ((baud >> 24) & 0xff),
                0 /* 1 stop bit */, 0 /* no parity */, 8 /* data bits */};
        if (connection.controlTransfer(REQUEST_TYPE_CLASS_OUT, SET_LINE_CODING, 0, mControlIndex,
                                       coding, coding.length, CONTROL_TIMEOUT_MS) < 0) {
            Log.w(TAG, "SET_LINE_CODING failed on " + device.getDeviceName());
        }
        if (!setLineState(LINE_DTR | LINE_RTS)) {
            closeConnection(false);
            nativeOnError(mHandle, attempt, "Could not assert DTR on the USB device", false);
            return;
        }

        mDeviceName = device.getDeviceName();
        // Opened before the reader starts, so C++ never sees data for a
        // port it does not yet consider open.
        nativeOnOpened(mHandle, attempt);
        startReader(connection, in, attempt);
    }

    private static UsbEndpoint[] bulkEndpoints(UsbInterface iface) {
        UsbEndpoint in = null;
        UsbEndpoint out = null;
        for (int e = 0; e < iface.getEndpointCount(); e++) {
            UsbEndpoint endpoint = iface.getEndpoint(e);
            if (endpoint.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) {
                continue;
            }
            if (endpoint.getDirection() == UsbConstants.USB_DIR_IN) {
                in = endpoint;
            } else {
                out = endpoint;
            }
        }
        return in != null && out != null ? new UsbEndpoint[] {in, out} : null;
    }

    private boolean setLineState(int state) {
        return mConnection.controlTransfer(REQUEST_TYPE_CLASS_OUT, SET_CONTROL_LINE_STATE, state,
                                           mControlIndex, null, 0, CONTROL_TIMEOUT_MS) >= 0;
    }

    // ---- read ----

    private void startReader(final UsbDeviceConnection connection, final UsbEndpoint in,
                             final int attempt) {
        mReading = true;
        mReader = new Thread(() -> {
            byte[] buffer = new byte[Math.max(4096, in.getMaxPacketSize())];
            int fastFailures = 0;
            while (mReading) {
                long started = SystemClock.elapsedRealtime();
                int count = connection.bulkTransfer(in, buffer, buffer.length, READ_TIMEOUT_MS);
                if (count > 0) {
                    fastFailures = 0;
                    nativeOnData(mHandle, attempt, Arrays.copyOf(buffer, count));
                } else if (count < 0) {
                    // -1 is both "timed out" and "failed". A failure returns
                    // well before the timeout; several in a row mean the
                    // device is gone (the detach broadcast usually says so
                    // first).
                    if (SystemClock.elapsedRealtime() - started < READ_TIMEOUT_MS / 2) {
                        if (++fastFailures >= 10 && mReading) {
                            mIo.execute(() -> lose("USB read failed", attempt));
                            return;
                        }
                    } else {
                        fastFailures = 0;
                    }
                }
            }
        }, "TraceViewUsbSerialReader");
        mReader.start();
    }

    // ---- close ----

    // mIo only. `lowerLines` is false when the device is already gone.
    private void closeConnection(boolean lowerLines) {
        mDeviceName = null;
        mReading = false;
        UsbDeviceConnection connection = mConnection;
        if (connection == null) {
            return;
        }
        if (lowerLines) {
            // RTS first, then DTR -- see the class comment.
            setLineState(LINE_DTR);
            setLineState(0);
        }
        if (mReader != null) {
            try {
                mReader.join(READ_TIMEOUT_MS * 3);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            mReader = null;
        }
        if (mControlInterface != null) {
            connection.releaseInterface(mControlInterface);
        }
        if (mDataInterface != null) {
            connection.releaseInterface(mDataInterface);
        }
        connection.close();
        mConnection = null;
        mControlInterface = null;
        mDataInterface = null;
    }

    // mIo only: the connection opened by `attempt` died underneath us. A
    // report that arrives after that connection was already replaced is
    // ignored rather than closing the new one.
    private void lose(String message, int attempt) {
        if (mConnection == null || attempt != mOpenAttempt) {
            return;
        }
        closeConnection(false);
        nativeOnLost(mHandle, attempt, message);
    }

    private void onDetached(UsbDevice device) {
        String name = mDeviceName;
        if (name != null && name.equals(device.getDeviceName())) {
            mIo.execute(() -> lose("USB device unplugged", mOpenAttempt));
        }
        synchronized (this) {
            if (mAwaitingPermission != null
                    && mAwaitingPermission.getDeviceName().equals(device.getDeviceName())) {
                mAwaitingPermission = null;
                nativeOnError(mHandle, mAttempt, "USB device unplugged", false);
            }
        }
    }

    // ---- device identity ----

    private static boolean isCdcAcm(UsbDevice device) {
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface iface = device.getInterface(i);
            if (iface.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA
                    && bulkEndpoints(iface) != null) {
                return true;
            }
        }
        return false;
    }

    // Readable only once the app holds the device permission (API 29+).
    private static String serialOf(UsbManager manager, UsbDevice device) {
        if (!manager.hasPermission(device)) {
            return null;
        }
        try {
            String serial = device.getSerialNumber();
            return serial == null || serial.isEmpty() ? null : serial;
        } catch (SecurityException e) {
            return null;
        }
    }

    private static String keyFor(UsbManager manager, UsbDevice device) {
        String key = String.format(Locale.ROOT, "usb:%04X:%04X", device.getVendorId(),
                                   device.getProductId());
        String serial = serialOf(manager, device);
        return serial != null ? key + ":" + serial : key;
    }

    private static String labelFor(UsbDevice device) {
        String product = device.getProductName();
        String ids = String.format(Locale.ROOT, "%04X:%04X", device.getVendorId(),
                                   device.getProductId());
        return (product == null || product.isEmpty() ? "USB serial" : product) + " (" + ids + ")";
    }

    // "usb:VVVV:PPPP[:serial]". A device matches on VID:PID when the key has
    // no serial or the device's serial cannot be read yet.
    private UsbDevice findDevice(String key) {
        if (key == null || !key.startsWith("usb:")) {
            return null;
        }
        String[] parts = key.split(":", 4);
        if (parts.length < 3) {
            return null;
        }
        int vid;
        int pid;
        try {
            vid = Integer.parseInt(parts[1], 16);
            pid = Integer.parseInt(parts[2], 16);
        } catch (NumberFormatException e) {
            return null;
        }
        String wantedSerial = parts.length == 4 ? parts[3] : null;
        UsbDevice fallback = null;
        for (UsbDevice device : mUsbManager.getDeviceList().values()) {
            if (device.getVendorId() != vid || device.getProductId() != pid
                    || !isCdcAcm(device)) {
                continue;
            }
            String serial = serialOf(mUsbManager, device);
            if (wantedSerial == null || wantedSerial.equals(serial)) {
                return device;
            }
            if (serial == null && fallback == null) {
                fallback = device;
            }
        }
        return fallback;
    }

    // ---- broadcasts ----

    // Caller holds sInstances.
    private static void ensureReceiver(Context app) {
        if (sReceiverRegistered) {
            return;
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(ACTION_USB_PERMISSION);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                dispatch(intent);
            }
        };
        // System broadcasts (attach/detach) still reach a not-exported
        // receiver, and the permission result is sent as this app.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            app.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(receiver, filter);
        }
        sReceiverRegistered = true;
    }

    @SuppressWarnings("deprecation")
    private static UsbDevice deviceExtra(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class);
        }
        return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
    }

    private static void dispatch(Intent intent) {
        String action = intent.getAction();
        if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
            nativeOnDeviceAttached();
            return;
        }
        UsbDevice device = deviceExtra(intent);
        if (device == null) {
            return;
        }
        List<UsbSerialBridge> instances;
        synchronized (sInstances) {
            instances = new ArrayList<>(sInstances);
        }
        for (UsbSerialBridge bridge : instances) {
            if (ACTION_USB_PERMISSION.equals(action)) {
                bridge.onPermissionResult(device,
                        intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false));
            } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                bridge.onDetached(device);
            }
        }
    }
}
