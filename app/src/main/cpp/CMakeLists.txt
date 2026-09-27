# PulsePoint native build.
#
# Two things worth noting:
#   * Only zlib is linked, and only for the font atlas.  Everything else --
#     rendering, audio, input, persistence, randomness -- is first-party code or
#     a platform library.  There is no physics engine, no asset loader, no UI
#     toolkit and no network stack in the binary.
#   * -ffast-math is deliberately NOT used.  The scoring maths and the reaction
#     timing are the product, and reproducible floating point is worth more here
#     than the last few percent of shader throughput.

cmake_minimum_required(VERSION 3.22.1)
project(pulsepoint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pulsepoint SHARED
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
)

target_include_directories(pulsepoint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(ZLIB REQUIRED)
target_link_libraries(pulsepoint
    PRIVATE
        ZLIB::ZLIB      # font atlas inflate
        GLESv3          # shapes, SDF text
        EGL             # surface lifetime
        android         # log, window insets
        log
)

target_compile_options(pulsepoint PRIVATE
    -Wall
    -Wextra
    -Wshadow
    -Wno-unused-parameter
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-O1>
)

# Keeps the frame path small enough to stay in the instruction cache on a
# mid-range ARM core, and lets the linker drop the rest.
target_link_options(pulsepoint PRIVATE
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
)
