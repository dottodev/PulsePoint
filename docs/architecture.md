# Architecture

## Layers

```
  platform/jni_bridge.cpp        input in, geometry in, audio out
          |
  game/app.{h,cpp}               lifecycle, input routing, state machine
    ui/screens.cpp               menu, mode select, ready, gameplay
    ui/screens2.cpp              result, achievements, statistics, settings, overlay
      |
      +-- game/modes.{h,cpp}     rules + a view model, no drawing
      +-- game/session.{h,cpp}   presses, samples, streaks, score
      +-- meta/*                 statistics, achievements, settings, save file
      +-- ui/widgets.{h,cpp}     immediate mode with persistent feel
      +-- ui/particles.*         the effect system
      +-- ui/icons.*             thirty procedural achievement icons
      +-- gfx/renderer.*         two draw calls
      +-- audio/synth.*          oscillators and a pad
```

The rule that shapes everything: **a mode never draws and a renderer never knows
what a target is.** A mode maintains targets, decides what a tap did, updates the
session, and fills a `RoundView` — a plain struct of targets and effect requests.
The renderer and the UI read that struct.

That is what lets all four modes be tested exhaustively on a desktop with no GPU
(`pulsepoint_tests`), and it is why adding a fifth mode touches exactly one new
file plus a row in a table.

## The frame

`App::onDrawFrame`, on the GL thread:

```
  now = monotonicNow()
  frameNs = now - lastFrame        smoothed into avgFrameNs  (drives AUTO calibration)
  drainInput(now)                  taps -> pointer state, and straight to the round
  updatePlaying(now)               mode update, effect requests consumed
  updateAmbient(now, dt)           transition, overlay queue, FX
  applyPendingResults(now)         score count-up
  saveProfileIfDirty(now)          debounced, atomic

  renderer.beginFrame()
  drawBackground()                 grid, motes, corner brackets
  draw<Screen>()                   one of nine
  drawTransition()                 the wipe
  drawAchievementOverlay()         if one is queued
  drawDebug()                      if enabled
  renderer.endFrame()              shapes, then text: two draw calls
```

Input is drained **before** the update, so a tap on the frame a target appears is
delivered to a round that has already made the target answerable. That ordering is
load-bearing: the other order would reject a legitimate press as an early one.

## Timing across two threads

```
  UI thread                          GL thread
  ---------                          ---------
  onTouchEvent                       onDrawFrame
    t = elapsedRealtimeNanos()        now = monotonicNow()
    input().push(sample{t, pos})      drainInput -> deliverTapToRound
                                       mode.onPress(pos, t)
                                         reaction = t - stimulusNs
```

`InputRouter` is a wait-free single-producer/single-consumer ring of 32 slots. The
producer never blocks and drops rather than stalling if the consumer falls behind;
`droppedSamples()` is surfaced in the debug overlay so that case is visible rather
than silent. The audio thread is a third, entirely separate consumer of the
synth, which is single-threaded by construction.

## Two programs, two buffers

`gfx/shaders.h` and `gfx/renderer.cpp`.

One vertex format, sixteen floats:

| slot | attribute | shapes | text |
|---|---|---|---|
| 0 | `a_pos` | pixels, y down | pixels, y down |
| 1 | `a_uv` | offset from the quad centre, in pixels | atlas texcoord |
| 2 | `a_color` | rgba | rgba |
| 3 | `a_params` | p0..p3 | outline width, glow width |
| 4 | `a_shape` | halfW, halfH, kind, spare | unused |

Shapes are SDFs evaluated per fragment. Every kind returns a signed distance in
**pixels** — `a_uv` already carries pixel offsets and `a_shape` carries the box
half-extents — so `fwidth(d)` is exactly one screen pixel and the antialiasing is
identical for a hairline and a panel. Kinds: rect, disc, ring, ring arc, segment,
glow, triangle, vignette — eight in total. Everything else in the game, including
all thirty achievement icons and the FOCUS target marks, is composed from those.

Two conventions that are easy to get wrong: a ring arc's quad is the whole box
(radius + thickness + a pixel), so the true radius travels in `p3`
(`v_params.w`) and the shader must use that, not the box half-extent; and the
glow distance is signed like the rest (negative inside, zero on the rim), so the
quad corners fade to nothing instead of sitting at half coverage as a visible
square plate on a black field.

Text is one `GL_R8` atlas. The field is a coverage ramp with 0.5 on the contour, so
one `smoothstep` at half the screen-space gradient is a clean edge at any size, and
`fwidth` does the scaling for free.

Both batches are plain arrays refilled each frame and uploaded once. A frame that
would exceed a batch is truncated rather than grown: missing a decoration is
invisible, allocating on the input path is not.

## The font pipeline

`tools/gen_font_atlas.py` rasterises two variable OpenType fonts at a 32 px em into
single-channel distance fields, shelf-packs them into one 1024x309 texture, encodes
that as an 8-bit greyscale PNG and embeds it base64 in a generated C++ source.

The runtime (`generated/font_atlas_decode.cpp`) base64-decodes it, inflates it with
zlib and undoes the five PNG row filters. That is a deliberate trade: a 28 KB
compressed atlas in the binary beats a 300 KB uncompressed one, and beats a 300 KB
`.png` in `assets/` plus an image decoder plus a filesystem path.

`tools/preview_font.py` is a CPU reference of the shader maths, so the atlas can be
eyeballed without building anything.

## Persistence

`meta/profile.{h,cpp}` and `meta/profile_io.cpp`.

One packed, versioned, CRC32'd struct. The write is temp file → `fsync` →
`rename`, with the previous good copy kept as `.bak` and used automatically if the
primary fails to validate. A corrupt, truncated, out-of-range or wrong-version blob
is rejected outright and the caller falls back to a fresh profile rather than
trusting it.

`Settings::calibrationMs` is clamped on load, so a hand-edited file cannot inject an
absurd value.

## Testing strategy

Three binaries, three different failure modes they catch.

| Binary | Catches |
|---|---|
| `pulsepoint_tests` | rules, timing, scoring, statistics, achievements, save file, font atlas |
| `pulsepoint_linktest` | missing initialisation in a screen, a broken state transition, a bad layout rect, an unresumed pool |
| `tools/check_platform.sh` | a misspelled native method, a wrong signature, a missing include |
| `tools/check_jni.py` | a JNI/Java mismatch, which is a runtime crash and nothing else catches it |

The gap is the GPU. It is narrowed rather than closed by keeping the renderer to
analytic distance fields, so the interesting arithmetic is in the CPU tests and the
only thing left unverified is whether the driver agrees.
