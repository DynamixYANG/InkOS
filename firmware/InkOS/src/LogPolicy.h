#pragma once
#include <stddef.h>
#include <stdint.h>
// Pure log naming/retention rules (no Arduino), shared by the writer task and host tests.
namespace ink {
constexpr uint32_t ClockValidAfter = 1700000000u; // Earlier = NTP not synced yet.
constexpr uint16_t LogRetentionChoices[] = {7, 21, 35, 90, 180};
constexpr uint16_t LogRetentionDefaultDays = 35;
bool validRetentionDays(int days);
// Archive name "inkos-NNNNNN.tar.gz" (exactly 6 digits).
bool logArchiveSeq(const char *name, uint32_t *seq);
// "current.log" or an archive name. No directories, no "..".
bool validLogName(const char *name);
// Accepts "name" or "/logs/name"; writes the bare name. Rejects any other path.
bool normalizeLogName(const char *in, char *out, size_t cap);
// Archive byte budget: Percent of the space logs may use beyond the reserve,
// i.e. (total - reserve - non-log data), clamped to at least MinBytes.
constexpr uint32_t LogBudgetPercent = 50, LogBudgetMinBytes = 262144, LogArchiveMaxFiles = 300;
uint32_t logArchiveBudget(uint64_t fsTotal, uint64_t nonLogUsed, uint32_t reserve);
struct LogArchiveEntry {
  uint32_t seq, bytes, endTime; // endTime: rotation time (Unix s) or 0 if unknown.
};
struct LogPruneLimits {
  uint32_t now;            // Unix seconds; < ClockValidAfter disables age pruning.
  uint32_t retentionDays;  // 0 = no age limit.
  uint32_t maxFiles;
  uint64_t maxBytes;
  int64_t freeBytes;       // Filesystem free space now.
  uint32_t reserve;        // Keep at least this much free.
};
// entries sorted oldest first (by seq). Marks remove[i]; returns how many.
// Order: age > retention, then count/byte caps, then the free-space reserve,
// always deleting the oldest remaining archive first.
int planLogPrune(const LogArchiveEntry *entries, int n, const LogPruneLimits &limits, bool *remove);
} // namespace ink
