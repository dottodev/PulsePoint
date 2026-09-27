// Screen drawing, part one: the menu, mode select, ready, and gameplay.
//
// Every method here is a pure function of the app's state plus the current
// time, apart from the widget layer's own slot table.  No screen owns data, no
// screen allocates, and every animation is driven by a duration from config.h
// rather than a per-frame increment, so a dropped frame shortens nothing and
// lengthens nothing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../game/app.h"
#include "../meta/stats.h"
#include "../ui/format.h"
#include "../ui/icons.h"

namespace pp {

using namespace ui_helpers;

namespace {

using namespace pp::ui;

// Widget ids.  Unique per control, so no two ever share press or scroll state.
enum : int {
  kIdMenuPlay = 1001,
  kIdMenuAchievements = 1002,
  kIdMenuStatistics = 1003,
  kIdMenuSettings = 1004,
  kIdBack = 1100,
  kIdModeReflex = 1201,
  kIdReadyStart = 1301,
  kIdPlayingAbort = 1302,
  kIdResultAgain = 1401,
  kIdResultChange = 1402,
  kIdResultMenu = 1403,
  kIdAchScroll = 1501,
  kIdStatScroll = 1601,
  kIdSetSound = 1701,
  kIdSetMusic = 1702,
  kIdSetHaptics = 1703,
  kIdSetFps = 1704,
  kIdSetCalib = 1705,
};

}  // namespace

// ---------------------------------------------------------------------------
// Shared chrome
// ---------------------------------------------------------------------------


float App::drawSubScreenHeader(Nanos now, const char* title, int backId,
                               const char* subtitle) {
  const Rect area = safeArea();
  const float t = transitioning_ ? clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                                          (transitionDur_ * 1000.0f))
                                 : 1.0f;
  const float rise = (1.0f - easeOutCubic(t)) * dp(14.0f);

  // BACK sits on its own row above the title.  Overlapping a centred heading
  // with a corner control is the classic way to end up with a button nobody can
  // read and nobody dares press.
  Style back;
  back.variant = Variant::Muted;
  back.textDp = 11.0f;
  const Rect backRect{area.x, area.y - dp(4.0f), dp(76.0f), dp(36.0f)};
  if (ui_.button(backId, backRect, "BACK", back)) {
    playSfx(audio::Sfx::UiBack, 0.9f);
    goTo(Screen::MainMenu);
  }

  const float titleY = area.y + dp(58.0f) + rise;
  ui_.textDisplay({area.centerX(), titleY}, title, cfg::kHeadingSizeDp, theme::kText,
                  HAlign::Center, VAlign::Middle, theme::kDisplayTrackingWide);
  if (subtitle != nullptr && subtitle[0] != '\0') {
    ui_.textMono({area.centerX(), titleY + dp(22.0f)}, subtitle, cfg::kLabelSizeDp,
                 theme::kTextFaint, HAlign::Center, VAlign::Middle);
  }
  const float dividerY = titleY + dp(38.0f);
  ui_.divider(area.x, area.right(), dividerY);
  return dividerY + dp(10.0f);
}

void App::drawTapPrompt(Nanos now, const char* line1, const char* line2) {
  const Rect area = safeArea();
  // A 1.6 s breathing cycle.  Slow enough not to nag, fast enough to notice.
  const float pulse = 0.55f + 0.45f * pingpong(secFromNs(elapsedSince(now, startNs_)) / 1.6f);
  ui_.textDisplay({area.centerX(), area.centerY() - dp(14.0f)}, line1, 26.0f,
                  withAlpha(theme::kBright, pulse), HAlign::Center, VAlign::Middle,
                  theme::kDisplayTrackingWide);
  if (line2 != nullptr && line2[0] != '\0') {
    ui_.textMono({area.centerX(), area.centerY() + dp(16.0f)}, line2, cfg::kLabelSizeDp,
                 theme::kTextFaint, HAlign::Center, VAlign::Middle);
  }
}

// ---------------------------------------------------------------------------
// MAIN MENU
// ---------------------------------------------------------------------------

void App::drawMenu(Nanos now) {
  const Rect area = safeArea();
  // A vignette, so the buttons at the bottom sit on a quieter field than the
  // wordmark at the top.
  renderer_.vignette({0.0f, 0.0f, surfaceWidth_, surfaceHeight_}, 0.45f, 1.15f,
                     withAlpha(theme::kVoid, 0.55f));

  const float t = transitioning_ ? clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                                          (transitionDur_ * 1000.0f))
                                 : 1.0f;
  const float intro = easeOutCubic(t);

  // Two sine terms at incommensurate periods, so the loop never visibly repeats.
  const float breath = 1.0f + 0.012f * std::sin(bgTime_ * 1.15f) + 0.006f * std::sin(bgTime_ * 0.43f);
  const float glowDp = 5.0f + 3.0f * (0.5f + 0.5f * std::sin(bgTime_ * 1.15f));

  // The wordmark is fitted to the display rather than sized in dp: ten wide
  // geometric capitals at a fixed size overflow a 1080 px screen, and the
  // overflow is invisible until you see it.  Measured at a reference size, then
  // scaled, so the tracking stays proportional.
  gfx::TextStyle measureStyle;
  measureStyle.face = &theme::display();
  measureStyle.sizePx = 100.0f;
  measureStyle.tracking = theme::kDisplayTrackingWide;
  const float referenceWidth = renderer_.measure("PULSEPOINT", measureStyle).width;
  const float maxTitleWidth = area.w * 0.90f;
  float titleSize = dp(cfg::kTitleSizeDp);
  if (referenceWidth > 1.0f) {
    titleSize = std::min(titleSize, 100.0f * maxTitleWidth / referenceWidth);
  }
  titleSize *= breath;

  gfx::TextStyle title;
  title.face = &theme::display();
  title.sizePx = titleSize;
  title.tracking = theme::kDisplayTrackingWide;
  title.color = withAlpha(theme::kBright, intro);
  title.glowPx = std::min(dp(glowDp), titleSize * 0.12f) * intro;

  // Rules above and below, drawing in from the centre, frame the wordmark
  // without a panel.
  // The rules are placed from the wordmark's own measured cap height, not from a
  // guessed fraction of the em: a geometric face with a tall cap and no descender
  // puts a fraction-based rule straight through its own letters.
  const gfx::TextMetrics titleMetrics = renderer_.measure("PULSEPOINT", title);
  const float ruleGap = titleMetrics.ascent + dp(11.0f);
  const float ruleW = titleSize * 1.18f * easeOutCubic(clamp01(t * 1.4f));
  const float titleY = area.y + area.h * 0.19f + (1.0f - intro) * dp(18.0f);

  renderer_.rect({area.centerX() - ruleW * 0.5f, titleY - ruleGap, ruleW, dp(1.0f)},
                 withAlpha(theme::kHairlineStrong, intro));
  renderer_.text(area.centerX(), titleY, "PULSEPOINT", title, gfx::HAlign::Center,
                 gfx::VAlign::Middle);
  renderer_.rect({area.centerX() - ruleW * 0.5f, titleY + ruleGap, ruleW, dp(1.0f)},
                 withAlpha(theme::kHairlineStrong, intro));

  const float belowTitle = titleY + ruleGap;
  ui_.textDisplay({area.centerX(), belowTitle + dp(24.0f)}, "TEST YOUR REFLEX.", 13.0f,
                  withAlpha(theme::kTextDim, intro), HAlign::Center, VAlign::Middle,
                  theme::kDisplayTrackingWide);

  char presses[32];
  formatCount(presses, sizeof(presses), profile_.stats.totalPresses);
  char headline[64];
  std::snprintf(headline, sizeof(headline), "%s PRESSES", presses);
  ui_.textMono({area.centerX(), belowTitle + dp(44.0f)}, headline, cfg::kLabelSizeDp,
               withAlpha(theme::kTextGhost, intro), HAlign::Center, VAlign::Middle);

  const float listTop = area.y + area.h * 0.44f;
  const float h = dp(cfg::kButtonHeightDp);
  const float gap = dp(12.0f);
  struct Item {
    int id;
    const char* label;
    Variant variant;
    Screen target;
  };
  const Item items[4] = {
      {kIdMenuPlay, "PLAY", Variant::Primary, Screen::ModeSelect},
      {kIdMenuAchievements, "ACHIEVEMENTS", Variant::Secondary, Screen::Achievements},
      {kIdMenuStatistics, "STATISTICS", Variant::Secondary, Screen::Statistics},
      {kIdMenuSettings, "SETTINGS", Variant::Secondary, Screen::Settings},
  };

  for (int i = 0; i < 4; ++i) {
    const float delay = 0.05f * static_cast<float>(i);
    const float appear = easeOutCubic(clamp01((t - delay) / std::max(0.05f, 1.0f - delay)));
    if (appear <= 0.001f) continue;
    const float y = listTop + static_cast<float>(i) * (h + gap) + (1.0f - appear) * dp(16.0f);
    Style st;
    st.variant = items[i].variant;
    st.textDp = 15.0f;
    if (ui_.button(items[i].id, {area.x, y, area.w, h}, items[i].label, st)) {
      navigate(items[i].target);
    }
  }

  // Achievement progress as a hairline at the very bottom: a reason to come back
  // without adding a screen.
  char ach[48];
  std::snprintf(ach, sizeof(ach), "%d / %d UNLOCKED", achievements_.unlockedCount,
                kAchievementCount);
  const float barY = area.bottom() - dp(30.0f);
  ui_.progressBar({area.x, barY, area.w, dp(3.0f)},
                  static_cast<float>(achievements_.unlockedCount) /
                      static_cast<float>(kAchievementCount),
                  theme::kHighlight, theme::kSurface);
  ui_.textMono({area.centerX(), barY + dp(14.0f)}, ach, cfg::kSmallSizeDp, theme::kTextFaint,
               HAlign::Center, VAlign::Middle);
}

// ---------------------------------------------------------------------------
// MODE SELECT
// ---------------------------------------------------------------------------

const char* App::modeBestLabel(Mode m, char* buf, size_t cap) const {
  const ModeStats& ms = profile_.stats.modes[static_cast<int>(m)];
  if (ms.games == 0) {
    std::snprintf(buf, cap, "NO GAMES YET");
    return buf;
  }
  char best[32];
  formatCount(best, sizeof(best), ms.bestScore);
  if (m == Mode::Max) {
    std::snprintf(buf, cap, "BEST L%u   %s", ms.bestLevel, best);
  } else {
    std::snprintf(buf, cap, "BEST %s", best);
  }
  return buf;
}

void App::drawModeSelect(Nanos now) {
  const Rect area = safeArea();
  const float top =
      drawSubScreenHeader(now, "SELECT MODE", kIdBack, "FOUR WAYS TO TEST YOUR REFLEX") + dp(4.0f);

  const float cardH = dp(84.0f);
  const float gap = dp(9.0f);
  const float t = transitioning_ ? clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                                          (transitionDur_ * 1000.0f))
                                 : 1.0f;

  for (int i = 0; i < kModeCount; ++i) {
    const Mode m = static_cast<Mode>(i);
    const ModeStats& ms = profile_.stats.modes[i];
    const float appear = easeOutCubic(clamp01((t - 0.04f * static_cast<float>(i)) / 0.7f));
    if (appear <= 0.001f) continue;
    const Rect bounds{area.x + (1.0f - appear) * dp(24.0f),
                      top + static_cast<float>(i) * (cardH + gap), area.w, cardH};

    // The whole card is the hit area, so the button is drawn first: an outlined
    // button paints its own fill, which would cover anything drawn before it.
    Style cardStyle;
    cardStyle.variant = Variant::Secondary;
    const bool picked = ui_.button(kIdModeReflex + i, bounds, "", cardStyle);

    // The preview is the mode itself, drawn live at thumbnail size: cheaper to
    // read than a paragraph and impossible to describe wrongly.
    const Rect preview{bounds.x + dp(11.0f), bounds.y + dp(11.0f), dp(58.0f), cardH - dp(22.0f)};
    ui_.panel(preview, theme::kButtonRadiusDp, theme::kSurface, dp(1.0f), theme::kHairline);
    const Vec2 pc = preview.center();
    switch (m) {
      case Mode::Reflex: {
        const float pulse = 0.4f + 0.6f * (0.5f + 0.5f * std::sin(bgTime_ * 2.4f));
        renderer_.ring(pc, dp(16.0f), dp(1.5f), theme::kHairlineStrong);
        renderer_.disc(pc, dp(8.0f) * (0.85f + 0.25f * pulse), withAlpha(theme::kBright, pulse));
        break;
      }
      case Mode::Flick: {
        renderer_.disc({pc.x - dp(14.0f), pc.y - dp(10.0f)}, dp(5.0f), theme::kBright);
        renderer_.ring({pc.x + dp(13.0f), pc.y - dp(5.0f)}, dp(4.5f), dp(1.5f), theme::kHighlight);
        renderer_.disc({pc.x - dp(7.0f), pc.y + dp(14.0f)}, dp(3.5f), theme::kTextDim);
        break;
      }
      case Mode::Focus: {
        renderer_.disc({pc.x - dp(15.0f), pc.y - dp(9.0f)}, dp(6.0f), theme::kSurfaceRaised);
        renderer_.disc({pc.x + dp(15.0f), pc.y - dp(9.0f)}, dp(6.0f), theme::kSurfaceRaised);
        renderer_.disc({pc.x, pc.y + dp(12.0f)}, dp(6.0f), theme::kSurfaceRaised);
        renderer_.disc(pc, dp(6.0f), theme::kBright);
        renderer_.disc(pc, dp(2.0f), theme::kVoid);
        break;
      }
      case Mode::Max: {
        for (int k = 0; k < 4; ++k) {
          const float a = bgTime_ * 0.9f + static_cast<float>(k) * 1.5707963f;
          renderer_.ring({pc.x + std::cos(a) * dp(9.0f), pc.y + std::sin(a) * dp(9.0f)}, dp(2.2f),
                         dp(1.2f), withAlpha(theme::kHighlight, 0.30f + 0.45f * (k / 3.0f)));
        }
        renderer_.disc(pc, dp(3.0f), theme::kBright);
        break;
      }
    }

    const float textX = preview.right() + dp(12.0f);
    ui_.textDisplay({textX, bounds.y + dp(23.0f)}, modeName(m), 17.0f, theme::kText, HAlign::Left,
                    VAlign::Middle, theme::kDisplayTrackingWide);
    ui_.textMono({textX, bounds.y + dp(43.0f)}, modeTagline(m), cfg::kLabelSizeDp,
                 theme::kTextDim, HAlign::Left, VAlign::Middle);
    char best[48];
    modeBestLabel(m, best, sizeof(best));
    // (the card's own chrome is already down)
    char reaction[24];
    formatMsOrDash(reaction, sizeof(reaction), ms.bestReactionUs);
    if (ms.bestReactionUs > 0) {
      std::snprintf(reaction, sizeof(reaction), "%s MS", reaction);
    }
    // Best score on the left, personal-best reaction on the right: two different
    // records, and picking a mode is usually about one of them specifically.
    ui_.textMono({textX, bounds.y + dp(60.0f)}, best, cfg::kSmallSizeDp, theme::kTextFaint,
                 HAlign::Left, VAlign::Middle);
    ui_.textMono({bounds.right() - dp(12.0f), bounds.y + dp(60.0f)}, reaction,
                 cfg::kSmallSizeDp, theme::kTextFaint, HAlign::Right, VAlign::Middle);

    if (picked) {
      pendingMode_ = m;
      navigate(Screen::Ready);
    }
  }

  ui_.textMono({area.centerX(), area.bottom() - dp(10.0f)}, "ONE MISTAKE ENDS A MAX RUN",
               cfg::kSmallSizeDp, theme::kTextGhost, HAlign::Center, VAlign::Middle);
}

// ---------------------------------------------------------------------------
// READY
// ---------------------------------------------------------------------------

void App::drawReady(Nanos now) {
  const Rect area = safeArea();
  const Mode m = pendingMode_;
  const float t = transitioning_ ? clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                                          (transitionDur_ * 1000.0f))
                                 : 1.0f;

  const float headY = area.y + area.h * 0.24f;
  ui_.textDisplay({area.centerX(), headY}, modeName(m), cfg::kTitleSizeDp * 0.8f,
                  withAlpha(theme::kBright, t), HAlign::Center, VAlign::Middle,
                  theme::kDisplayTrackingWide);

  // The blurb is a sentence or two of prose, so it wraps; the rules line sits
  // below whatever height it came to rather than at a guessed offset.
  const float descTop = headY + dp(32.0f);
  const float descH = ui_.textParagraph({area.centerX(), descTop}, modeDescription(m),
                                        cfg::kLabelSizeDp, withAlpha(theme::kTextDim, t),
                                        area.w - dp(28.0f), cfg::kLabelSizeDp * 1.55f);

  char rules[96];
  switch (m) {
    case Mode::Reflex:
      std::snprintf(rules, sizeof(rules), "%d TARGETS   RANDOM DELAY   TAP FAST",
                    cfg::kReflexTrials);
      break;
    case Mode::Flick:
      std::snprintf(rules, sizeof(rules), "%d TARGETS   SHRINKING   RAPID SPAWN",
                    cfg::kFlickTargetsPerRound);
      break;
    case Mode::Focus:
      std::snprintf(rules, sizeof(rules), "%d TARGETS   FIND THE BULLSEYE",
                    cfg::kFocusTargetsPerRound);
      break;
    case Mode::Max:
      std::snprintf(rules, sizeof(rules), "ENDLESS   ONE MISTAKE ENDS THE RUN");
      break;
  }
  ui_.divider(area.x + area.w * 0.18f, area.right() - area.w * 0.18f, descTop + descH + dp(13.0f));
  ui_.textMono({area.centerX(), descTop + descH + dp(28.0f)}, rules, cfg::kSmallSizeDp,
               withAlpha(theme::kTextGhost, t), HAlign::Center, VAlign::Middle);

  drawTapPrompt(now, "TAP TO START", "THE FIRST DELAY IS RANDOM");

  Style st;
  st.variant = Variant::Primary;
  st.textDp = 15.0f;
  const Rect start{area.x, area.centerY() + dp(38.0f), area.w, dp(cfg::kButtonHeightDp)};
  bool launched = ui_.button(kIdReadyStart, start, "START", st);
  // The prompt above the button is a target too, because that is where the eye
  // already is.  A player should never have to aim to begin.
  const Rect prompt{area.x, area.centerY() - dp(52.0f), area.w, dp(64.0f)};
  if (ui_.pointerUpIn(prompt)) launched = true;
  if (launched) {
    startRound(m, now);
    return;
  }

  Style bs;
  bs.variant = Variant::Ghost;
  bs.textDp = 12.0f;
  if (ui_.button(kIdBack + 1, {area.x, area.bottom() - dp(2.0f), area.w, dp(44.0f)}, "CANCEL", bs)) {
    playSfx(audio::Sfx::UiBack, 0.9f);
    goTo(Screen::ModeSelect);
  }
}

// ---------------------------------------------------------------------------
// PLAYING
// ---------------------------------------------------------------------------

void App::drawPlaying(Nanos now) {
  drawTargets(now);
  drawHud(now);

  // An abort control, because a player must never be trapped in a round.  It
  // sits in the top-left of the safe area and is resolved before the tap reaches
  // the mode, so it works no matter where the target is.  Quitting keeps the
  // round's stats: an abandoned run is a short run, not a fake one.
  Style st;
  st.variant = Variant::Muted;
  st.textDp = 10.0f;
  const Rect area = safeArea();
  if (ui_.button(kIdPlayingAbort, {area.x, area.y, dp(62.0f), dp(32.0f)}, "QUIT", st)) {
    abortRound_ = true;
  }
}

void App::drawTargets(Nanos now) {
  (void)now;
  const RoundView& v = mode_.view();
  for (int i = 0; i < v.activeTargets; ++i) {
    const TargetView& t = v.targets[i];
    // The spawn scale runs over about three frames.  Deliberately short: the
    // stimulus is already answerable, so the animation must not delay the thing
    // it is animating.
    const float grow = easeOutBack(clamp01(t.spawnProgress));
    const float radius = t.radius * grow;
    if (radius < 0.5f) continue;

    if (t.isTarget) {
      // A soft outer bloom, so a white target separates cleanly from a particle
      // burst and stays findable at the edge of vision.  A dark halo would be
      // pointless here: the field is already black.
      renderer_.glow(t.center, radius * 1.55f, withAlpha(theme::kHighlight, 0.16f * t.alpha), 2.2f);
      renderer_.disc(t.center, radius, withAlpha(theme::kBright, t.alpha));
    } else {
      // Decoys: a dim filled disc, always visually behind the real target.
      renderer_.disc(t.center, radius, withAlpha(theme::kSurfaceRaised, 0.95f * t.alpha));
      renderer_.ring(t.center, radius, dp(1.0f), withAlpha(theme::kHairline, t.alpha));
    }

    switch (t.mark) {
      case MarkKind::None:
        break;
      case MarkKind::Bullseye: {
        // The target's identity: a hole with a dot at the centre.  Punching the
        // hole with an opaque disc is cheaper than a second SDF kind and reads
        // identically on a black background.
        renderer_.disc(t.center, radius * 0.54f, theme::kVoid);
        renderer_.disc(t.center, radius * 0.20f, withAlpha(theme::kBright, t.alpha));
        break;
      }
      case MarkKind::Plus: {
        const float a = radius * 0.32f;
        const Color c = withAlpha(theme::kTextDim, t.alpha);
        renderer_.line({t.center.x - a, t.center.y}, {t.center.x + a, t.center.y}, dp(1.6f), c);
        renderer_.line({t.center.x, t.center.y - a}, {t.center.x, t.center.y + a}, dp(1.6f), c);
        break;
      }
      case MarkKind::Cross: {
        const float a = radius * 0.34f;
        const Color c = withAlpha(theme::kTextDim, t.alpha);
        renderer_.line({t.center.x - a, t.center.y - a}, {t.center.x + a, t.center.y + a}, dp(1.6f), c);
        renderer_.line({t.center.x - a, t.center.y + a}, {t.center.x + a, t.center.y - a}, dp(1.6f), c);
        break;
      }
      case MarkKind::Slash: {
        const float a = radius * 0.36f;
        renderer_.line({t.center.x - a, t.center.y - a}, {t.center.x + a, t.center.y + a}, dp(1.6f),
                       withAlpha(theme::kTextDim, t.alpha));
        break;
      }
      case MarkKind::Square: {
        const float a = radius * 0.30f;
        renderer_.rect(Rect::fromCenter(t.center, a * 2.0f, a * 2.0f),
                       withAlpha(theme::kTextFaint, t.alpha), 0.0f);
        break;
      }
      case MarkKind::Triangle: {
        renderer_.triangle(t.center, radius * 0.36f, 0.0f, withAlpha(theme::kTextFaint, t.alpha));
        break;
      }
      case MarkKind::Arc: {
        renderer_.ringArc(t.center, radius * 0.34f, dp(1.6f), -1.1f, 4.0f,
                          withAlpha(theme::kTextFaint, t.alpha));
        break;
      }
    }

    // Response-window ring: a thin arc that closes as the window runs out, so
    // the player can see how much time is left without reading anything.
    if (t.isTarget && v.showWindowRing && v.windowProgress < 0.999f) {
      // The ring closes as the window runs out.  It has to be clearly visible:
      // this is the only feedback that tells a player how long they have, and a
      // ring too faint to see is worse than no ring at all.
      const float remain = clamp01(1.0f - v.windowProgress);
      renderer_.ringArc(t.center, radius * 1.42f, dp(2.5f), -1.5707963f,
                        6.2831853f * remain,
                        withAlpha(theme::kHighlight, 0.45f + 0.45f * (1.0f - remain)));
    }
  }
}

void App::drawHud(Nanos now) {
  (void)now;  // The effect layer is drawn once, above the screen, from onDrawFrame.
  const Rect area = safeArea();
  const RoundSummary& s = mode_.session().summary();

  // Deliberately sparse: score on the left, the round's own best on the right,
  // nothing else.  Any extra furniture during a stimulus is a distraction the
  // player did not ask for.
  char score[16];
  std::snprintf(score, sizeof(score), "%d", s.score);
  ui_.textDisplay({area.centerX(), area.y + dp(11.0f)}, score, 15.0f, theme::kText, HAlign::Center,
                  VAlign::Middle, theme::kDisplayTracking);

  if (mode_.mode() == Mode::Max) {
    char right[32];
    std::snprintf(right, sizeof(right), "LEVEL %d", mode_.level());
    ui_.textDisplay({area.right(), area.y + dp(11.0f)}, right, 15.0f, theme::kText, HAlign::Right,
                    VAlign::Middle, theme::kDisplayTracking);
  } else {
    char right[32];
    if (s.bestUs > 0) {
      char best[12];
      formatMs(best, sizeof(best), s.bestUs);
      std::snprintf(right, sizeof(right), "BEST %s MS", best);
    } else {
      // Before the first answer there is no best yet, and "BEST  MS" with a hole
      // in it looks like a bug.
      std::snprintf(right, sizeof(right), "BEST --");
    }
    ui_.textMono({area.right(), area.y + dp(11.0f)}, right, cfg::kLabelSizeDp, theme::kTextFaint,
                 HAlign::Right, VAlign::Middle);
  }

  // Round progress, bottom-left, out of the way of the target field.
  {
    char prog[32];
    if (mode_.mode() == Mode::Max) {
      std::snprintf(prog, sizeof(prog), "%d HIT", s.hits);
    } else {
      std::snprintf(prog, sizeof(prog), "%d / %d", s.hits, s.trials);
    }
    ui_.textMono({area.x, area.bottom() - dp(10.0f)}, prog, cfg::kSmallSizeDp, theme::kTextGhost,
                 HAlign::Left, VAlign::Middle);
  }

  int targetTrials = 0;
  switch (mode_.mode()) {
    case Mode::Reflex: targetTrials = cfg::kReflexTrials; break;
    case Mode::Flick: targetTrials = cfg::kFlickTargetsPerRound; break;
    case Mode::Focus: targetTrials = cfg::kFocusTargetsPerRound; break;
    case Mode::Max: targetTrials = 0; break;
  }
  if (targetTrials > 0) {
    const float t = clamp01(static_cast<float>(s.hits) / static_cast<float>(targetTrials));
    ui_.progressBar({area.x, area.bottom(), area.w, dp(2.0f)}, t, theme::kHighlight,
                    theme::kSurface);
  } else {
    // MAX has no end, so the bar tracks the level instead.
    const float t = clamp01(static_cast<float>(mode_.level()) / 40.0f);
    ui_.progressBar({area.x, area.bottom(), area.w, dp(2.0f)}, t, theme::kSurfaceRaised,
                    theme::kSurface);
  }

  // The streak only appears once it is worth showing.
  if (s.streak >= 5) {
    char streak[24];
    std::snprintf(streak, sizeof(streak), "x%d", s.streak);
    ui_.textDisplay({area.centerX(), area.y + dp(11.0f)}, streak, 13.0f, theme::kTextDim,
                    HAlign::Center, VAlign::Middle, theme::kDisplayTracking);
  }

  fx_.draw(renderer_, now);
}

}  // namespace pp
