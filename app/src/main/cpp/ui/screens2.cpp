// Screen drawing, part two: the result screen, achievements, statistics,
// settings, the transition wipe and the achievement unlock overlay.
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

enum : int {
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
  kIdBack = 1100,
};
}  // namespace

// ---------------------------------------------------------------------------
// RESULT
// ---------------------------------------------------------------------------

void App::drawResult(Nanos now) {
  const Rect area = safeArea();
  const RoundSummary& s = lastSummary_;
  // The screen reveals itself in sequence from one shared clock, so a slow frame
  // never leaves half the rows stranded.
  const float t = transitioning_ ? clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                                          (transitionDur_ * 1000.0f))
                                 : 1.0f;
  const float revealClock = secFromNs(elapsedSince(now, resultStartNs_));

  ui_.textDisplay({area.centerX(), area.y + dp(20.0f)}, "RESULT", cfg::kHeadingSizeDp,
                  withAlpha(theme::kText, t), HAlign::Center, VAlign::Middle,
                  theme::kDisplayTrackingWide);
  ui_.textMono({area.centerX(), area.y + dp(40.0f)}, modeName(finishedMode_), cfg::kLabelSizeDp,
               withAlpha(theme::kTextFaint, t), HAlign::Center, VAlign::Middle);

  // The reaction time is by far the most noticeable element here, and that is
  // the whole point of the game.  A round abandoned before the first answer has
  // no reaction time at all, and showing an empty hero slot would be worse than
  // saying so.
  const float heroBase = dp(cfg::kHeroNumberSizeDp);
  const float heroGrow = easeOutQuint(clamp01(revealClock / 0.35f));
  const float heroY = area.y + area.h * 0.24f;
  char best[16];
  if (s.bestUs > 0) {
    formatMs(best, sizeof(best), s.bestUs);
  } else {
    std::snprintf(best, sizeof(best), "NO ANSWERS");
  }
  const float heroSize =
      (s.bestUs > 0 ? heroBase * (0.86f + 0.14f * heroGrow) : heroBase * 0.26f) * t;

  gfx::TextStyle hero;
  hero.face = &theme::display();
  hero.sizePx = heroSize;
  hero.tracking = theme::kDisplayTracking;
  hero.color = withAlpha(s.bestUs > 0 ? theme::kBright : theme::kTextFaint, t);
  hero.glowPx = s.bestUs > 0 ? dp(7.0f) * heroGrow : 0.0f;

  if (heroSize > dp(8.0f)) {
    renderer_.text(area.centerX(), heroY, best, hero, gfx::HAlign::Center, gfx::VAlign::Middle);
  }
  if (s.bestUs > 0) {
    // Placed from the hero's own measured ascent, so the unit never lands inside
    // the digits no matter how wide the number is.
    const gfx::TextMetrics heroMetrics = renderer_.measure(best, hero);
    ui_.textDisplay({area.centerX(), heroY + heroMetrics.ascent + dp(14.0f)}, "MS", 15.0f,
                    withAlpha(theme::kTextDim, t), HAlign::Center, VAlign::Middle,
                    theme::kDisplayTrackingWide);
  }

  if (lastRoundWasRecord_) {
    // The banner only exists when it is true, and it arrives hard.
    const float bt = easeOutBack(clamp01((revealClock - 0.28f) / 0.4f));
    if (bt > 0.001f) {
      const float w = dp(224.0f) * bt;
      const Rect band{area.centerX() - w * 0.5f, heroY + heroBase * 0.56f, w, dp(26.0f)};
      renderer_.rect(band, withAlpha(theme::kBright, t), dp(2.0f));
      if (bt > 0.6f) {
        gfx::TextStyle pb;
        pb.face = &theme::display();
        pb.sizePx = dp(12.0f);
        pb.tracking = theme::kDisplayTrackingWide;
        pb.color = withAlpha(theme::kVoid, clamp01((bt - 0.6f) / 0.3f));
        renderer_.text(band.centerX(), band.centerY(), "NEW PERSONAL BEST", pb,
                       gfx::HAlign::Center, gfx::VAlign::Middle);
      }
    }
  }

  // The stat grid, two columns, revealed a row at a time.
  const float gridTop = area.y + area.h * 0.44f;
  const float rowH = dp(24.0f);
  const float colW = area.w * 0.5f;
  char buf[32];
  char rows[8][2][26];

  std::snprintf(rows[0][0], 26, "AVERAGE");
  if (s.samples > 0) {
    std::snprintf(buf, sizeof(buf), "%u", static_cast<uint32_t>(s.averageMs()));
    std::snprintf(rows[0][1], 26, "%s MS", buf);
  } else {
    std::snprintf(rows[0][1], 26, "--");
  }
  std::snprintf(rows[1][0], 26, "SLOWEST");
  if (s.samples > 0) {
    std::snprintf(buf, sizeof(buf), "%u", s.worstUs / 1000u);
    std::snprintf(rows[1][1], 26, "%s MS", buf);
  } else {
    std::snprintf(rows[1][1], 26, "--");
  }
  std::snprintf(rows[2][0], 26, "ACCURACY");
  formatPercent(buf, sizeof(buf), s.accuracy);
  std::snprintf(rows[2][1], 26, "%s", buf);
  std::snprintf(rows[3][0], 26, "STREAK");
  std::snprintf(rows[3][1], 26, "%d", s.streak);
  std::snprintf(rows[4][0], 26, "SUCCESSFUL");
  std::snprintf(rows[4][1], 26, "%d", s.hits);
  std::snprintf(rows[5][0], 26, "TOTAL PRESSES");
  std::snprintf(rows[5][1], 26, "%d", s.totalPresses);
  std::snprintf(rows[6][0], 26, "SPEED BONUS");
  std::snprintf(rows[6][1], 26, "%d", s.speedBonusPoints);
  std::snprintf(rows[7][0], 26, "PENALTIES");
  // A clean zero, not a negative one: "-%d" of nothing reads as a bug.
  if (s.penaltyPoints > 0) {
    std::snprintf(rows[7][1], 26, "-%d", s.penaltyPoints);
  } else {
    std::snprintf(rows[7][1], 26, "0");
  }

  for (int i = 0; i < 8; ++i) {
    const float appear = easeOutCubic(clamp01((revealClock - 0.07f * static_cast<float>(i)) / 0.4f));
    if (appear <= 0.001f) continue;
    const int col = i / 4;
    const int line = i % 4;
    const Rect r{area.x + static_cast<float>(col) * colW,
                 gridTop + static_cast<float>(line) * rowH + (1.0f - appear) * dp(8.0f),
                 colW - dp(12.0f), rowH};
    ui_.statRow(r, rows[i][0], rows[i][1], theme::kText, 13.0f);
  }

  // Score, counted up from zero.
  const int shownScore = static_cast<int>(displayedScore_ + 0.5f);
  char scoreBuf[16];
  std::snprintf(scoreBuf, sizeof(scoreBuf), "%d", shownScore);
  // Clear of the fourth stat row: the score is set at 32 dp, so a small offset
  // would put its cap height on top of the row above.
  const float scoreY = gridTop + rowH * 4.0f + dp(26.0f);
  ui_.textDisplay({area.x, scoreY}, "SCORE", 12.0f, theme::kTextDim, HAlign::Left, VAlign::Middle,
                  theme::kDisplayTrackingWide);
  gfx::TextStyle scoreStyle;
  scoreStyle.face = &theme::display();
  scoreStyle.sizePx = dp(32.0f);
  scoreStyle.tracking = theme::kDisplayTracking;
  scoreStyle.color = theme::kBright;
  scoreStyle.glowPx = dp(3.0f);
  renderer_.text(area.right(), scoreY, scoreBuf, scoreStyle, gfx::HAlign::Right,
                 gfx::VAlign::Middle);

  if (finishedMode_ == Mode::Max) {
    char surv[24];
    formatDuration(surv, sizeof(surv), static_cast<uint64_t>(s.survivalNs));
    char extra[56];
    std::snprintf(extra, sizeof(extra), "SURVIVED %s AT LEVEL %d", surv, s.level);
    ui_.textMono({area.centerX(), scoreY + dp(22.0f)}, extra, cfg::kSmallSizeDp,
                 theme::kTextFaint, HAlign::Center, VAlign::Middle);
  } else {
    char dur[24];
    formatDuration(dur, sizeof(dur), static_cast<uint64_t>(s.durationNs));
    ui_.textMono({area.centerX(), scoreY + dp(22.0f)}, dur, cfg::kSmallSizeDp, theme::kTextFaint,
                 HAlign::Center, VAlign::Middle);
  }

  // Buttons.
  const float btnH = dp(cfg::kButtonHeightDp);
  const float btnTop = area.bottom() - btnH * 2.0f - dp(14.0f);
  Style again;
  again.variant = Variant::Primary;
  again.textDp = 15.0f;
  if (ui_.button(kIdResultAgain, {area.x, btnTop, area.w, btnH}, "PLAY AGAIN", again)) {
    startRound(finishedMode_, now);
    return;
  }
  const float halfW = (area.w - dp(10.0f)) * 0.5f;
  Style second;
  second.variant = Variant::Secondary;
  second.textDp = 12.0f;
  if (ui_.button(kIdResultChange, {area.x, btnTop + btnH + dp(10.0f), halfW, btnH}, "CHANGE MODE",
                 second)) {
    playSfx(audio::Sfx::UiBack, 0.9f);
    goTo(Screen::ModeSelect);
    return;
  }
  if (ui_.button(kIdResultMenu, {area.x + halfW + dp(10.0f), btnTop + btnH + dp(10.0f), halfW, btnH},
                 "MAIN MENU", second)) {
    playSfx(audio::Sfx::UiBack, 0.9f);
    goTo(Screen::MainMenu);
  }
}

// ---------------------------------------------------------------------------
// ACHIEVEMENTS
// ---------------------------------------------------------------------------

void App::drawAchievements(Nanos now) {
  const Rect area = safeArea();
  char header[48];
  std::snprintf(header, sizeof(header), "UNLOCKED: %d / %d", achievements_.unlockedCount,
                kAchievementCount);
  const float contentTop = drawSubScreenHeader(now, "ACHIEVEMENTS", kIdBack, header);

  const float rowH = dp(64.0f);
  const float gap = dp(6.0f);
  const float top = contentTop;
  const float viewportH = std::max(dp(80.0f), area.bottom() - top - dp(18.0f));
  const float contentH = (rowH + gap) * static_cast<float>(kAchievementCount);

  ui_.beginScroll(kIdAchScroll, {area.x, top, area.w, viewportH}, contentH);
  const float scroll = ui_.scrollOffset();

  for (int i = 0; i < kAchievementCount; ++i) {
    const AchievementDef& d = achievements_.defs[i];
    const float y = top - scroll + static_cast<float>(i) * (rowH + gap);
    // Only rows whose whole height is inside the viewport are drawn.  A row that
    // starts just above the fold is fine -- that is what makes the list feel
    // continuous -- but a row whose *bottom* pokes past it would put its progress
    // line on top of the footer.
    if (y + rowH <= top || y + rowH > top + viewportH) continue;

    const bool unlocked = d.achieved;
    const Rect row{area.x, y, area.w, rowH};
    ui_.panel(row, theme::kCardRadiusDp, unlocked ? theme::kSurface : theme::kVoid, dp(1.0f),
              unlocked ? theme::kHairline : theme::kSurface);

    // A press anywhere on the row lights its left edge, so a long list can be
    // navigated without a separate selected state.
    const float sel = ui_.pressOf(kIdAchScroll * 4 + i);
    if (sel > 0.01f) {
      renderer_.rect({row.x, row.y + dp(9.0f), dp(2.0f), row.h - dp(18.0f)},
                     withAlpha(theme::kBright, sel), dp(1.0f));
    }

    const Vec2 iconCenter{row.x + dp(29.0f), row.centerY()};
    const float iconSize = dp(30.0f);
    if (unlocked) {
      const float breathe = 1.0f + 0.03f * std::sin(bgTime_ * 1.6f + static_cast<float>(i));
      drawIcon(renderer_, d.icon, iconCenter, iconSize * breathe, 1.0f);
    } else {
      // The same icon as a silhouette: the shape is hinted at, the meaning is not.
      drawIcon(renderer_, d.icon, iconCenter, iconSize, 0.17f);
    }

    const float textX = iconCenter.x + dp(24.0f);
    const float textW = std::max(dp(20.0f), row.right() - textX - dp(12.0f));

    if (unlocked) {
      ui_.textDisplay({textX, row.y + dp(21.0f)}, d.name, 13.0f, theme::kText, HAlign::Left,
                      VAlign::Middle, theme::kDisplayTracking);
      ui_.textMono({textX, row.y + dp(38.0f)}, d.description, cfg::kSmallSizeDp, theme::kTextDim,
                   HAlign::Left, VAlign::Middle);
      if (d.progress != ProgressStyle::None && d.target > 0.0 && !d.secret) {
        ui_.progressBar({textX, row.y + dp(50.0f), textW, dp(2.0f)},
                        clamp01(static_cast<float>(d.current / d.target)),
                        theme::kHairlineStrong, theme::kSurface);
      }
    } else if (d.secret) {
      // Nothing.  No question mark, no counter: the entry is genuinely blank
      // until it fires, and the row keeps the same height as its neighbours so
      // the grid does not reveal the secrets by spacing.
      ui_.textDisplay({textX, row.y + dp(30.0f)}, "???", 13.0f, theme::kTextGhost, HAlign::Left,
                      VAlign::Middle, theme::kDisplayTrackingWide);
    } else {
      ui_.textDisplay({textX, row.y + dp(21.0f)}, d.name, 13.0f, theme::kTextFaint, HAlign::Left,
                      VAlign::Middle, theme::kDisplayTracking);
      ui_.textMono({textX, row.y + dp(38.0f)}, d.description, cfg::kSmallSizeDp, theme::kTextGhost,
                   HAlign::Left, VAlign::Middle);
      if (d.progress != ProgressStyle::None && d.target > 0.0) {
        const char* unit = d.progressLabel != nullptr ? d.progressLabel : "";
        ui_.progressLine({textX, row.y + dp(51.0f), textW, dp(14.0f)}, "PROGRESS", d.current,
                         d.target, unit);
      }
    }
  }
  ui_.endScroll();

  ui_.textMono({area.centerX(), area.bottom() - dp(4.0f)}, "DRAG TO SCROLL", cfg::kSmallSizeDp,
               theme::kTextGhost, HAlign::Center, VAlign::Middle);
}

// ---------------------------------------------------------------------------
// STATISTICS
// ---------------------------------------------------------------------------

void App::drawStatistics(Nanos now) {
  const Rect area = safeArea();
  char subtitle[48];
  char games[24];
  formatCount(games, sizeof(games), profile_.stats.totalGames);
  std::snprintf(subtitle, sizeof(subtitle), "%s %s", games,
                games[1] == '\0' || games[2] == '\0' ? "GAME" : "GAMES");
  const float top = drawSubScreenHeader(now, "STATISTICS", kIdBack, subtitle);
  const float viewportH = std::max(dp(80.0f), area.bottom() - top - dp(6.0f));
  const float contentH = dp(1180.0f);

  ui_.beginScroll(kIdStatScroll, {area.x, top, area.w, viewportH}, contentH);
  const float scroll = ui_.scrollOffset();

  const float x = area.x;
  const float w = area.w;
  const float rowH = dp(24.0f);
  float y = top - scroll + dp(4.0f);
  const Stats& st = profile_.stats;
  char buf[40];

  // Rows are laid out unconditionally and drawn only when they are on screen:
  // the y cursor has to keep advancing or the sections would collide.
  auto visible = [&](float lineTop, float lineHeight) {
    return lineTop + lineHeight > top && lineTop < top + viewportH;
  };
  auto header = [&](const char* label) {
    if (visible(y, dp(24.0f))) {
      ui_.textDisplay({x, y}, label, 11.0f, theme::kTextDim, HAlign::Left, VAlign::Middle,
                      theme::kDisplayTrackingWide);
      ui_.divider(x, x + w, y + dp(15.0f));
    }
    y += dp(24.0f);
  };
  auto row = [&](const char* label, const char* value, Color c = theme::kText, float sizeDp = 0.0f) {
    if (visible(y, rowH)) ui_.statRow({x, y, w, rowH}, label, value, c, sizeDp);
    y += rowH;
  };
  auto rowCount = [&](const char* label, uint64_t v, Color c = theme::kText, float sizeDp = 0.0f) {
    formatCount(buf, sizeof(buf), v);
    row(label, buf, c, sizeDp);
  };

  header("LIFETIME");
  rowCount("TOTAL PRESSES", st.totalPresses, theme::kBright, 15.0f);
  rowCount("TOTAL HITS", st.totalHits, theme::kText, 15.0f);
  rowCount("TOTAL MISSES", st.totalMisses);
  rowCount("FALSE STARTS", st.totalFalseStarts);
  rowCount("TOTAL GAMES", st.totalGames);
  formatDuration(buf, sizeof(buf), st.totalTimePlayedNs);
  row("TOTAL TIME PLAYED", buf);
  y += dp(10.0f);

  header("RECORDS");
  formatMsOrDash(buf, sizeof(buf), st.bestReactionUs);
  {
    char withUnit[32];
    if (st.bestReactionUs > 0) {
      std::snprintf(withUnit, sizeof(withUnit), "%s MS", buf);
    } else {
      std::snprintf(withUnit, sizeof(withUnit), "--");
    }
    row("BEST REACTION", withUnit, theme::kBright, 15.0f);
  }
  if (st.reactionSamples > 0) {
    char withUnit[32];
    std::snprintf(withUnit, sizeof(withUnit), "%u MS", st.averageReactionMsU32());
    row("AVERAGE REACTION", withUnit, theme::kText, 15.0f);
  } else {
    row("AVERAGE REACTION", "--");
  }
  formatPercent(buf, sizeof(buf), st.bestAccuracy);
  row("BEST ACCURACY", buf, theme::kText, 15.0f);
  std::snprintf(buf, sizeof(buf), "%u", st.bestStreak);
  row("BEST STREAK", buf, theme::kText, 15.0f);
  rowCount("TOTAL SCORE", st.totalScore);
  y += dp(10.0f);

  header("BEST SCORE BY MODE");
  {
    float values[kModeCount];
    float maxV = 1.0f;
    for (int i = 0; i < kModeCount; ++i) {
      values[i] = static_cast<float>(st.modes[i].bestScore);
      if (values[i] > maxV) maxV = values[i];
    }
    const float barTop = y + dp(14.0f);
    ui_.barChart({x, barTop, w, dp(46.0f)}, values, kModeCount, maxV, theme::kHighlight);
    const float colW = w / static_cast<float>(kModeCount);
    char value[16];
    for (int i = 0; i < kModeCount; ++i) {
      const float cx = x + colW * (static_cast<float>(i) + 0.5f);
      // The value rides above its own bar, so a short bar still gets a number
      // and a tall one never has one hidden underneath it.
      const float v = clamp01(values[i] / maxV);
      const float barH = std::max(dp(1.5f), dp(46.0f) * v);
      std::snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(st.modes[i].bestScore));
      ui_.textMono({cx, barTop + dp(46.0f) - barH - dp(8.0f)}, value, cfg::kSmallSizeDp,
                   values[i] > 0.0f ? theme::kTextDim : theme::kTextGhost, HAlign::Center,
                   VAlign::Middle);
      ui_.textDisplay({cx, barTop + dp(60.0f)}, modeName(static_cast<Mode>(i)), 9.0f,
                      theme::kTextFaint, HAlign::Center, VAlign::Middle,
                      theme::kDisplayTracking);
    }
    y += dp(90.0f);
  }

  for (int i = 0; i < kModeCount; ++i) {
    const Mode m = static_cast<Mode>(i);
    const ModeStats& ms = st.modes[i];
    header(modeName(m));
    rowCount("BEST SCORE", ms.bestScore);
    {
      char withUnit[32];
      formatMsOrDash(buf, sizeof(buf), ms.bestReactionUs);
      if (ms.bestReactionUs > 0) {
        std::snprintf(withUnit, sizeof(withUnit), "%s MS", buf);
      } else {
        std::snprintf(withUnit, sizeof(withUnit), "--");
      }
      row("BEST REACTION", withUnit);
    }
    if (ms.reactionSamples > 0) {
      char withUnit[32];
      std::snprintf(withUnit, sizeof(withUnit), "%u MS",
                    static_cast<uint32_t>(ms.reactionUsSum / ms.reactionSamples / 1000ull));
      row("AVERAGE REACTION", withUnit);
    } else {
      row("AVERAGE REACTION", "--");
    }
    formatPercent(buf, sizeof(buf), ms.bestAccuracy);
    row("BEST ACCURACY", buf);
    std::snprintf(buf, sizeof(buf), "%u", ms.bestStreak);
    row("BEST STREAK", buf);
    if (m == Mode::Max) {
      std::snprintf(buf, sizeof(buf), "%u", ms.bestLevel);
      row("BEST LEVEL", buf);
      formatDuration(buf, sizeof(buf), ms.bestSurvivalNs);
      row("LONGEST RUN", buf);
    }
    rowCount("GAMES PLAYED", ms.games);
    formatDuration(buf, sizeof(buf), ms.totalTimeNs);
    row("TIME PLAYED", buf);
    rowCount("TOTAL HITS", ms.hits);
    y += dp(6.0f);
  }
  ui_.endScroll();
}

// ---------------------------------------------------------------------------
// SETTINGS
// ---------------------------------------------------------------------------

void App::drawSettings(Nanos now) {
  const Rect area = safeArea();
  const float top =
      drawSubScreenHeader(now, "SETTINGS", kIdBack, "AUDIO, FEEDBACK AND TIMING") + dp(8.0f);
  const float rowH = dp(56.0f);
  const float gap = dp(8.0f);
  const float w = area.w;
  float y = top;

  // Every row is one full-width tap target with its control on the right, so the
  // label, the caption and the switch all toggle the same thing.
  auto toggleRow = [&](int id, const char* label, const char* caption, bool* value, bool first) {
    const Rect r{area.x, y, w, rowH};
    if (!isCulledForSettings(r)) {
      if (!first) ui_.divider(r.x + dp(4.0f), r.right() - dp(4.0f), r.y);
      if (ui_.tapArea(id, r)) {
        *value = !*value;
        applySettingChange();
      }
      ui_.settingsRow(r, label, caption);
      ui_.togglePill(r, *value);
    }
    y += rowH + gap;
  };

  toggleRow(kIdSetSound, "SOUND", "TAPS, MISSES, UNLOCKS", &settings_->sound, true);
  toggleRow(kIdSetMusic, "MUSIC", "AMBIENT BED", &settings_->music, false);
  toggleRow(kIdSetHaptics, "HAPTICS", "VIBRATION ON IMPACT", &settings_->haptics, false);
  toggleRow(kIdSetFps, "SHOW FPS", "FRAME COUNTER AND TIMING DEBUG", &settings_->showFps, false);

  // Latency calibration.  AUTO tracks half the frame interval; the manual steps
  // exist for players on a high-latency panel or an external display.  The
  // measured value rides inside the stepper, not in the caption, so a long
  // caption can never collide with the buttons.
  {
    const Rect r{area.x, y, w, rowH};
    if (!isCulledForSettings(r)) {
      ui_.divider(r.x + dp(4.0f), r.right() - dp(4.0f), r.y);
      const Rect row{r.x, r.y, r.w, rowH};
      int v = settings_->calibrationMs;
      if (ui_.stepperControl(kIdSetCalib, row, &v, Settings::kCalibrationAuto, 30)) {
        settings_->calibrationMs = v;
        applySettingChange();
      }
      char valueText[16];
      if (v == Settings::kCalibrationAuto) {
        std::snprintf(valueText, sizeof(valueText), "AUTO");
      } else {
        std::snprintf(valueText, sizeof(valueText), "%d MS", v);
      }
      ui_.settingsRow(row, "LATENCY CALIBRATION",
                      "SUBTRACTED FROM EVERY TAP");
      ui_.stepperValue(row, valueText);
    }
    y += rowH + gap;
  }

  // A short colophon rather than empty space: what the numbers on the other
  // screens mean, and where they are kept.
  {
    const Rect r{area.x, y, w, dp(86.0f)};
    if (!isCulledForSettings(r)) {
      ui_.panel(r, theme::kCardRadiusDp, theme::kSurface);
      ui_.textDisplay({r.x + dp(14.0f), r.y + dp(26.0f)}, "PULSEPOINT  1.0", 11.0f,
                      theme::kTextDim, HAlign::Left, VAlign::Middle,
                      theme::kDisplayTracking);
      ui_.divider(r.x + dp(14.0f), r.right() - dp(14.0f), r.y + dp(42.0f));
      ui_.textMono({r.x + dp(14.0f), r.y + dp(58.0f)},
                   "EVERY SESSION, BEST REACTION AND UNLOCK", cfg::kSmallSizeDp,
                   theme::kTextFaint, HAlign::Left, VAlign::Middle);
      ui_.textMono({r.x + dp(14.0f), r.y + dp(73.0f)},
                   "IS STORED ON THIS DEVICE AND NEVER LEAVES IT", cfg::kSmallSizeDp,
                   theme::kTextFaint, HAlign::Left, VAlign::Middle);
    }
    y += r.h + gap;
  }

  // Reset, deliberately the least prominent control on the screen, pinned above
  // the footer.  Confirmation replaces it in place instead of stacking another
  // button on top, which is what made the two of them overlap.
  {
    const Rect r{area.x, area.bottom() - dp(72.0f), w, dp(50.0f)};
    Style st;
    st.variant = resetRequested_ ? Variant::Secondary : Variant::Muted;
    st.textDp = 11.0f;
    const char* label = resetRequested_ ? "ERASE EVERYTHING" : "RESET ALL PROGRESS";
    if (ui_.button(kIdSetCalib + 50, r, label, st)) {
      if (resetRequested_) {
        performReset();
        resetRequested_ = false;
      } else {
        resetRequested_ = true;
      }
    } else if (resetRequested_ && !ui_.pointerIn(r)) {
      resetRequested_ = false;
    }
    if (resetRequested_) {
      ui_.textMono({area.centerX(), r.y - dp(11.0f)}, "TAP AGAIN TO CONFIRM", cfg::kSmallSizeDp,
                   theme::kWarn, HAlign::Center, VAlign::Middle);
    }
    ui_.textMono({area.centerX(), area.bottom() - dp(6.0f)}, "ALL DATA STAYS ON THIS DEVICE",
                 cfg::kSmallSizeDp, theme::kTextGhost, HAlign::Center, VAlign::Middle);
  }
}

bool App::isCulledForSettings(const Rect& r) const {
  return r.bottom() < safeArea().y || r.top() > safeArea().bottom();
}

void App::applySettingChange() {
  settings_->clampToValid();
  synth_.setSoundEnabled(settings_->sound);
  synth_.setMusicEnabled(settings_->music);
  markDirty();
}

void App::performReset() {
  const uint32_t oldMask = profile_.achievementMask;
  profile_ = Profile{};
  profile_.achievementMask = oldMask;  // Keep unlock bits; the counters start over.
  profile_.achievementMask = 0;
  seedAchievementState(achievements_, profile_.stats, 0);
  settings_ = &profile_.settings;
  synth_.setSoundEnabled(settings_->sound);
  synth_.setMusicEnabled(settings_->music);
  fx_.clear();
  markDirty();
  if (filesDir_ != nullptr) {
    saveProfileToFile(filesDir_, profile_);
    profileDirty_ = false;
  }
  lastSaveNs_ = monotonicNow();
}

// ---------------------------------------------------------------------------
// TRANSITION
// ---------------------------------------------------------------------------

void App::drawTransition(Nanos now) {
  if (!transitioning_) return;
  const float t = clamp01(msFromNs(elapsedSince(now, transitionStart_)) /
                          std::max(1.0f, transitionDur_ * 1000.0f));
  const float w = surfaceWidth_;
  const float h = surfaceHeight_;

  // A bright band sweeping across, plus a short dim of the whole frame.  Fast,
  // geometric, and it never hides what has just arrived for more than 160 ms.
  const float sweep = easeInOutCubic(t);
  const float bandW = w * 0.42f;
  const float x = -bandW + (w + bandW * 2.0f) * sweep;
  const Color c = withAlpha(theme::kBright, 0.10f * (1.0f - t));
  renderer_.rect({x, 0.0f, bandW, h}, c);
  renderer_.rect({x, 0.0f, dp(1.5f), h}, withAlpha(theme::kBright, 0.55f * (1.0f - t)));
  renderer_.rect({x + bandW, 0.0f, dp(1.5f), h}, withAlpha(theme::kBright, 0.55f * (1.0f - t)));
}

// ---------------------------------------------------------------------------
// ACHIEVEMENT OVERLAY
// ---------------------------------------------------------------------------

void App::drawAchievementOverlay(Nanos now) {
  if (!overlayActive_ || overlayIndex_ < 0 || overlayIndex_ >= kAchievementCount) return;
  const AchievementDef& d = achievements_.defs[overlayIndex_];
  const float age = secFromNs(elapsedSince(now, overlayStartNs_));
  const float w = surfaceWidth_;
  const float h = surfaceHeight_;

  // A full-frame blackout that snaps in and eases out, so the moment is
  // unmistakable without hiding the screen for long.
  const float inT = clamp01(age / 0.10f);
  const float outT = clamp01((cfg::kAchievementShowSeconds - age) / 0.22f);
  const float veil = std::min(inT, outT);
  renderer_.rect({0.0f, 0.0f, w, h}, withAlpha(theme::kVoid, 0.88f * veil));

  // Everything inside scales in from 0.86 with a slight overshoot.
  const float pop = easeOutBack(clamp01(age / 0.30f));
  const float scale = 0.86f + 0.14f * pop;
  const float fade = clamp01(age / 0.14f) * clamp01((cfg::kAchievementShowSeconds - age) / 0.20f);

  const float cx = w * 0.5f;
  const float cy = h * 0.5f;

  // A frame that draws itself, then the mark, then the words.
  const float frameW = dp(276.0f) * scale;
  const float frameH = dp(190.0f) * scale;
  const Rect frame{cx - frameW * 0.5f, cy - frameH * 0.5f - dp(14.0f), frameW, frameH};
  // A solid backing behind the whole composition.  The veil above only dims
  // the shape pass, and text is uploaded in a later draw call, so without this
  // the screen's own labels would shine through at full brightness and land on
  // top of these words.  Black on black is invisible; what the panel does is
  // hide the screen behind the words, which is the entire job of a modal.  It
  // runs past the frame to cover the description block too, whose height varies
  // with the wrap: two lines plus a secret requirement is the tallest it gets.
  const float backBottom = cy + frameH * 0.5f + dp(96.0f);
  renderer_.rect({frame.x, frame.y, frame.w, backBottom - frame.y},
                 withAlpha(theme::kVoid, 0.94f * fade), dp(2.0f));
  const float borderT = clamp01(age / 0.22f);
  const Rect borderProgress{frame.x, frame.y, frame.w * easeOutQuint(borderT), frame.h};
  renderer_.rect(borderProgress, withAlpha(theme::kHairlineStrong, fade), dp(2.0f), dp(1.0f));

  // Screen pulse: two rings expanding on different periods, plus a burst of
  // square fragments thrown outward from the icon.  Drawn here rather than
  // through the particle system so the effect belongs to this moment and cannot
  // outlive it.
  const Vec2 iconPos{cx, frame.y + frame.h * 0.36f};
  for (int k = 0; k < 2; ++k) {
    const float ringT = clamp01((age - 0.04f * static_cast<float>(k)) / 0.62f);
    if (ringT <= 0.0f || ringT >= 1.0f) continue;
    renderer_.ring(iconPos, dp(24.0f) + dp(72.0f) * ringT, dp(1.5f),
                   withAlpha(theme::kHighlight, 0.55f * (1.0f - ringT) * (1.0f - 0.4f * k)));
  }
  {
    const float burstT = clamp01((age - 0.06f) / 0.60f);
    if (burstT > 0.0f && burstT < 1.0f) {
      const float burstFade = 1.0f - burstT;
      for (int i = 0; i < 18; ++i) {
        // A fixed, even spread rather than a random one, so the burst is
        // identical every time and reads as deliberate.
        const float a = 6.2831853f * (static_cast<float>(i) + 0.5f) / 18.0f;
        const float dist = dp(18.0f) + dp(120.0f) * easeOutCubic(burstT);
        const float size = dp(3.2f) * burstFade;
        if (size < 0.5f) continue;
        renderer_.rect(Rect::fromCenter({iconPos.x + std::cos(a) * dist,
                                         iconPos.y + std::sin(a) * dist},
                                        size, size),
                       withAlpha(theme::kBright, 0.75f * burstFade), 0.0f);
      }
    }
  }

  drawIcon(renderer_, d.icon, iconPos, dp(48.0f) * pop, fade);

  const float textAlpha = clamp01((age - 0.12f) / 0.18f) * fade;
  ui_.textDisplay({cx, frame.bottom() - dp(46.0f)}, "ACHIEVEMENT UNLOCKED", 10.0f,
                  withAlpha(theme::kTextFaint, textAlpha), HAlign::Center, VAlign::Middle,
                  theme::kDisplayTrackingWide);
  ui_.textDisplay({cx, frame.bottom() - dp(24.0f)}, d.name, 20.0f,
                  withAlpha(theme::kBright, textAlpha), HAlign::Center, VAlign::Middle,
                  theme::kDisplayTrackingWide);
  // The description is prose of unpredictable length -- the longest of the
  // thirty is 61 characters -- so it wraps, and everything below it stacks
  // from its measured height instead of from guessed offsets that a second
  // line would land on top of.
  float below = cy + frameH * 0.5f + dp(14.0f);
  below += ui_.textParagraph({cx, below}, d.description, cfg::kLabelSizeDp,
                             withAlpha(theme::kTextDim, textAlpha), w - dp(48.0f),
                             cfg::kLabelSizeDp * 1.5f);

  // A secret's requirement is shown exactly once, here, at the moment it fires.
  if (d.secret && d.secretRequirement != nullptr) {
    ui_.textMono({cx, below + dp(9.0f)}, d.secretRequirement, cfg::kSmallSizeDp,
                 withAlpha(theme::kHighlight, textAlpha), HAlign::Center, VAlign::Middle);
    below += dp(18.0f);
  }
  ui_.textMono({cx, below + dp(11.0f)}, "A GAMEPLAY RECORD, NOT A BIOLOGICAL CLAIM",
               cfg::kSmallSizeDp, withAlpha(theme::kTextGhost, textAlpha * 0.9f),
               HAlign::Center, VAlign::Middle);

  // Progress through the stay, as a hairline under the frame.
  if (queueCount_ > 0) {
    const float p = clamp01(age / cfg::kAchievementShowSeconds);
    renderer_.rect({frame.x, frame.bottom() + dp(8.0f), frame.w * p, dp(1.5f)},
                   withAlpha(theme::kHighlight, 0.5f * fade));
  }
}

// ---------------------------------------------------------------------------
// DEBUG
// ---------------------------------------------------------------------------

void App::drawDebug(Nanos now) {
  (void)now;
  if (settings_ == nullptr || !settings_->showFps) return;
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%.0f FPS  %.1f MS  CAL %d MS  TRI %llu  DROP %u", fps_,
                averageFrameMs(), lastCalibrationMs_,
                static_cast<unsigned long long>(renderer_.shapeVertexCount() +
                                                renderer_.textVertexCount()),
                droppedInputSamples());
  ui_.textMono({safeArea().x, surfaceHeight_ - dp(6.0f)}, buf, cfg::kSmallSizeDp,
               theme::kTextFaint, HAlign::Left, VAlign::Bottom);
}

}  // namespace pp
