// Minimal <android/log.h> for desktop compile checks.  See hosttest/shim/jni.h.
#pragma once

#include <cstdarg>

extern "C" {
enum android_LogPriority {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT
};

int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap);
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
}
