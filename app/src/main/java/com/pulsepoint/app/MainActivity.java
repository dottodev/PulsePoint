package com.pulsepoint.app;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The whole Activity.
 *
 * <p>There is exactly one screen, one view and one native library.  The Activity's only jobs
 * are to make the display behave like a game display -- immersive, edge to edge, awake, no
 * flicker on rotate -- and to keep the native game informed about the safe area so no
 * button can end up behind a gesture bar or a cutout.
 */
public final class MainActivity extends Activity {

    private GameView view;
    private AudioStream audio;
    private boolean nativeReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // A reaction game is played with the screen on and the phone still.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        view = new GameView(this);
        setContentView(view);

        // Activity has no onApplyWindowInsets to override, so the decor view
        // reports them: rotate the phone and the native layout follows.
        view.setOnApplyWindowInsetsListener((v, insets) -> {
            applySafeArea(insets);
            return insets;
        });

        // The renderer needs a live EGL context before it can create any GL object, so
        // the native game starts from the Activity's first layout pass, which is after
        // onSurfaceCreated has already run.
        view.post(this::startNative);

        goImmersive();
    }

    private void startNative() {
        if (nativeReady) return;
        nativeReady = PulsePoint.nativeInit(getFilesDir().getAbsolutePath());
        if (!nativeReady) return;
        applySafeArea();
        audio = new AudioStream();
        audio.start();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) goImmersive();
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        applySafeArea();
    }

    /**
     * Publishes the safe area to the native layout.
     *
     * <p>Both insets are reported, not just the top: a gesture bar at the bottom is exactly
     * where a result screen's buttons live, and a control hidden behind it is a control
     * that cannot be pressed.
     */
    private void applySafeArea() {
        if (view == null) return;
        float top = 0.0f;
        float bottom = 0.0f;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final WindowInsets insets = getWindow().getDecorView().getRootWindowInsets();
            if (insets != null) {
                final android.graphics.Insets bars =
                        insets.getInsetsIgnoringVisibility(WindowInsets.Type.systemBars());
                top = bars.top;
                bottom = bars.bottom;
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // status_bar_height is not in the public SDK; look it up by name.
            final int statusId =
                    getResources().getIdentifier("status_bar_height", "dimen", "android");
            if (statusId > 0) top = getResources().getDimensionPixelSize(statusId);
            final int navId =
                    getResources().getIdentifier("navigation_bar_height", "dimen", "android");
            if (navId > 0) bottom = getResources().getDimensionPixelSize(navId);
        }
        view.setSafeArea(top, bottom);
    }

    private void applySafeArea(WindowInsets insets) {
        if (view == null) return;
        final float top = insets.getSystemWindowInsetTop();
        final float bottom = insets.getSystemWindowInsetBottom();
        view.setSafeArea(top, bottom);
    }

    /** Edge to edge, bars hidden, laid out against the cutout rather than inside it. */
    private void goImmersive() {
        final View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        // A round in progress is abandoned, not resumed, and the profile is written out
        // before the process can be killed.
        if (nativeReady) {
            PulsePoint.nativePause();
            audioStop();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (nativeReady) {
            PulsePoint.nativeResume();
            if (audio == null) {
                audio = new AudioStream();
            }
            audio.start();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        audioStop();
        if (nativeReady) {
            PulsePoint.nativeShutdown();
            nativeReady = false;
        }
    }

    private void audioStop() {
        if (audio != null) {
            audio.stop();
            audio = null;
        }
    }
}
