# Scoring

A score that rewards speed alone is a score that rewards gambling. A score that
rewards accuracy alone is a score that rewards slowness. PulsePoint's formula is
built to make both of those losing strategies, and the tests assert it directly
rather than trusting the reasoning.

## The formula

Per successful hit:

```
speed       = clamp01((refMs - reactionMs) / refMs)
speedPoints = 1000 * speed^1.7

accuracy    = hits / presses, evaluated at the moment of this hit
accMult     = 0.22 + 0.78 * accuracy^1.4

consistency = 1 - normalised CV of this round's samples
consMult    = 0.80 + 0.20 * consistency

streakMult  = 1 + min(streak, 32) * 0.028
levelMult   = 1 + level * 0.045, capped at 3.0        (MAX only)

hit         = speedPoints * accMult * consMult * streakMult * levelMult
```

`refMs` is the reaction time that scores zero speed points: 380 ms for REFLEX, 460
for FLICK, 560 for FOCUS, 420 for MAX. A hit slower than that still collects its
accuracy, consistency and streak multipliers, so playing carefully is never worse
than not playing.

Penalties, applied to the running total, floored at zero:

| Event | Cost |
|---|---|
| Miss (window expired) | — |
| False start (REFLEX: any unanswered press) | 150 |
| Wrong target (a FOCUS decoy) | 70 |
| Stray press on an empty screen | 25 |

## Why mashing loses

The headline test, in `hosttest/tests.cpp`:

```cpp
// Forty mashed presses, then one lucky 160 ms hit.
spam.session.registerPress(PressKind::Spam, 0, 1.0f);   // x40
spam.hit(usOf(160));

// Five clean 200 ms hits.
for (int i = 0; i < 5; ++i) clean.hit(usOf(200));

CHECK(clean.score() > spam.score() * 3);
```

The arithmetic behind it: after forty presses and one hit, `accuracy` is 1/41 and
`accMult` collapses to its floor. The hit's 340 raw speed points become about 70
after the multiplier, and the 1000 penalty points from the mash take the running
total to zero. The clean run scores roughly 2500.

The three properties that make this hold:

1. **Accuracy is evaluated per hit, not at the end.** Mashing does not retroactively
   damage points already earned — that would feel arbitrary — but it collapses the
   multiplier on every hit that follows, so the damage is entirely in the future.
2. **The floor is 0.22, not 0.** At 0% accuracy a hit is still worth something, which
   keeps a genuinely bad round from scoring a flat zero and feeling broken.
3. **The exponent is 1.4.** Without it, `accMult` would be nearly linear and a lucky
   hit after a moderate amount of spam would be competitive.

## Why slowness loses

`kSpeedExponent = 1.7` is the important number. Six 150 ms hits and six 340 ms hits
in the same round:

```
150 ms:  speed = (380-150)/380 = 0.605 -> 0.605^1.7 = 0.428 -> 428 points before multipliers
340 ms:  speed = (380-340)/380 = 0.105 -> 0.105^1.7 = 0.024 ->  24 points before multipliers
```

A curve with a linear speed term would make the second worth 86. The exponent is
what makes the gap between good and great large, which is the whole reason to play.

## Why consistency is a multiplier, not a bonus

`consistency` is `1 - CV/0.39`, clamped, where CV is the coefficient of variation of
the round's reaction samples. CV is used rather than standard deviation because
reaction times are naturally log-normal and scale with the player: a 240 ± 30 ms
player and a 190 ± 30 ms player are equally consistent, and only CV says so.

A round needs at least six samples before consistency means anything; below that it
scores 0 rather than a flattering 1.

The multiplier is narrow on purpose — 0.80 to 1.00. Consistency is a tiebreaker, not
a strategy: you cannot become fast by being tidy.

## Why a round is a fixed length

REFLEX is 10 trials, FLICK 20 targets, FOCUS 12 groups. MAX is endless, which is the
exception that proves the rule: it is bounded by dying.

There is no way to farm. You cannot replay a single stimulus, you cannot leave a
round with its points intact without having played it, and a round in progress is
abandoned rather than resumed on the app returning to the foreground — an
interrupted reaction measurement is not a measurement.

## Level multiplier

Only MAX scales with level, and it is capped at 3.0. Elsewhere `levelFactor` is 1.0.
The reason is that a rising multiplier in a fixed-length round would make the last
target of a round worth more than the first for reasons unrelated to skill, and the
same exponent that punishes slowness would be diluted by a factor the player cannot
control.

MAX's own curve uses diminishing returns — `1 - 1/(1 + level*0.16)` — so level 200
is not literally impossible, it is just very hard.

## What is not in the score

* No time bonus. Every mode is a fixed number of stimuli, so a timer would only
  reward hesitation.
* No difficulty selection. A player picks one of four modes, not one of four
  difficulty levels, so there is no "hard" multiplier to lean on.
* No streak that survives a miss. A miss resets it to zero, which is why the
  `Unstoppable` and `Machine` achievements are worth chasing.
* No randomness in the award. The only randomness in a round is where the targets
  appear, and the round is driven entirely from a seed that `ModeRunner` exposes, so
  any round can be replayed exactly — which is how the determinism tests work.
