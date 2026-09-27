package com.pulsepoint.app;

import android.content.Context;
import android.opengl.GLES30;
import android.opengl.GLSurfaceView;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.view.MotionEvent;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * The GL surface and the touch front end.
 *
 * <p>Two things here exist purely for latency, and both are load-bearing:
 *
 * <ul>
 *   <li>Touch samples are timestamped with {@link SystemClock#elapsedRealtimeNanos()} on
 *       entry to {@code onTouchEvent}, before any dispatch, view lookup or allocation.
 *       Reacting to a {@code MotionEvent} in the renderer instead would be simpler and
 *       would add the input-queue delay to every measurement.
 *   <li>The native side is fed directly from the UI thread rather than through {@code
 *       queueEvent()}.  {@code queueEvent} defers work to the start of the next GL frame,
 *       which is up to 16 ms of scheduling delay on a display the player is already
 *       waiting on.  Native does the de-duplication, so the two threads never need to
 *       agree on anything but a pointer.
 * </ul>
 *
 * <p>Nothing in the touch path allocates and nothing takes a lock.  {@code queueEvent} is
 * used only for geometry, which is not on the measured path.
 */
final class GameView extends GLSurfaceView {

    private static final int PHASE_DOWN = 0;
    private static final int PHASE_MOVE = 1;
    private static final int PHASE_UP = 2;
    private static final int PHASE_CANCEL = 3;

    // Published to the GL thread; only ever written from the UI thread on layout.
    private volatile float density;
    private volatile float safeTop;
    private volatile float safeBottom;

    GameView(Context context) {
        super(context);
        setEGLContextClientVersion(3);
        // No depth or stencil: the frame is entirely painter-ordered, so asking for those
        // buffers costs memory for nothing on a mid-range device.
        setEGLConfigChooser(8, 8, 8, 0, 0, 0);
        density = context.getResources().getDisplayMetrics().density;
        setRenderer(new FrameRenderer());
        setRenderMode(RENDERMODE_CONTINUOUSLY);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    /** Called by the Activity whenever the window insets change. */
    void setSafeArea(float topPx, float bottomPx) {
        safeTop = topPx;
        safeBottom = bottomPx;
        reportGeometry();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        reportGeometry();
    }

    /**
     * Pushes the current geometry to the native side.  Deferred through {@code queueEvent}
     * because the renderer owns that state, and because nothing here is measured.
     */
    private void reportGeometry() {
        final int w = getWidth();
        final int h = getHeight();
        if (w <= 0 || h <= 0) return;
        final float d = density;
        final float t = safeTop;
        final float b = safeBottom;
        queueEvent(() -> PulsePoint.nativeSurfaceChanged(w, h, d, t, b));
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // Sampled first, unconditionally: this is the measurement.
        final long timeNs = SystemClock.elapsedRealtimeNanos();
        final int action = event.getActionMasked();

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                final int index = event.getActionIndex();
                forward(event, event.getPointerId(index), PHASE_DOWN, timeNs);
                // Only the first finger is ever a measurement; the rest are rejected
                // natively, so they cost one call and nothing else.
                return true;
            }
            case MotionEvent.ACTION_MOVE:
                // Only the primary pointer's current position.  A reaction game has no use
                // for a drag path, and reading history would add work to the input path.
                forward(event, 0, PHASE_MOVE, timeNs);
                return true;
            case MotionEvent.ACTION_UP:
                forward(event, 0, PHASE_UP, timeNs);
                performClick();
                return true;
            case MotionEvent.ACTION_POINTER_UP: {
                final int index = event.getActionIndex();
                forward(event, event.getPointerId(index), PHASE_UP, timeNs);
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                final int count = event.getPointerCount();
                for (int i = 0; i < count; i++) {
                    forward(event, event.getPointerId(i), PHASE_CANCEL, timeNs);
                }
                return true;
            }
            default:
                return super.onTouchEvent(event);
        }
    }

    private void forward(MotionEvent event, int pointerId, int phase, long timeNs) {
        PulsePoint.nativePointer(pointerId, phase, event.getX(pointerId), event.getY(pointerId), timeNs);
    }

    @Override
    public boolean performClick() {
        return super.performClick();
    }

    /** GL thread.  Owns nothing but the frame callback. */
    private static final class FrameRenderer implements GLSurfaceView.Renderer {

        @Override
        public void onSurfaceCreated(GL10 unused, EGLConfig config) {
            // All GL objects are created inside the native App, so a context loss is
            // handled by the Activity recreating the surface.
        }

        @Override
        public void onSurfaceChanged(GL10 unused, int width, int height) {
            GLES30.glViewport(0, 0, width, height);
        }

        @Override
        public void onDrawFrame(GL10 unused) {
            PulsePoint.nativeDrawFrame();
        }
    }

    float density() {
        DisplayMetrics m = getResources().getDisplayMetrics();
        return m.density;
    }
}
