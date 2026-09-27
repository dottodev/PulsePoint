// A minimal jni.h and android/log.h, for compile-checking the JNI bridge on a
// desktop.  It declares exactly the entry points the bridge uses, with the
// signatures the NDK's jni.h gives them.
//
// This exists so tools/check_platform.sh can type-check
// app/src/main/cpp/platform/jni_bridge.cpp without an NDK installed.  It is
// never compiled into the app: the real NDK headers take precedence there, and
// the build scripts never put this directory on the include path for a device
// build.  Anything the bridge uses that is missing here shows up immediately as
// a compile error rather than as a link failure on a phone.
#pragma once

#include <cstdarg>
#include <cstdint>

// ---- <jni.h> ---------------------------------------------------------------

typedef signed char jbyte;
typedef unsigned short jchar;
typedef short jshort;
typedef int jint;
typedef long long jlong;
typedef unsigned char jboolean;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

class _jobject {};
class _jclass : public _jobject {};
class _jstring : public _jobject {};
class _jarray : public _jobject {};
class _jobjectArray : public _jarray {};
class _jbyteArray : public _jarray {};
class _jshortArray : public _jarray {};
class _jintArray : public _jarray {};
class _jlongArray : public _jarray {};
class _jfloatArray : public _jarray {};
class _jdoubleArray : public _jarray {};
class _jbooleanArray : public _jarray {};
class _jcharArray : public _jarray {};

class _jthrowable : public _jobject {};

// The JNI type aliases, matching the real header.
typedef _jobject* jobject;
typedef _jclass* jclass;
typedef _jstring* jstring;
typedef _jarray* jarray;
typedef _jobjectArray* jobjectArray;
typedef _jbyteArray* jbyteArray;
typedef _jshortArray* jshortArray;
typedef _jintArray* jintArray;
typedef _jlongArray* jlongArray;
typedef _jfloatArray* jfloatArray;
typedef _jdoubleArray* jdoubleArray;
typedef _jbooleanArray* jbooleanArray;
typedef _jcharArray* jcharArray;
typedef _jthrowable* jthrowable;

union _jvalue {
  jboolean z;
  jbyte b;
  jchar c;
  jshort s;
  jint i;
  jlong j;
  jfloat f;
  jdouble d;
  jobject l;
};
typedef _jvalue jvalue;

class JavaVM;
struct JNIEnv;
class JavaVMAttachArgs;

#define JNI_VERSION_1_6 0x00010006
#define JNI_FALSE 0
#define JNI_TRUE 1
#define JNIEXPORT __attribute__((visibility("default")))
#define JNIIMPORT
#define JNICALL

extern "C" {
jint JNI_OnLoad(JavaVM* vm, void* reserved);
}

// Only the members the bridge actually calls.  A real jni.h is a superset.
struct JNIEnv {
  const char* GetStringUTFChars(jstring, jboolean*);
  void ReleaseStringUTFChars(jstring, const char*);
  void* GetDirectBufferAddress(jobject);
  jsize GetDirectBufferCapacity(jobject);
};
