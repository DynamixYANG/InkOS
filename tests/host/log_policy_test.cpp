#include "LogPolicy.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace ink;
static int count(const bool *r, int n) { int c = 0; for (int i = 0; i < n; i++) c += r[i]; return c; }
int main() {
  // ---- names / path validation ----
  const char *good[] = {"current.log", "inkos-000001.tar.gz", "inkos-999999.tar.gz", "/logs/current.log", "/logs/inkos-000042.tar.gz"};
  const char *bad[] = {"", "..", "../current.log", "/logs/../agenda.bin", "/logs/../logs/current.log", "logs/current.log",
                       "/current.log", "/logs//current.log", "/logs/sub/current.log", "agenda.bin", "/feed0.ics",
                       "inkos-00001.tar.gz", "inkos-0000001.tar.gz", "inkos-00000a.tar.gz", "inkos-000001.tar.gz/",
                       "inkos-000001.tar.gzx", "INKOS-000001.tar.gz", "current.log ", "current.log/..", ".rotate.tmp",
                       "/logs/.rotate.tmp", "\\logs\\current.log", "/LOGS/current.log", "/logs/"};
  char out[32];
  for (auto n : good) assert(normalizeLogName(n, out, sizeof out) && validLogName(out));
  assert(normalizeLogName("/logs/inkos-000042.tar.gz", out, sizeof out) && !strcmp(out, "inkos-000042.tar.gz"));
  for (auto n : bad) { if (normalizeLogName(n, out, sizeof out)) { printf("accepted %s\n", n); return 1; } }
  assert(!normalizeLogName(nullptr, out, sizeof out) && !normalizeLogName("current.log", out, 5));
  uint32_t seq = 0; assert(logArchiveSeq("inkos-000123.tar.gz", &seq) && seq == 123);
  for (int d : {7, 21, 35, 90, 180}) assert(validRetentionDays(d));
  for (int d : {0, 1, 30, 36, 365, -7}) assert(!validRetentionDays(d));
  // ---- budget ----
  assert(logArchiveBudget(4194304, 65536, 524288) == (4194304 - 524288 - 65536) / 2);
  assert(logArchiveBudget(1000000, 900000, 524288) == LogBudgetMinBytes);
  // ---- pruning with a fake clock ----
  const uint32_t day = 86400, now = 1792270000;
  std::vector<LogArchiveEntry> e;
  for (uint32_t i = 0; i < 10; i++) e.push_back({i + 1, 8192, now - (40 - i * 4) * day}); // ages 40,36,...,4 days
  bool rm[512];
  LogPruneLimits l{now, 35, 200, 10u << 20, 3 << 20, 524288};
  int n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 2 && rm[0] && rm[1] && !rm[2]); // 40 and 36 days > 35
  l.retentionDays = 7;
  n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 9 && !rm[9]); // only the 4-day-old one stays (8 days old is out)
  // Clock not set: never delete by age.
  l.now = 1000; l.retentionDays = 7;
  assert(planLogPrune(e.data(), int(e.size()), l, rm) == 0);
  // Unknown archive time (rotated before NTP): kept by age, still subject to caps.
  std::vector<LogArchiveEntry> u = e; u[0].endTime = 0; l.now = now; l.retentionDays = 35;
  n = planLogPrune(u.data(), int(u.size()), l, rm);
  assert(n == 1 && !rm[0] && rm[1]);
  // Future timestamps (clock jumped back) are not aged out.
  u = e; u[0].endTime = now + 10 * day; n = planLogPrune(u.data(), int(u.size()), l, rm); assert(!rm[0]);
  // Count cap, oldest first.
  l = {now, 180, 4, 10u << 20, 3 << 20, 524288};
  n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 6 && rm[0] && rm[5] && !rm[6] && count(rm + 6, 4) == 0);
  // Byte cap.
  l = {now, 180, 200, 3 * 8192, 3 << 20, 524288};
  n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 7 && !rm[7] && !rm[8] && !rm[9]);
  // Free-space reserve: need 524288 free, have 524288 - 20000 → delete 3 oldest (8192 each).
  l = {now, 180, 200, 10u << 20, 524288 - 20000, 524288};
  n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 3 && rm[0] && rm[1] && rm[2] && !rm[3]);
  // Retention within the space limit: age prune and caps combine.
  l = {now, 35, 5, 10u << 20, 3 << 20, 524288};
  n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 5 && rm[0] && rm[1] && rm[4] && !rm[5]);
  // A day later with a fake clock advance: one more ages out.
  l = {now + 4 * day, 35, 200, 10u << 20, 3 << 20, 524288};
  n = planLogPrune(e.data(), int(e.size()), l, rm);
  assert(n == 3 && rm[2] && !rm[3]);
  assert(planLogPrune(nullptr, 0, l, rm) == 0);
  puts("OK");
}
