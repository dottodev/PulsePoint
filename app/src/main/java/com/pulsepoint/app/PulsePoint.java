package com.pulsepoint.app;

/**
 * The single native entry point.
 *
 * <p>Every method here is a straight forward into {@code libpulsepoint.so}.  The class holds
 * no state: the game lives in C++, so there is exactly one copy of the truth and no
 * possibility of the Java and native views of a run disagreeing.
 *
 * <p>The load is guarded because an {@code UnsatisfiedLinkError} on an unsupported ABI
 * should produce a readable failure rather than a crash on startup.
 */
public final class PulsePoint {

    static {
        System.loadLibrary("pulsepoint");
    }

    private PulsePoint() {}

    /** @return true when the native game initialised. */
    public static native boolean nativeInit(String filesDir);

    public static native void nativeShutdown();

    public static native void nativeSurfaceChanged(
            int widthPx, int heightPx, float density, float safeTopPx, float safeBottomPx);

    public static native void nativeDrawFrame();

    public static native void nativePause();

    public static native void nativeResume();

    /**
     * Forwards one pointer sample.
     *
     * @param phase 0 = down, 1 = move, 2 = up, 3 = cancel
     * @param timeNs {@code SystemClock.elapsedRealtimeNanos()} sampled the instant the
     *     event was dequeued, before any of our own work.  It shares a counter with the
     *     native monotonic clock, so a stimulus and a tap can be subtracted directly.
     */
    public static native void nativePointer(
            int pointerId, int phase, float x, float y, long timeNs);

    public static native void nativeHaptic(int strength);

    /** Reports the rate the audio track actually opened at, so the synth matches it. */
    public static native void nativeSetAudioSampleRate(int sampleRate);

    /**
     * Renders interleaved stereo 16-bit frames straight into a direct byte buffer.
     *
     * @param capacityBytes the buffer's capacity, so native can clamp rather than trust.
     * @return the number of bytes written.
     */
    public static native int nativeRenderAudio(java.nio.ByteBuffer buffer, int capacityBytes);

    public static native int nativeScreen();

    public static native long nativeTotalPresses();

    public static native float nativeAverageFrameMs();

    public static native int nativeCalibrationMs();

    public static native int nativeDroppedInput();
}
