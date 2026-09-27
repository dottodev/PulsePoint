// Local save file: binary, versioned, checksummed, written atomically.
//
// There is no account and no network.  The whole profile is a single fixed-size
// record, so writing it is a memcpy into a buffer, a CRC and a rename.  That
// combination means a kill -9, a flat battery or a full disk can lose the most
// recent round but can never leave a half-written profile behind: the previous
// good file is only replaced once the new one is complete and verified.
#pragma once

#include <cstddef>
#include <cstdint>

#include "achievements.h"
#include "settings.h"
#include "stats.h"

namespace pp {

// Bumped whenever the layout changes.  A mismatch is treated as "no profile".
constexpr uint32_t kProfileMagic = 0x50507A31;  // "PPz1"
constexpr uint32_t kProfileVersion = 1;

#pragma pack(push, 1)
struct ProfileBlob {
  uint32_t magic;
  uint32_t version;

  // Lifetime counters.
  uint64_t totalPresses;
  uint64_t totalHits;
  uint64_t totalMisses;
  uint64_t totalFalseStarts;
  uint64_t totalGames;
  uint64_t totalTimePlayedNs;
  uint32_t bestReactionUs;
  uint64_t reactionUsSum;
  uint64_t reactionSamples;
  float bestAccuracy;
  uint32_t bestStreak;
  uint64_t totalScore;

  // Per-mode block, written field by field so the layout is explicit.
  uint64_t modeGames[kModeCount];
  uint64_t modeHits[kModeCount];
  uint64_t modePresses[kModeCount];
  uint64_t modeMisses[kModeCount];
  uint64_t modeBestScore[kModeCount];
  uint64_t modeTotalScore[kModeCount];
  uint32_t modeBestReactionUs[kModeCount];
  uint64_t modeReactionUsSum[kModeCount];
  uint64_t modeReactionSamples[kModeCount];
  float modeBestAccuracy[kModeCount];
  uint32_t modeBestStreak[kModeCount];
  uint32_t modeBestLevel[kModeCount];
  uint64_t modeBestSurvivalNs[kModeCount];
  uint64_t modeTotalTimeNs[kModeCount];
  uint64_t modeLastPlayedMs[kModeCount];

  // Achievements: one bit per entry, bit N is achievement N.
  uint32_t achievementMask;

  // Settings.
  uint8_t sound;
  uint8_t music;
  uint8_t haptics;
  uint8_t showFps;
  int32_t calibrationMs;
  float hapticsStrength;

  uint32_t crc;  // CRC32 of every preceding byte.
};
#pragma pack(pop)

struct Profile {
  Stats stats;
  Settings settings;
  uint32_t achievementMask = 0;
  // Bumped on every successful save; handy for the debug overlay.
  uint32_t revision = 0;
};

// Serialises the profile into `out`, which must be at least profileBlobSize().
size_t serializeProfile(const Profile& p, uint8_t* out, size_t capacity);

// Returns false on a short buffer, a bad magic, a wrong version, a bad CRC or
// an out-of-range mode index -- in every one of those cases nothing is written
// to `out` and the caller should fall back to a fresh profile.
bool deserializeProfile(const uint8_t* in, size_t size, Profile* out);

constexpr size_t profileBlobSize() { return sizeof(ProfileBlob); }

uint32_t crc32(const uint8_t* data, size_t size, uint32_t seed = 0);

// Filesystem helpers, implemented in profile_io.cpp so the pure logic above
// stays testable without a platform.
bool saveProfileToFile(const char* path, const Profile& p);
bool loadProfileFromFile(const char* path, Profile* p);

}  // namespace pp
