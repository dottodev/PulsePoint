#!/usr/bin/env bash
#
# Everything that can be verified without an Android device, in one command.
#
#   1. The gameplay, scoring, statistics, achievement, save-file and input tests.
#   2. The link and screen smoke test, which compiles the renderer, every screen,
#      the audio synth and the state machine against stub GL and drives a full
#      session through them.
#   3. A type-check of the JNI bridge, the one file that needs the NDK headers.
#   4. A cross-check that the JNI bridge and the Java declarations agree.
#   5. A rebuild of the font atlas, to prove the generated sources in the tree
#      are the ones the generator produces.
#
# What this cannot do is run on a GPU, which is why the renderer is written as
# distance fields with analytic antialiasing: the maths is checkable here, and
# the only thing left for a device is whether the driver agrees.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

step "gameplay and persistence tests"
python3 tools/build_host_tests.py >/dev/null
./build-host/pulsepoint_tests

step "link and screen smoke test"
./build-host/pulsepoint_linktest

step "JNI bridge type check"
CXX="${CXX:-clang++}" tools/check_platform.sh

step "JNI bridge / Java declaration cross-check"
python3 tools/check_jni.py

step "font atlas is reproducible from its sources"
cp app/src/main/cpp/generated/font_atlas_data.cpp /tmp/pp_atlas_before.cpp
python3 tools/gen_font_atlas.py >/dev/null
if cmp -s /tmp/pp_atlas_before.cpp app/src/main/cpp/generated/font_atlas_data.cpp; then
  echo "generated atlas matches the committed one"
else
  echo "generated atlas differs from the committed one; committing the regenerated"
  echo "file above is correct and the difference is a generator or font change"
  exit 1
fi

printf '\n\033[1mall checks passed\033[0m\n'
