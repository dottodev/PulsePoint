#include "profile.h"

#include <cstddef>
#include <cstring>

namespace pp {
namespace {

// The CRC covers every byte before the checksum itself.
constexpr size_t kCrcCovered = offsetof(ProfileBlob, crc);

// Bitwise CRC-32 (IEEE 802.3), reflected.  Built once on first use; the
// initialiser form of a function-local static is thread-safe in C++11 and
// costs nothing after the first call.
const uint32_t* crcTable() {
  static const uint32_t* table = [] {
    static uint32_t t[256];
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
    return t;
  }();
  return table;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t size, uint32_t seed) {
  const uint32_t* table = crcTable();
  uint32_t c = seed ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

size_t serializeProfile(const Profile& p, uint8_t* out, size_t capacity) {
  if (out == nullptr || capacity < sizeof(ProfileBlob)) return 0;

  ProfileBlob b;
  std::memset(&b, 0, sizeof(b));
  b.magic = kProfileMagic;
  b.version = kProfileVersion;

  const Stats& s = p.stats;
  b.totalPresses = s.totalPresses;
  b.totalHits = s.totalHits;
  b.totalMisses = s.totalMisses;
  b.totalFalseStarts = s.totalFalseStarts;
  b.totalGames = s.totalGames;
  b.totalTimePlayedNs = s.totalTimePlayedNs;
  b.bestReactionUs = s.bestReactionUs;
  b.reactionUsSum = s.reactionUsSum;
  b.reactionSamples = s.reactionSamples;
  b.bestAccuracy = s.bestAccuracy;
  b.bestStreak = s.bestStreak;
  b.totalScore = s.totalScore;

  for (int i = 0; i < kModeCount; ++i) {
    const ModeStats& m = s.modes[i];
    b.modeGames[i] = m.games;
    b.modeHits[i] = m.hits;
    b.modePresses[i] = m.presses;
    b.modeMisses[i] = m.misses;
    b.modeBestScore[i] = m.bestScore;
    b.modeTotalScore[i] = m.totalScore;
    b.modeBestReactionUs[i] = m.bestReactionUs;
    b.modeReactionUsSum[i] = m.reactionUsSum;
    b.modeReactionSamples[i] = m.reactionSamples;
    b.modeBestAccuracy[i] = m.bestAccuracy;
    b.modeBestStreak[i] = m.bestStreak;
    b.modeBestLevel[i] = m.bestLevel;
    b.modeBestSurvivalNs[i] = m.bestSurvivalNs;
    b.modeTotalTimeNs[i] = m.totalTimeNs;
    b.modeLastPlayedMs[i] = m.lastPlayedUnixMs;
  }

  b.achievementMask = p.achievementMask;

  b.sound = p.settings.sound ? 1u : 0u;
  b.music = p.settings.music ? 1u : 0u;
  b.haptics = p.settings.haptics ? 1u : 0u;
  b.showFps = p.settings.showFps ? 1u : 0u;
  b.calibrationMs = p.settings.calibrationMs;
  b.hapticsStrength = p.settings.hapticsStrength;

  b.crc = crc32(reinterpret_cast<const uint8_t*>(&b), kCrcCovered);

  std::memcpy(out, &b, sizeof(b));
  return sizeof(b);
}

bool deserializeProfile(const uint8_t* in, size_t size, Profile* out) {
  if (in == nullptr || out == nullptr) return false;
  if (size < sizeof(ProfileBlob)) return false;

  ProfileBlob b;
  std::memcpy(&b, in, sizeof(b));
  if (b.magic != kProfileMagic) return false;
  if (b.version != kProfileVersion) return false;
  if (crc32(reinterpret_cast<const uint8_t*>(&b), kCrcCovered) != b.crc) return false;

  Profile p;
  Stats& s = p.stats;
  s.totalPresses = b.totalPresses;
  s.totalHits = b.totalHits;
  s.totalMisses = b.totalMisses;
  s.totalFalseStarts = b.totalFalseStarts;
  s.totalGames = b.totalGames;
  s.totalTimePlayedNs = b.totalTimePlayedNs;
  s.bestReactionUs = b.bestReactionUs;
  s.reactionUsSum = b.reactionUsSum;
  s.reactionSamples = b.reactionSamples;
  s.bestAccuracy = b.bestAccuracy;
  s.bestStreak = b.bestStreak;
  s.totalScore = b.totalScore;

  for (int i = 0; i < kModeCount; ++i) {
    ModeStats& m = s.modes[i];
    m.games = b.modeGames[i];
    m.hits = b.modeHits[i];
    m.presses = b.modePresses[i];
    m.misses = b.modeMisses[i];
    m.bestScore = b.modeBestScore[i];
    m.totalScore = b.modeTotalScore[i];
    m.bestReactionUs = b.modeBestReactionUs[i];
    m.reactionUsSum = b.modeReactionUsSum[i];
    m.reactionSamples = b.modeReactionSamples[i];
    m.bestAccuracy = b.modeBestAccuracy[i];
    m.bestStreak = b.modeBestStreak[i];
    m.bestLevel = b.modeBestLevel[i];
    m.bestSurvivalNs = b.modeBestSurvivalNs[i];
    m.totalTimeNs = b.modeTotalTimeNs[i];
    m.lastPlayedUnixMs = b.modeLastPlayedMs[i];
  }

  // A mask can only have bits for achievements that exist.
  if (b.achievementMask >= (1u << kAchievementCount)) return false;
  p.achievementMask = b.achievementMask;

  p.settings.sound = b.sound != 0;
  p.settings.music = b.music != 0;
  p.settings.haptics = b.haptics != 0;
  p.settings.showFps = b.showFps != 0;
  p.settings.calibrationMs = b.calibrationMs;
  p.settings.hapticsStrength = b.hapticsStrength;
  p.settings.clampToValid();

  *out = p;
  return true;
}

}  // namespace pp
