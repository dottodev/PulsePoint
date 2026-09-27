# Timing, measurement and what the numbers mean

This is the part of PulsePoint worth reading carefully, because a reaction-time
game that reports the wrong number is worse than no game at all.

## The clock

One clock, everywhere:

```cpp
clock_gettime(CLOCK_MONOTONIC_RAW)
```

`core/clock.cpp` falls back to `CLOCK_MONOTONIC` where `CLOCK_MONOTONIC_RAW` is
unavailable. `RAW` is not slewed by NTP, so it is the steadier of the two and the
one to prefer when measuring intervals.

Nothing in the gameplay path derives a duration from anything else. There is no
frame counter in the timing code, no animation duration feeding a measurement, no
UI timer, and no vsync callback. `grep` for `deltaTime` in `game/` finds nothing
because there is no such thing.

## The two timestamps

**Stimulus.** A mode makes a target answerable inside `ModeRunner::update(now)`,
where `now` is `monotonicNow()` sampled at the top of the frame on the GL thread.
That value becomes `activeStimulusNs_` and is the stimulus timestamp.

**Input.** `GameView.onTouchEvent` samples `SystemClock.elapsedRealtimeNanos()`
on its first line, before any dispatch, any view lookup and any allocation, and
forwards it to native. The Java and C++ clocks are the same hardware counter, so
the difference needs no offset:

```
reactionTime = inputTimestamp - stimulusTimestamp
```

If the tap had been forwarded with `queueEvent()` — which is the obvious way to
get work from the UI thread onto the GL thread — the timestamp would still be
correct, but the *processing* would be deferred to the start of the next frame,
up to 16 ms later. That is harmless for the timestamp and worth knowing about for
everything else.

## Precision

Reaction times are stored in **microseconds**, as `uint32_t` inside `RoundSummary`.
They are displayed in whole milliseconds. Two stimuli 500 microseconds apart are
distinct in the record and identical on screen, which is the intended relationship:
the stored value is finer than anything shown, so a player who chases a number
they cannot see is chasing something the app has actually measured.

## Display latency compensation

A stimulus is stamped when the frame containing it is *decided*. The photons
reach the eye roughly one to two frames later. The measurement therefore contains
a systematic positive bias of order half a frame interval.

`App::calibrationMs()` subtracts it:

| Setting | Behaviour |
|---|---|
| `AUTO` (default) | half the exponentially smoothed frame interval, clamped to 0–22 ms |
| `0` | no compensation |
| `1`–`30` | fixed, for an unusual panel or an external display |

This is a measurement correction, not a difficulty adjustment: it makes the number
describe the interval between seeing the target and moving the finger, which is
the thing a player is actually training. It is reported in the debug overlay and
visible in Settings, and a frame that hitches is clamped out of the average so a
stall cannot inflate or erase a score.

## Input de-duplication

`core/input.cpp`. Three rules, all applied on the UI thread at capture time:

1. **A 9 ms floor** between accepted taps. The fastest double tap a human can
   produce is roughly 40 ms, so this cannot touch real play; it removes a duplicate
   delivered with an identical timestamp.
2. **A same-spot window**: a tap within 24 ms of the previous one and within 4 dp
   of its position is a duplicate delivered a frame later. Two genuinely distinct
   taps in that window at that distance are not a thing a finger does.
3. **One finger.** A second pointer landing while the first is down is a
   multi-touch gesture, not a measurement, and is dropped.

`TOTAL PRESSES` increments only for taps that survive all three, so a duplicate
event can never inflate the counter the whole progression is built on.

## Why `TOTAL PRESSES` is not `TOTAL HITS + TOTAL MISSES`

Timeouts are misses and cost points, but they are not presses — nothing was
pressed. The statistics screen therefore shows presses, hits, misses and false
starts as four independent counters, and the sum of presses is not expected to
equal the sum of misses. The link test asserts only the invariant that actually
holds: presses outnumber hits.

## Audio latency is not in the path

Sound is synthesised in C++ and streamed into an `AudioTrack` from its own thread.
Nothing in the game reacts to audio, so audio latency cannot affect a measurement.
The track is asked for low-latency mode where the platform offers it and the
buffer is sized for stability rather than for minimum delay, because the two goals
conflict and only one of them matters here.

## What is deliberately not instrumented

* No per-frame allocation. The render batches are fixed-size arrays, the particle
  pool is fixed, the widget slot table is fixed, and a frame that would exceed a
  batch is truncated rather than grown. `hosttest/tests.cpp` and the link test
  both run thousands of frames without an allocation.
* No render targets, no post-processing, no feedback buffers. Two draw calls per
  frame and a clear.
* No `ffast-math`. Reproducible floating point is worth more than the last few
  percent of shader throughput when the arithmetic is the score.
