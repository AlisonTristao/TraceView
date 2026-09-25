package io.github.alisontristao.traceview;

import android.annotation.TargetApi;
import android.app.Activity;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

// Works around Qt 6.9's soft-keyboard visibility tracking on Android 11+.
//
// Qt's QtInputDelegate puts an OnApplyWindowInsetsListener on the decor view
// and, the moment an insets pass reports the IME as not visible, marks the
// keyboard hidden and clears focus from its QtEditText -- which really does
// close the keyboard. Some IMEs (seen on a Xiaomi phone) report the IME as
// not visible for a single pass while they resize themselves, e.g. when the
// suggestion strip changes after a Backspace; Qt then closes the keyboard
// and reopens it, so it visibly drops and comes back on every delete.
//
// This replaces Qt's listener with one that forwards "visible" right away
// but only forwards "hidden" if the IME is still hidden a moment later.
// Qt's own state is reached by reflection into its (pinned-version) classes;
// if any of that is missing, nothing is installed and Qt's behavior stays.
public final class KeyboardInsetsDebounce {
    private static final String TAG = "TraceView";
    private static final long HIDE_DELAY_MS = 250;

    private KeyboardInsetsDebounce() {}

    public static void install(final Activity activity) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            try {
                installOnUiThread(activity);
            } catch (Throwable e) {
                Log.w(TAG, "keyboard insets debounce not installed", e);
            }
        });
    }

    @TargetApi(Build.VERSION_CODES.R)
    private static void installOnUiThread(Activity activity) throws Exception {
        Object activityDelegate = field(activity.getClass(), "m_delegate").get(activity);
        Object inputDelegate = field(activityDelegate.getClass(), "m_inputDelegate")
                .get(activityDelegate);
        final Field visibleField = field(inputDelegate.getClass(), "m_keyboardIsVisible");
        final Method setVisibility = inputDelegate.getClass().getDeclaredMethod(
                "setKeyboardVisibility_internal", boolean.class, long.class);
        setVisibility.setAccessible(true);
        final Object delegate = inputDelegate;

        final View decor = activity.getWindow().getDecorView();
        final Handler handler = new Handler(Looper.getMainLooper());
        final Runnable confirmHidden = () -> {
            WindowInsets now = decor.getRootWindowInsets();
            if (now != null && now.isVisible(WindowInsets.Type.ime())) {
                return;
            }
            apply(delegate, visibleField, setVisibility, false);
        };

        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            if (insets.isVisible(WindowInsets.Type.ime())) {
                handler.removeCallbacks(confirmHidden);
                apply(delegate, visibleField, setVisibility, true);
            } else {
                handler.removeCallbacks(confirmHidden);
                handler.postDelayed(confirmHidden, HIDE_DELAY_MS);
            }
            return insets;
        });
    }

    private static void apply(Object delegate, Field visibleField, Method setVisibility,
                              boolean visible) {
        try {
            if (visibleField.getBoolean(delegate) != visible) {
                setVisibility.invoke(delegate, visible, System.nanoTime());
            }
        } catch (Throwable e) {
            Log.w(TAG, "keyboard visibility update failed", e);
        }
    }

    // Declared field `name` on `owner` or its nearest superclass declaring it.
    private static Field field(Class<?> owner, String name) throws NoSuchFieldException {
        for (Class<?> c = owner; c != null; c = c.getSuperclass()) {
            try {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);
                return f;
            } catch (NoSuchFieldException ignored) {
                // keep walking up
            }
        }
        throw new NoSuchFieldException(name);
    }
}
