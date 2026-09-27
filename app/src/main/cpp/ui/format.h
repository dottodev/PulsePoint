// Number formatting for the statistics and result screens.
//
// These live in a header rather than in screens.cpp for one reason: every
// number a player ever reads passes through here, and a thousands separator in
// the wrong place is a bug that no compiler will catch and no screenshot of a
// menu will reveal.  Keeping them inline and testable is cheaper than finding
// one in a bug report.
#ifndef PP_UI_FORMAT_H
#define PP_UI_FORMAT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace pp {
namespace ui_helpers {

// Milliseconds, truncated, with no unit suffix.  Zero means "not measured yet"
// and produces an empty string so callers can decide between a dash and a unit.
inline void formatMs(char* buf, size_t cap, uint32_t us) {
  if (us == 0 || buf == nullptr || cap == 0) {
    if (buf != nullptr && cap > 0) buf[0] = '\0';
    return;
  }
  std::snprintf(buf, cap, "%u", us / 1000u);
}

// As formatMs, but an unmeasured value reads as "--" rather than nothing.
inline void formatMsOrDash(char* buf, size_t cap, uint32_t us) {
  if (us == 0) {
    std::snprintf(buf, cap, "--");
    return;
  }
  formatMs(buf, cap, us);
}

// A count with thousands separators: 27412 becomes "27,412".
inline void formatCount(char* buf, size_t cap, uint64_t v) {
  if (buf == nullptr || cap == 0) return;
  if (v == 0) {
    std::snprintf(buf, cap, "0");
    return;
  }
  char tmp[32];
  int n = 0;
  while (v > 0 && n < static_cast<int>(sizeof(tmp)) - 1) {
    tmp[n++] = static_cast<char>('0' + static_cast<int>(v % 10));
    v /= 10;
  }
  int out = 0;
  for (int i = n - 1; i >= 0; --i) {
    // i indexes the reversed digits, so the digit emitted here sits at
    // left-position n-1-i and has i+1 digits to its right: that is the count a
    // thousands separator is measured from.
    if (out > 0 && ((i + 1) % 3) == 0 && out < static_cast<int>(cap) - 2) buf[out++] = ',';
    if (out >= static_cast<int>(cap) - 1) break;
    buf[out++] = tmp[i];
  }
  buf[out] = '\0';
}

// A share of one, as a percentage with one decimal.  Zero is unknown, not 0%.
inline void formatPercent(char* buf, size_t cap, float v) {
  if (v <= 0.0f) {
    std::snprintf(buf, cap, "--");
    return;
  }
  std::snprintf(buf, cap, "%.1f%%", v * 100.0f);
}

// Played time, coarsened to the largest two units that say something: "1M 04S"
// for a minute, "3H 12M" for a session that ran past lunch, "45S" below that.
inline void formatDuration(char* buf, size_t cap, uint64_t ns) {
  constexpr uint64_t kNsPerSec = 1000000000ull;
  const uint64_t totalSec = ns / kNsPerSec;
  if (totalSec == 0) {
    std::snprintf(buf, cap, "--");
    return;
  }
  const uint64_t h = totalSec / 3600ull;
  const uint64_t m = (totalSec % 3600ull) / 60ull;
  const uint64_t s = totalSec % 60ull;
  if (h > 0) {
    std::snprintf(buf, cap, "%lluH %lluM", static_cast<unsigned long long>(h),
                  static_cast<unsigned long long>(m));
  } else if (m > 0) {
    std::snprintf(buf, cap, "%lluM %lluS", static_cast<unsigned long long>(m),
                  static_cast<unsigned long long>(s));
  } else {
    std::snprintf(buf, cap, "%lluS", static_cast<unsigned long long>(s));
  }
}

}  // namespace ui_helpers
}  // namespace pp

#endif  // PP_UI_FORMAT_H
