#!/usr/bin/env bash
#
# Builds a signed PulsePoint APK without Gradle.
#
# Why this exists: the project is one Activity, one GLSurfaceView and one shared
# object, so a full Android build system is mostly ceremony.  This script does
# the same four steps Gradle does -- compile the native library, compile and dex
# the Java, package the resources, sign the result -- and nothing else.
#
# It is also the path that cannot drift from the CMake build, because it compiles
# exactly the same source list with the same flags.
#
# Requirements, all of which the Android SDK manager already installs:
#   ANDROID_SDK_ROOT   the SDK, with platform-tools and build-tools
#   ANDROID_NDK_HOME   the NDK (27.x recommended; 26.x also works)
#   A JDK              javac and keytool
#
# Usage:
#   tools/build_apk.sh                 # release build, signed with a generated key
#   tools/build_apk.sh --debug         # debuggable build
#   tools/build_apk.sh --abi arm64-v8a # one ABI only (much faster to iterate)
#
set -euo pipefail

# A JAVA_HOME, when set, wins for javac/keytool and for the d8/apksigner
# wrapper scripts.  (Some machines have several JDKs; the newest is not always
# the one that runs.)
[ -n "${JAVA_HOME:-}" ] && export PATH="$JAVA_HOME/bin:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-apk"
CPP="$ROOT/app/src/main/cpp"
JAVA_SRC="$ROOT/app/src/main/java"
RES="$ROOT/app/src/main/res"
MANIFEST="$ROOT/app/src/main/AndroidManifest.xml"

BUILD_TYPE="release"
ABIS=("arm64-v8a" "armeabi-v7a" "x86_64")
COMPILE_SDK=34
MIN_SDK=24
TARGET_SDK=34
VERSION_CODE="${VERSION_CODE:-1}"
VERSION_NAME="${VERSION_NAME:-1.0}"

while [ $# -gt 0 ]; do
  case "$1" in
    --debug) BUILD_TYPE="debug"; shift ;;
    --abi) shift; ABIS=("$1"); shift ;;
    --help|-h) sed -n '2,25p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

# ---------------------------------------------------------------------------
# Toolchain discovery
# ---------------------------------------------------------------------------

: "${ANDROID_SDK_ROOT:?set ANDROID_SDK_ROOT to your Android SDK}"

find_build_tools() {
  local best="" best_ver=0
  for d in "$ANDROID_SDK_ROOT"/build-tools/*/; do
    [ -x "$d/aapt2" ] || continue
    local ver
    ver="$(basename "$d" | sed 's/\..*//')"
    if [ "$ver" -gt "$best_ver" ] 2>/dev/null; then best_ver="$ver"; best="$d"; fi
  done
  echo "$best"
}

BUILD_TOOLS="$(find_build_tools)"
[ -n "$BUILD_TOOLS" ] || { echo "no build-tools with aapt2 found in $ANDROID_SDK_ROOT" >&2; exit 1; }

if [ -n "${ANDROID_NDK_HOME:-}" ]; then
  NDK="$ANDROID_NDK_HOME"
elif [ -n "${ANDROID_NDK_ROOT:-}" ]; then
  NDK="$ANDROID_NDK_ROOT"
else
  NDK="$(ls -d "$ANDROID_SDK_ROOT"/ndk/*/ 2>/dev/null | tail -1 || true)"
fi
[ -n "$NDK" ] && [ -d "$NDK" ] || {
  echo "no NDK found; set ANDROID_NDK_HOME" >&2; exit 1;
}

SYSROOT="$NDK/toolchains/llvm/prebuilt"
case "$(uname -s)" in
  Darwin) HOST_TAG="darwin-x86_64" ;;
  *)      HOST_TAG="linux-x86_64" ;;
esac
TOOLCHAIN="$SYSROOT/$HOST_TAG"
[ -d "$TOOLCHAIN" ] || { echo "no prebuilt toolchain at $TOOLCHAIN" >&2; exit 1; }
SYSROOT_NDK="$TOOLCHAIN/sysroot"

# The NDK only ships compiler binaries for x86-64 hosts.  On anything else
# (Linux on ARM64, for example) the prebuilt clang cannot execute, so a host
# clang targeting Android with the NDK sysroot is used instead: the compiler
# is retargetable, the sysroot is data.  Probed by execution, not by uname,
# because an x86-64 box with a broken NDK deserves the same fallback.
NDK_CXX_PROBE="$TOOLCHAIN/bin/aarch64-linux-android24-clang++"
if "$NDK_CXX_PROBE" --version >/dev/null 2>&1; then
  USE_HOST_CLANG=0
else
  USE_HOST_CLANG=1
  command -v clang++ >/dev/null || { echo "no NDK compiler and no host clang++" >&2; exit 1; }
  echo "note: NDK compiler not runnable here; using host clang++ with the NDK sysroot"
fi

# The glibc-linked aapt2/zipalign cannot run on musl; the two symbols they miss
# are shimmed with a twelve-line library built on the spot.  Native glibc hosts
# never take this path.  (Runs after "$BUILD" exists; see below.)
need_sdk_shim() {
  ! "$BUILD_TOOLS/aapt2" version >/dev/null 2>&1
}

# The NDK's clang is named after its target, so the API level is baked into the
# binary name rather than passed as a flag.
find_clang() {
  local api="$1" abi="$2"
  local name
  case "$abi" in
    arm64-v8a)  name="aarch64-linux-android" ;;
    armeabi-v7a) name="armv7a-linux-androideabi" ;;
    x86_64)     name="x86_64-linux-android" ;;
    *) echo "unsupported ABI: $abi" >&2; exit 1 ;;
  esac
  local cxx="$TOOLCHAIN/bin/${name}${api}-clang++"
  [ -x "$cxx" ] || { echo "missing compiler: $cxx" >&2; exit 1; }
  echo "$cxx"
}

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

read -r -d '' NATIVE_SOURCES <<'EOF' || true
core/clock.cpp
core/input.cpp
core/rng.cpp
game/app.cpp
game/modes.cpp
game/session.cpp
gfx/renderer.cpp
generated/font_atlas_data.cpp
generated/font_atlas_decode.cpp
meta/achievements.cpp
meta/profile.cpp
meta/profile_io.cpp
meta/settings.cpp
meta/stats.cpp
platform/jni_bridge.cpp
audio/synth.cpp
ui/icons.cpp
ui/particles.cpp
ui/screens.cpp
ui/screens2.cpp
ui/widgets.cpp
EOF

if [ "$BUILD_TYPE" = "debug" ]; then
  OPT="-O1 -g -fno-omit-frame-pointer"
  LDEXTRA=""
else
  OPT="-O3 -DNDEBUG"
  LDEXTRA="-Wl,--gc-sections -Wl,-z,max-page-size=16384"
fi

# -ffast-math is deliberately absent: the scoring and timing maths is the product.
CXXFLAGS="-std=c++17 -fexceptions -fvisibility=hidden -ffunction-sections -fdata-sections \
-Wall -Wextra -Wshadow -Wno-unused-parameter $OPT"

rm -rf "$BUILD"
mkdir -p "$BUILD"

if need_sdk_shim; then
  command -v clang++ >/dev/null || { echo "aapt2 cannot run and no host clang++ for the shim" >&2; exit 1; }
  echo "note: building musl shim for the SDK native tools"
  cat > "$BUILD/host-shim.c" <<'SHIMEOF'
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/random.h>
#ifdef __cplusplus
extern "C" {
#endif
void arc4random_buf(void* buf, size_t n) {
  size_t off = 0;
  while (off < n) {
    ssize_t r = getrandom((char*)buf + off, n - off, 0);
    if (r <= 0) break;
    off += (size_t)r;
  }
}
int fcntl64(int fd, int cmd, ...) {
  va_list ap; va_start(ap, cmd);
  void* arg = va_arg(ap, void*);
  va_end(ap);
  return fcntl(fd, cmd, arg);
}
#ifdef __cplusplus
}
#endif
SHIMEOF
  clang++ -fuse-ld=lld -shared -o "$BUILD/host-shim.so" "$BUILD/host-shim.c" 2>/dev/null || \
  clang++ -shared -o "$BUILD/host-shim.so" "$BUILD/host-shim.c"
  export LD_PRELOAD="$BUILD/host-shim.so"
  "$BUILD_TOOLS/aapt2" version >/dev/null || { echo "aapt2 still cannot run" >&2; exit 1; }
fi

# Host-clang cross build: one compiler entry per ABI.  Triple is the NDK
# sysroot directory name; target is what host clang wants; builtins is the
# compiler-rt archive the NDK ships as data; linker is the on-device loader.
host_abi_params() {
  case "$1" in
    arm64-v8a)   echo "aarch64-linux-android  aarch64-linux-android  libclang_rt.builtins-aarch64-android.a  /system/bin/linker64  " ;;
    armeabi-v7a) echo "armv7a-linux-androideabi arm-linux-androideabi libclang_rt.builtins-arm-android.a  /system/bin/linker -march=armv7-a -mfloat-abi=softfp" ;;
    x86_64)      echo "x86_64-linux-android  x86_64-linux-android  libclang_rt.builtins-x86_64-android.a  /system/bin/linker64  " ;;
  esac
}

echo "==> native libraries"
for abi in "${ABIS[@]}"; do
  api="$MIN_SDK"
  [ "$abi" = "armeabi-v7a" ] && api=24
  outdir="$BUILD/lib/$abi"
  mkdir -p "$outdir"
  srcs=()
  while IFS= read -r f; do [ -n "$f" ] && srcs+=("$CPP/$f"); done <<< "$NATIVE_SOURCES"
  echo "    $abi  (${#srcs[@]} sources)"
  # shellcheck disable=SC2086
  read -r target triple builtins linker extra <<< "$(host_abi_params "$abi")"
  if [ "$USE_HOST_CLANG" = 1 ]; then
    # shellcheck disable=SC2086
    apilib="$SYSROOT_NDK/usr/lib/$triple/$api"
    clang_resource="$SYSROOT_NDK/../lib/clang"
    builtins_a="$(echo $clang_resource/*/lib/linux/$builtins)"
    # shellcheck disable=SC2086
    clang++ -target "$target$api" --sysroot="$SYSROOT_NDK" -stdlib=libc++ \
      $CXXFLAGS -fno-stack-protector -I "$CPP" \
      $extra -c "${srcs[@]}" --output="$outdir/tmp.o" 2>/dev/null || {
      # Older clangs want one -c per file; fall back to a loop.
      for s in "${srcs[@]}"; do
        b="$(basename "$s" .cpp).o"
        clang++ -target "$target$api" --sysroot="$SYSROOT_NDK" -stdlib=libc++ \
          $CXXFLAGS -fno-stack-protector -I "$CPP" \
          $extra -c "$s" -o "$outdir/$b" || exit 1
      done
    }
    objlist=("$outdir"/*.o)
    [ -f "$outdir/tmp.o" ] && objlist=("$outdir/tmp.o")
    clang++ -target "$target$api" --sysroot="$SYSROOT_NDK" -nostdlib -shared \
      -fuse-ld=lld "$apilib/crtbegin_so.o" "${objlist[@]}" "$builtins_a" \
      -L"$apilib" -llog -lGLESv3 -lEGL -lz -lm -lc -ldl \
      "$SYSROOT_NDK/usr/lib/$triple/libc++_shared.so" "$apilib/crtend_so.o" \
      -o "$outdir/libpulsepoint.so" \
      -Wl,--dynamic-linker,"$linker" -Wl,--allow-shlib-undefined $LDEXTRA
    rm -f "$outdir"/*.o
    cp "$SYSROOT_NDK/usr/lib/$triple/libc++_shared.so" "$outdir/"
  else
    cxx="$(find_clang "$api" "$abi")"
    # shellcheck disable=SC2086
    "$cxx" $CXXFLAGS -I "$CPP" -shared \
      "${srcs[@]}" \
      -o "$outdir/libpulsepoint.so" \
      -llog -lGLESv3 -lEGL -lz -lm $LDEXTRA
    cp "$SYSROOT_NDK/usr/lib/$triple/libc++_shared.so" "$outdir/" 2>/dev/null || true
  fi
  if "$TOOLCHAIN/bin/llvm-strip" --version >/dev/null 2>&1; then
    "$TOOLCHAIN/bin/llvm-strip" --strip-unneeded "$outdir/libpulsepoint.so"
  elif command -v llvm-strip >/dev/null 2>&1; then
    llvm-strip --strip-unneeded "$outdir/libpulsepoint.so"
  fi
done

echo "==> resources"
"$BUILD_TOOLS/aapt2" compile --dir "$RES" -o "$BUILD/res.zip"

echo "==> manifest and resources"
cp "$MANIFEST" "$BUILD/AndroidManifest.xml"
if [ "$BUILD_TYPE" = "debug" ]; then
  # A debuggable build needs the debuggable flag, which the manifest does not carry.
  sed -i 's|android:label="@string/app_name"|android:label="@string/app_name" android:debuggable="true"|' \
    "$BUILD/AndroidManifest.xml"
fi
if ! grep -q 'package=' "$BUILD/AndroidManifest.xml"; then
  # Raw aapt2 link has no Gradle namespace to fall back on: the package has to
  # live in the manifest.  Injected here so the source manifest keeps the
  # modern package-less style the Gradle build expects.
  sed -i 's|<manifest |<manifest package="com.pulsepoint.app" |' "$BUILD/AndroidManifest.xml"
fi

"$BUILD_TOOLS/aapt2" link \
  -o "$BUILD/base.apk" \
  -I "$ANDROID_SDK_ROOT/platforms/android-$COMPILE_SDK/android.jar" \
  --manifest "$BUILD/AndroidManifest.xml" \
  --min-sdk-version "$MIN_SDK" \
  --target-sdk-version "$TARGET_SDK" \
  --version-code "$VERSION_CODE" \
  --version-name "$VERSION_NAME" \
  --no-version-vectors \
  "$BUILD/res.zip"

echo "==> java"
mkdir -p "$BUILD/classes"
find "$JAVA_SRC" -name '*.java' > "$BUILD/sources.txt"
javac -source 17 -target 17 -encoding UTF-8 \
  -classpath "$ANDROID_SDK_ROOT/platforms/android-$COMPILE_SDK/android.jar" \
  -d "$BUILD/classes" \
  @"$BUILD/sources.txt"

echo "==> dex"
mkdir -p "$BUILD/dex"
"$BUILD_TOOLS/d8" \
  --min-api "$MIN_SDK" \
  --release \
  --lib "$ANDROID_SDK_ROOT/platforms/android-$COMPILE_SDK/android.jar" \
  --output "$BUILD/dex" \
  "$BUILD/classes/com/pulsepoint/app/"*.class

echo "==> package"
cp "$BUILD/base.apk" "$BUILD/pulsepoint-unsigned.apk"
# Native libraries go in uncompressed: the loader maps them straight out of
# the APK, and on 16 KB-page devices a compressed .so fails to install.
# (The dex stays deflated; it has no alignment requirement beyond zipalign's.)
# -D keeps directory entries out: apksigner drops them on signing, and every
# dropped byte before a .so would shift it off its 16 KB page.
# classes.dex lives at the archive root: the platform looks up exactly that
# path (StrictJarFile.findEntry), and anything else -- dex/classes.dex, for
# example -- installs to "code is missing" with no further explanation.
mv "$BUILD/dex/classes.dex" "$BUILD/classes.dex"
rmdir "$BUILD/dex"
( cd "$BUILD" && zip -q -X "pulsepoint-unsigned.apk" "classes.dex" \
  && zip -q -X -0 -r -D "pulsepoint-unsigned.apk" "lib" )
rm -f "$BUILD/classes.dex"

echo "==> align"
# tools/align_apk.py, not zipalign: the SDK's zipalign for this host silently
# no-ops on -p/-P (entry offsets do not move, yet -c reports OK), so alignment
# is done and self-checked in Python.  .so files land on 16 KB boundaries,
# which Android 15+ devices with 16 KB pages require at install time.
python3 "$ROOT/tools/align_apk.py" "$BUILD/pulsepoint-unsigned.apk" "$BUILD/pulsepoint-aligned.apk"

echo "==> sign"
# The key lives outside "$BUILD" (which is wiped every run): a fresh key per
# build would make every release uninstallable over the previous one.  Override
# with KEYSTORE_PATH for a real release key.
KEYSTORE="${KEYSTORE_PATH:-$HOME/.config/pulsepoint/debug.keystore}"
mkdir -p "$(dirname "$KEYSTORE")"
if [ ! -f "$KEYSTORE" ]; then
  keytool -genkeypair -v \
    -keystore "$KEYSTORE" \
    -storepass pulsepoint -keypass pulsepoint \
    -alias pulsepoint \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=PulsePoint Debug, OU=Development, O=PulsePoint, C=ZZ" >/dev/null 2>&1
  echo "    generated $KEYSTORE (back it up: updates must keep the same key)"
fi
# v1 (JAR) signing is off: v2 covers every device back to the minSdkVersion,
# and a present-but-unverifying v1 block is one more thing a strict installer
# can choke on.
"$BUILD_TOOLS/apksigner" sign \
  --v1-signing-enabled false --v2-signing-enabled true \
  --ks "$KEYSTORE" --ks-pass pass:pulsepoint --key-pass pass:pulsepoint \
  --out "$BUILD/PulsePoint.apk" "$BUILD/pulsepoint-aligned.apk"

"$BUILD_TOOLS/apksigner" verify --print-certs "$BUILD/PulsePoint.apk" >/dev/null

SIZE="$(du -h "$BUILD/PulsePoint.apk" | cut -f1)"
echo
echo "built $BUILD/PulsePoint.apk  ($SIZE)"
echo "install with: adb install -r $BUILD/PulsePoint.apk"
