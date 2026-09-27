#!/usr/bin/env python3
"""
Builds the headless test binary and the link/smoke test for PulsePoint.

Two binaries, both runnable on a desktop:

  build-host/pulsepoint_tests    game rules, scoring, statistics, achievements,
                                 save file, input de-duplication, font atlas
  build-host/pulsepoint_linktest the renderer, widget layer, every screen, the
                                 audio synth and the state machine, linked
                                 against stub OpenGL entry points and driven
                                 through a scripted session

The first exists because reaction timing and score integrity are easy to get
subtly wrong.  The second exists because a missing initialisation in a screen is
invisible until the screen is actually drawn, and on a device that means a long
round trip.

    python3 tools/build_host_tests.py
    ./build-host/pulsepoint_tests
    ./build-host/pulsepoint_linktest
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build-host")
CPP = os.path.join(ROOT, "app", "src", "main", "cpp")

CORE = [
    "app/src/main/cpp/core/clock.cpp",
    "app/src/main/cpp/core/rng.cpp",
    "app/src/main/cpp/core/input.cpp",
    "app/src/main/cpp/audio/synth.cpp",
    "app/src/main/cpp/game/session.cpp",
    "app/src/main/cpp/game/modes.cpp",
    "app/src/main/cpp/meta/stats.cpp",
    "app/src/main/cpp/meta/settings.cpp",
    "app/src/main/cpp/meta/achievements.cpp",
    "app/src/main/cpp/meta/profile.cpp",
    "app/src/main/cpp/meta/profile_io.cpp",
    "app/src/main/cpp/generated/font_atlas_data.cpp",
    "app/src/main/cpp/generated/font_atlas_decode.cpp",
]

GFX = [
    "app/src/main/cpp/gfx/renderer.cpp",
    "app/src/main/cpp/ui/particles.cpp",
    "app/src/main/cpp/ui/widgets.cpp",
    "app/src/main/cpp/ui/icons.cpp",
    "app/src/main/cpp/ui/screens.cpp",
    "app/src/main/cpp/ui/screens2.cpp",
    "app/src/main/cpp/game/app.cpp",
]

TARGETS = [
    ("pulsepoint_tests", ["hosttest/tests.cpp"] + CORE),
    ("pulsepoint_linktest", ["hosttest/linktest.cpp"] + CORE + GFX),
    ("pulsepoint_softrender", ["hosttest/softrender.cpp"] + CORE + GFX),
]

FLAGS = [
    "-std=c++17",
    "-O1",
    "-g",
    "-Wall",
    "-Wextra",
    "-Wshadow",
    "-Wno-unused-parameter",
    "-Werror=return-type",
    "-Werror=uninitialized",
]


def pick_linker(compiler):
    """Some sandboxes block the system `ld` but leave lld runnable."""
    def links(flag):
        src = os.path.join(BUILD, ".probe.cpp")
        with open(src, "w") as fh:
            fh.write("int main(){return 0;}\n")
        flags = [compiler, "-x", "c++", src, "-o", os.path.join(BUILD, ".probe")]
        if flag:
            flags.insert(1, "-fuse-ld=" + flag)
        rc = subprocess.call(flags, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for f in (src, os.path.join(BUILD, ".probe")):
            if os.path.exists(f):
                os.remove(f)
        return rc == 0

    chosen = os.environ.get("HOST_LD", "")
    if not chosen and not links("") and links("lld"):
        chosen = "lld"
    return chosen


def main():
    compiler = os.environ.get("CXX", "clang++")
    if subprocess.call(["which", compiler], stdout=subprocess.DEVNULL) != 0:
        sys.exit("%s not found; set CXX to a C++17 compiler" % compiler)
    os.makedirs(BUILD, exist_ok=True)
    linker = pick_linker(compiler)

    only = sys.argv[1] if len(sys.argv) > 1 else None
    failed = False
    for name, sources in TARGETS:
        if only and only not in name:
            continue
        out = os.path.join(BUILD, name)
        cmd = [compiler] + FLAGS
        if linker:
            cmd.append("-fuse-ld=" + linker)
        cmd += ["-I", CPP]
        cmd += [os.path.join(ROOT, s) for s in sources]
        cmd += ["-o", out, "-lz", "-lm", "-lpthread"]
        print("building %s (%d sources)" % (name, len(sources)))
        if subprocess.call(cmd) != 0:
            failed = True
            continue
        print("  -> %s" % out)
    if failed:
        sys.exit(1)
    print(
        "\nrun:\n  %s/pulsepoint_tests\n  %s/pulsepoint_linktest\n"
        "  %s/pulsepoint_softrender build-host/frames && python3 tools/softrender.py"
        % (BUILD, BUILD, BUILD)
    )


if __name__ == "__main__":
    main()
