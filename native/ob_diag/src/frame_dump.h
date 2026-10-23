#ifndef OB_DIAG_FRAME_DUMP_H
#define OB_DIAG_FRAME_DUMP_H

#include <cstdint>

// Writes one presented RGBA8 frame as <dir>/frame-<vsyncTick, 8 digits>.png at its native size.
// Returns false on a null or empty dir, null pixels, a zero size, or a failed write.
bool obDumpFrame(const char *dir, uint64_t vsyncTick, const uint8_t *rgba, uint32_t width, uint32_t height);

// Writes one presented RGBA8 frame as <dir>/<fileName> at its native size (creates dir).
// Returns false on a null or empty dir or fileName, null pixels, a zero size, or a failed write.
bool obDumpFrameNamed(const char *dir, const char *fileName, const uint8_t *rgba, uint32_t width, uint32_t height);

#endif
