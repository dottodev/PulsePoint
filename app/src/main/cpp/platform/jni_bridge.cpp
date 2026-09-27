// The JNI boundary.  Deliberately thin: it forwards input, forwards geometry,
// and pulls audio frames.  No game logic lives on this side, and nothing here
// allocates or blocks on the input path.
//
// The one subtlety worth stating: pointer samples are timestamped on the UI
// thread at the instant onTouchEvent is entered, using SystemClock.elapsedRealtimeNanos.
// That is the same hardware counter clock_gettime(CLOCK_MONOTONIC) reads, so a
// stimulus stamped natively and a tap stamped here subtract directly with
// nanosecond resolution and no clock-offset correction.  queueEvent() would have
// been simpler and would have added up to a full frame of measurement error.
#include <jni.h>

#include <android/log.h>

#include <pthread.h>

#include <cstdarg>
#include <cstring>
#include <new>

#include "core/clock.h"
#include "core/input.h"
#include "game/app.h"

namespace {

constexpr const char* kTag = "PulsePoint";

pp::App* g_app = nullptr;
int g_audioSampleRate = 48000;
void logInfo(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, args);
  va_end(args);
}

}  // namespace

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

JNIEXPORT jboolean JNICALL Java_com_pulsepoint_app_PulsePoint_nativeInit(JNIEnv* env, jclass,
                                                                        jstring filesDir) {
  const char* dir = env->GetStringUTFChars(filesDir, nullptr);
  if (g_app == nullptr) g_app = new (std::nothrow) pp::App();
  if (g_app == nullptr) {
    env->ReleaseStringUTFChars(filesDir, dir);
    return JNI_FALSE;
  }
  const bool ok = g_app->init(dir);
  env->ReleaseStringUTFChars(filesDir, dir);
  if (!ok) logInfo("native init failed");
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativeShutdown(JNIEnv*, jclass) {
  if (g_app != nullptr) {
    g_app->shutdown();
    delete g_app;
    g_app = nullptr;
  }
}

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativeSurfaceChanged(
    JNIEnv*, jclass, jint width, jint height, jfloat density, jfloat safeTop, jfloat safeBottom) {
  if (g_app != nullptr) g_app->onSurfaceChanged(width, height, density, safeTop, safeBottom);
}

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativeDrawFrame(JNIEnv*, jclass) {
  if (g_app != nullptr) g_app->onDrawFrame();
}

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativePause(JNIEnv*, jclass) {
  if (g_app != nullptr) g_app->onPause();
}

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativeResume(JNIEnv*, jclass) {
  if (g_app != nullptr) g_app->onResume();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativePointer(
    JNIEnv*, jclass, jint pointerId, jint phase, jfloat x, jfloat y, jlong timeNs) {
  if (g_app == nullptr) return;
  pp::PointerSample s;
  s.pointerId = pointerId;
  switch (phase) {
    case 0: s.phase = pp::PointerPhase::Down; break;
    case 1: s.phase = pp::PointerPhase::Move; break;
    case 2: s.phase = pp::PointerPhase::Up; break;
    default: s.phase = pp::PointerPhase::Cancel; break;
  }
  s.pos = {x, y};
  s.timeNs = static_cast<pp::Nanos>(timeNs);
  s.valid = true;
  g_app->input().push(s);
}

JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativeHaptic(JNIEnv*, jclass,
                                                                     jint strength) {
  if (g_app != nullptr) g_app->onHaptic(strength);
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

// The synth renders at whatever the AudioTrack managed to open at, so the Java
// side reports it back once and the two never disagree.
JNIEXPORT void JNICALL Java_com_pulsepoint_app_PulsePoint_nativeSetAudioSampleRate(JNIEnv*, jclass,
                                                                                 jint rate) {
  if (rate > 8000) g_audioSampleRate = rate;
}

// Called from the audio thread.  The synth writes straight into the track's direct
// buffer, so there is no intermediate copy and no Java-side allocation per burst.  The
// synth is single-threaded by construction and the UI thread never touches it.
JNIEXPORT jint JNICALL Java_com_pulsepoint_app_PulsePoint_nativeRenderAudio(JNIEnv* env, jclass,
                                                                            jobject buffer,
                                                                            jint capacityBytes) {
  if (g_app == nullptr || buffer == nullptr || capacityBytes < 4) return 0;
  void* data = env->GetDirectBufferAddress(buffer);
  const jsize capacity = env->GetDirectBufferCapacity(buffer);
  if (data == nullptr || capacity <= 0) return 0;
  const jsize usable = capacity < capacityBytes ? capacity : capacityBytes;
  const int frames = static_cast<int>(usable / 4);  // stereo 16-bit
  if (frames <= 0) return 0;
  return g_app->renderAudio(static_cast<int16_t*>(data), frames) * 4;
}

// ---------------------------------------------------------------------------
// Introspection, for the debug overlay and for a smoke test on device
// ---------------------------------------------------------------------------

JNIEXPORT jint JNICALL Java_com_pulsepoint_app_PulsePoint_nativeScreen(JNIEnv*, jclass) {
  return g_app != nullptr ? static_cast<jint>(g_app->screen()) : 0;
}

JNIEXPORT jlong JNICALL Java_com_pulsepoint_app_PulsePoint_nativeTotalPresses(JNIEnv*, jclass) {
  return g_app != nullptr ? static_cast<jlong>(g_app->stats().totalPresses) : 0;
}

JNIEXPORT jfloat JNICALL Java_com_pulsepoint_app_PulsePoint_nativeAverageFrameMs(JNIEnv*, jclass) {
  return g_app != nullptr ? g_app->averageFrameMs() : 0.0f;
}

JNIEXPORT jint JNICALL Java_com_pulsepoint_app_PulsePoint_nativeCalibrationMs(JNIEnv*, jclass) {
  return g_app != nullptr ? g_app->calibrationMsUsed() : 0;
}

JNIEXPORT jint JNICALL Java_com_pulsepoint_app_PulsePoint_nativeDroppedInput(JNIEnv*, jclass) {
  return g_app != nullptr ? static_cast<jint>(g_app->droppedInputSamples()) : 0;
}

}  // extern "C"
