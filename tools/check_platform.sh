#!/usr/bin/env bash
#
# Type-checks the parts of PulsePoint that need the NDK: the JNI bridge.
#
# Everything else in the project is already covered by tools/build_host_tests.py,
# which compiles and links it for real.  The bridge is the one file that needs
# jni.h, so it gets a minimal shim and a syntax-only pass.  That is not a
# substitute for building on a device, but it catches the mistakes that actually
# happen here: a misspelled native method, a wrong signature, a missing include.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-clang++}"

FILES=(
  app/src/main/cpp/platform/jni_bridge.cpp
)

fail=0
for f in "${FILES[@]}"; do
  echo "checking $f"
  if ! "$CXX" -std=c++17 -fsyntax-only \
      -Wall -Wextra -Wshadow -Wno-unused-parameter \
      -I "$ROOT/hosttest/shim" -I "$ROOT/app/src/main/cpp" \
      "$ROOT/$f"; then
    fail=1
  fi
done
exit $fail
