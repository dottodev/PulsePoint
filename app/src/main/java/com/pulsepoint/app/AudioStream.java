package com.pulsepoint.app;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Build;
import android.util.Log;

/**
 * Streams the native synth into an {@link AudioTrack}.
 *
 * <p>Sound is synthesised in C++ and pulled from a dedicated thread through a direct
 * buffer.  That is the whole audio architecture: there are no sample assets, no decoder
 * and no file handles, and a feedback sound can be pitched by the gameplay value that
 * produced it.
 *
 * <p>Latency is not on the measured path -- nothing in the game reacts to audio -- so the
 * buffer is sized for stability rather than for minimum latency.  The track is asked for
 * low-latency mode where the platform offers it and quietly falls back where it does not.
 *
 * <p>The thread exits when {@link #stop()} is called or the track fails; a broken audio
 * device must never take the game down with it.
 */
final class AudioStream {

    private static final String TAG = "PulsePoint";
    private static final int FRAMES_PER_BURST = 256;
    private static final int BYTES_PER_FRAME = 4; // stereo, 16-bit

    private AudioTrack track;
    private Thread thread;
    private volatile boolean running;

    void start() {
        if (running) return;

        final int minBytes = AudioTrack.getMinBufferSize(
                48000, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        // Four bursts is the smallest buffer that survives a scheduling hiccup without
        // adding audible delay to a transient.
        final int bufferBytes = Math.max(minBytes, FRAMES_PER_BURST * 4 * 2 * 2);

        try {
            AudioTrack.Builder builder =
                    new AudioTrack.Builder()
                            .setAudioAttributes(
                                    new AudioAttributes.Builder()
                                            .setUsage(AudioAttributes.USAGE_GAME)
                                            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                                            .build())
                            .setAudioFormat(
                                    new AudioFormat.Builder()
                                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                            .setSampleRate(48000)
                                            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                                            .build())
                            .setTransferMode(AudioTrack.MODE_STREAM)
                            .setBufferSizeInBytes(bufferBytes);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                builder.setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY);
            }
            track = builder.build();
        } catch (Exception e) {
            Log.w(TAG, "audio unavailable, continuing without sound", e);
            track = null;
            return;
        }

        if (track.getState() != AudioTrack.STATE_INITIALIZED) {
            Log.w(TAG, "audio track failed to initialise, continuing without sound");
            track.release();
            track = null;
            return;
        }

        final int sampleRate = track.getSampleRate();
        PulsePoint.nativeSetAudioSampleRate(sampleRate);
        track.play();

        running = true;
        thread = new Thread(this::pump, "PulsePointAudio");
        thread.setPriority(Thread.MAX_PRIORITY);
        thread.start();
    }

    /** Pulls frames from the synth and hands them to the track until stopped. */
    private void pump() {
        final int frames = FRAMES_PER_BURST;
        final int bytesPerFrame = BYTES_PER_FRAME;
        // A direct buffer so the synth writes straight into the track's pipe with no
        // intermediate copy and no Java-side array churn.
        final java.nio.ByteBuffer buffer = java.nio.ByteBuffer.allocateDirect(frames * bytesPerFrame);

        while (running && track != null) {
            final int written = PulsePoint.nativeRenderAudio(buffer, frames * bytesPerFrame);
            if (written < bytesPerFrame) {
                // Nothing to play: yield rather than spin.
                try {
                    Thread.sleep(2);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
                continue;
            }
            // Blocking mode gives the thread its own back-pressure, so the synth only ever
            // renders what the device is ready for.
            final int result = track.write(buffer, written, AudioTrack.WRITE_BLOCKING);
            if (result < 0) {
                Log.w(TAG, "audio write failed, stopping the stream");
                return;
            }
        }
    }

    void stop() {
        running = false;
        final Thread t = thread;
        thread = null;
        if (t != null) {
            t.interrupt();
            try {
                t.join(200);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
        final AudioTrack tr = track;
        track = null;
        if (tr != null) {
            try {
                tr.stop();
            } catch (IllegalStateException ignored) {
                // Already stopped; nothing to do.
            }
            tr.release();
        }
    }
}
