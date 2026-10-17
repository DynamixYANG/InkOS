#include "LogPolicy.h"
#include <string.h>
namespace ink {
bool validRetentionDays(int days) {
  for (auto d : LogRetentionChoices)
    if (days == d)
      return true;
  return false;
}
bool logArchiveSeq(const char *name, uint32_t *seq) {
  static const char prefix[] = "inkos-", suffix[] = ".tar.gz";
  if (!name || strlen(name) != 6 + 6 + 7 || strncmp(name, prefix, 6) || strcmp(name + 12, suffix))
    return false;
  uint32_t v = 0;
  for (int i = 6; i < 12; i++) {
    if (name[i] < '0' || name[i] > '9')
      return false;
    v = v * 10 + uint32_t(name[i] - '0');
  }
  if (seq)
    *seq = v;
  return true;
}
bool validLogName(const char *name) {
  return name && (!strcmp(name, "current.log") || logArchiveSeq(name, nullptr));
}
bool normalizeLogName(const char *in, char *out, size_t cap) {
  if (!in || !cap)
    return false;
  const char *bare = in;
  if (!strncmp(in, "/logs/", 6))
    bare = in + 6;
  // validLogName already excludes '/', '\\' and "..": only fixed patterns pass.
  if (!validLogName(bare) || strlen(bare) + 1 > cap)
    return false;
  strcpy(out, bare);
  return true;
}
uint32_t logArchiveBudget(uint64_t fsTotal, uint64_t nonLogUsed, uint32_t reserve) {
  uint64_t usable = fsTotal > uint64_t(reserve) + nonLogUsed ? fsTotal - reserve - nonLogUsed : 0;
  uint64_t budget = usable * LogBudgetPercent / 100;
  if (budget < LogBudgetMinBytes)
    budget = LogBudgetMinBytes;
  return budget > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(budget);
}
int planLogPrune(const LogArchiveEntry *e, int n, const LogPruneLimits &l, bool *remove) {
  int removed = 0, kept = n;
  uint64_t bytes = 0;
  int64_t freeBytes = l.freeBytes;
  for (int i = 0; i < n; i++) {
    remove[i] = false;
    bytes += e[i].bytes;
  }
  auto drop = [&](int i) {
    remove[i] = true;
    removed++;
    kept--;
    bytes -= e[i].bytes;
    freeBytes += e[i].bytes;
  };
  if (l.retentionDays && l.now >= ClockValidAfter) {
    uint64_t horizon = uint64_t(l.retentionDays) * 86400u;
    for (int i = 0; i < n; i++)
      if (e[i].endTime >= ClockValidAfter && e[i].endTime <= l.now && l.now - e[i].endTime > horizon)
        drop(i);
  }
  for (int i = 0; i < n; i++) {
    if (remove[i])
      continue;
    if (kept > int(l.maxFiles) || bytes > l.maxBytes || freeBytes < int64_t(l.reserve))
      drop(i);
  }
  return removed;
}
} // namespace ink
