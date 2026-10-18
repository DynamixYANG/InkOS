#pragma once
#include "LogPolicy.h"
#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>
namespace ink {
// Persistent diagnostics on the external LittleFS (see Log.h for the RAM ring).
// Writer task: core 0, prio 1; never touches frameMutex or the panel.
//   /logs/current.log          appended in batches (>= 8 KiB pending or 15 s)
//   /logs/inkos-NNNNNN.tar.gz  rotated at 64 KiB: ustar with one .log, gzip
//                              (ROM miniz raw deflate + CRC32), streaming.
// Retention (LogPolicy): archives older than log_retention_days (only once the
// clock is valid), then at most LogArchiveMaxFiles archives and a byte budget of
// LogBudgetPercent of the space beyond the reserve not used by other data; the
// filesystem always keeps LogFsReserveBytes free (calendar/energy caches).
// Pruning runs at boot, at rotation, on setting changes and once a day while
// logging is enabled (never on a periodic wake while disabled).
constexpr uint32_t LogRotateBytes = 65536, LogFsReserveBytes = 524288;
void startLogStore();               // after ensureStorage(), before tasks log
void applyLoggingEnabled(bool on);  // runtime toggle; flushes on disable
void setLogRetentionDays(uint16_t days); // validated by the caller; triggers a prune
uint16_t logRetentionDays();
cJSON *logCommand(const char *cmd, cJSON *j); // nullptr if not a log command
struct LogFileInfo {
  char name[32];
  uint32_t size, mtime;
};
} // namespace ink
