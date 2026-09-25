package io.github.alisontristao.traceview;

import android.annotation.TargetApi;
import android.app.Activity;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayDeque;
import java.util.List;

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

    private static final int MAX_TRACE_LINES = 64;
    private static final ArrayDeque<String> s_trace = new ArrayDeque<>();

    private KeyboardInsetsDebounce() {}

    public static void install(final Activity activity) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            try {
                installTracing(activity);
            } catch (Throwable e) {
                Log.w(TAG, "keyboard tracing not installed", e);
            }
            try {
                installOnUiThread(activity);
                trace("debounce installed");
            } catch (Throwable e) {
                trace("debounce NOT installed: " + e);
                Log.w(TAG, "keyboard insets debounce not installed", e);
            }
        });
    }

    // Lines recorded since the last call, oldest first, newline-separated, each
    // prefixed with SystemClock.uptimeMillis() % 100000 -- the same clock
    // (CLOCK_MONOTONIC) the C++ input diagnostics overlay stamps its own
    // lines with, so the two can be read side by side. Polled by that
    // overlay (lib/core/inputdiagnostics.cpp); nothing else reads it.
    public static synchronized String drainTrace() {
        if (s_trace.isEmpty()) {
            return "";
        }
        StringBuilder out = new StringBuilder();
        while (!s_trace.isEmpty()) {
            if (out.length() > 0) {
                out.append('\n');
            }
            out.append(s_trace.pollFirst());
        }
        return out.toString();
    }

    private static synchronized void trace(String line) {
        s_trace.addLast(String.format("%5d J %s", SystemClock.uptimeMillis() % 100000, line));
        while (s_trace.size() > MAX_TRACE_LINES) {
            s_trace.pollFirst();
        }
    }

    private static String viewName(View view) {
        return view == null ? "null" : view.getClass().getSimpleName();
    }

    // Records what the platform does with the IME around a Backspace --
    // insets passes, IME show/hide animations, Android-side focus moves --
    // for the overlay, so a keyboard that still drops can be traced to its
    // source on a phone without adb.
    @TargetApi(Build.VERSION_CODES.R)
    private static void installTracing(Activity activity) {
        final View decor = activity.getWindow().getDecorView();
        decor.getViewTreeObserver().addOnGlobalFocusChangeListener(
                (oldFocus, newFocus) -> trace("focus " + viewName(oldFocus) + " -> "
                                              + viewName(newFocus)));
        decor.setWindowInsetsAnimationCallback(
                new WindowInsetsAnimation.Callback(
                        WindowInsetsAnimation.Callback.DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
                    @Override
                    public void onPrepare(WindowInsetsAnimation animation) {
                        if ((animation.getTypeMask() & WindowInsets.Type.ime()) != 0) {
                            WindowInsets now = decor.getRootWindowInsets();
                            trace("ime anim prepare (visible="
                                  + (now != null && now.isVisible(WindowInsets.Type.ime()))
                                  + ")");
                        }
                    }

                    @Override
                    public WindowInsets onProgress(WindowInsets insets,
                                                   List<WindowInsetsAnimation> running) {
                        return insets;
                    }

                    @Override
                    public void onEnd(WindowInsetsAnimation animation) {
                        if ((animation.getTypeMask() & WindowInsets.Type.ime()) != 0) {
                            WindowInsets now = decor.getRootWindowInsets();
                            trace("ime anim end (visible="
                                  + (now != null && now.isVisible(WindowInsets.Type.ime()))
                                  + ")");
                        }
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
                trace("hide not confirmed, kept open");
                return;
            }
            trace("hide confirmed -> Qt");
            apply(delegate, visibleField, setVisibility, false);
        };

        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            trace("insets ime=" + insets.isVisible(WindowInsets.Type.ime()) + " bottom="
                  + insets.getInsets(WindowInsets.Type.ime()).bottom);
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
