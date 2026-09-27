// Filesystem side of the save file.
//
// The write is temp-file + fsync + rename, which is the standard way to make a
// replace atomic on POSIX: a reader either sees the old complete file or the
// new complete file, never a partial one.  The previous good copy is kept as a
// .bak and is used automatically if the primary file fails to validate.
#include "profile.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace pp {
namespace {

constexpr int kMaxBlobSize = 4096;

bool readWholeFile(const char* path, uint8_t* buf, size_t capacity, size_t* outSize) {
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return false;
  const size_t n = std::fread(buf, 1, capacity, f);
  const bool ok = std::ferror(f) == 0;
  std::fclose(f);
  if (!ok) return false;
  *outSize = n;
  return true;
}

void joinPath(char* dst, size_t cap, const char* dir, const char* name) {
  size_t i = 0;
  for (; dir[i] != '\0' && i + 1 < cap; ++i) dst[i] = dir[i];
  if (i > 0 && dst[i - 1] != '/') {
    dst[i++] = '/';
  }
  for (size_t j = 0; name[j] != '\0' && i + 1 < cap; ++j) dst[i++] = name[j];
  dst[i] = '\0';
}

}  // namespace

bool saveProfileToFile(const char* dir, const Profile& p) {
  if (dir == nullptr) return false;

  uint8_t blob[kMaxBlobSize];
  const size_t n = serializeProfile(p, blob, sizeof(blob));
  if (n == 0) return false;

  char finalPath[512];
  char tempPath[512];
  char bakPath[512];
  joinPath(finalPath, sizeof(finalPath), dir, "profile.bin");
  joinPath(tempPath, sizeof(tempPath), dir, "profile.tmp");
  joinPath(bakPath, sizeof(bakPath), dir, "profile.bak");

  std::FILE* f = std::fopen(tempPath, "wb");
  if (f == nullptr) return false;
  const size_t written = std::fwrite(blob, 1, n, f);
  const bool flushed = written == n && std::fflush(f) == 0;
  std::fclose(f);
  if (!flushed) {
    std::remove(tempPath);
    return false;
  }
  // Durability before the rename, otherwise a crash can leave the new name
  // pointing at an empty inode.
  const int fd = ::open(tempPath, O_RDONLY);
  if (fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }

  // Keep one generation of backup, then swap atomically.
  std::remove(bakPath);
  std::FILE* existing = std::fopen(finalPath, "rb");
  if (existing != nullptr) {
    std::fclose(existing);
    std::rename(finalPath, bakPath);
  }
  if (std::rename(tempPath, finalPath) != 0) {
    std::remove(tempPath);
    // Last resort: put the backup back so the player keeps their history.
    std::rename(bakPath, finalPath);
    return false;
  }
  return true;
}

bool loadProfileFromFile(const char* dir, Profile* p) {
  if (dir == nullptr || p == nullptr) return false;

  char path[512];
  uint8_t blob[kMaxBlobSize];
  size_t n = 0;

  joinPath(path, sizeof(path), dir, "profile.bin");
  if (readWholeFile(path, blob, sizeof(blob), &n) && deserializeProfile(blob, n, p)) {
    return true;
  }

  joinPath(path, sizeof(path), dir, "profile.bak");
  if (readWholeFile(path, blob, sizeof(blob), &n) && deserializeProfile(blob, n, p)) {
    return true;
  }
  return false;
}

}  // namespace pp
