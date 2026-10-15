#include "Energy.h"
#include "Services.h"
#include "Zones.h"
#include <LittleFS.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <new>
namespace ink {
time_t parsePriceTimestamp(const char *s) {
  if (!s)
    return 0;
  int y, m, d, h, min, sec, n = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d%n", &y, &m, &d, &h, &min, &sec, &n) != 6 || y < 2000 ||
      y > 2100 || m < 1 || m > 12 || d < 1 || d > Clock::monthDays(y, m) || h < 0 || h > 23 ||
      min < 0 || min > 59 || sec < 0 || sec > 59)
    return 0;
  const char *p = s + n;
  if (*p == '.') {
    p++;
    if (!isdigit(*p))
      return 0;
    while (isdigit(*p))
      p++;
  }
  int offset = 0;
  if (*p == 'Z' && !p[1]) {
  } else if ((*p == '+' || *p == '-') && strlen(p) == 6 && p[3] == ':' && isdigit(p[1]) &&
             isdigit(p[2]) && isdigit(p[4]) && isdigit(p[5])) {
    int hh = (p[1] - '0') * 10 + p[2] - '0', mm = (p[4] - '0') * 10 + p[5] - '0';
    if (hh > 23 || mm > 59)
      return 0;
    offset = (hh * 60 + mm) * 60 * (*p == '+' ? 1 : -1);
  } else
    return 0;
  return Clock::epoch(y, m, d, h, min) + sec - offset;
}
const PriceHour *priceAt(const Energy &e, time_t t) {
  if (!e.enabled || !e.valid)
    return nullptr;
  time_t start = t / 3600 * 3600;
  for (int i = 0; i < e.count; i++)
    if (e.hours[i].start == start)
      return &e.hours[i];
  return nullptr;
}
double totalPriceCt(const Energy &e, time_t t) {
  auto p = priceAt(e, t);
  if (!p)
    return NAN;
  double tax = 1 + e.vatPercent / 100, fees = e.marginCt + e.transmissionCt;
  return double(p->spotNetCt) * tax + fees * (e.feesIncludeVat ? 1 : tax);
}
struct Interval {
  time_t start, end;
  double price;
};
bool parseEnergyPrices(const char *raw, Energy &out, time_t now, int provider) {
  auto j = cJSON_Parse(raw);
  auto rows = cJSON_GetObjectItem(j, "prices");
  int count = cJSON_GetArraySize(rows);
  if (!cJSON_IsArray(rows) || count < 1 || count > 512) {
    cJSON_Delete(j);
    return false;
  }
  auto intervals = (Interval *)heap_caps_calloc(count, sizeof(Interval), MALLOC_CAP_SPIRAM);
  if (!intervals) {
    cJSON_Delete(j);
    return false;
  }
  bool valid = true;
  int used = 0;
  for (int i = 0; i < count && valid; i++) {
    auto r = cJSON_GetArrayItem(rows, i),
         price = cJSON_GetObjectItem(r, provider == 0 ? "value" : "price"),
         start = cJSON_GetObjectItem(r, provider == 0 ? "date" : "startDate"),
         end = cJSON_GetObjectItem(r, "endDate");
    if (!cJSON_IsNumber(price) || !isfinite(price->valuedouble) ||
        fabs(price->valuedouble) > 100000 || !cJSON_IsString(start) ||
        (provider != 0 && !cJSON_IsString(end))) {
      valid = false;
      break;
    }
    time_t a = parsePriceTimestamp(start->valuestring),
           b = provider == 0 ? a + 900 : parsePriceTimestamp(end->valuestring);
    // v2 ends at xx:14:59.999Z; normalize its inclusive end to [start,end).
    if (b - a == 899 || b - a == 3599)
      b++;
    if (!a || (b - a != 900 && b - a != 3600) || a % 900 || a / 3600 != (b - 1) / 3600) {
      valid = false;
      break;
    }
    double net = price->valuedouble;
    if (provider == 0)
      net /= 10; // net EUR/MWh -> net ct/kWh.
    else if (provider == 1) {
      double sourceVat = a >= Clock::epoch(2024, 9, 1)                                    ? 25.5
                         : a >= Clock::epoch(2022, 12, 1) && a < Clock::epoch(2023, 5, 1) ? 10
                                                                                          : 24;
      net /= (1 + sourceVat / 100);
    }
    if (a >= now / 3600 * 3600 - 72 * 3600 && a < now / 3600 * 3600 + 96 * 3600)
      intervals[used++] = {a, b, net};
  }
  cJSON_Delete(j);
  std::sort(intervals, intervals + used,
            [](const Interval &a, const Interval &b) { return a.start < b.start; });
  PriceHour fresh[MAX_PRICE_HOURS];
  int freshCount = 0;
  for (int i = 0; i < used && valid;) {
    time_t hour = intervals[i].start / 3600 * 3600, cursor = hour;
    double weighted = 0;
    bool complete = true;
    do {
      const auto &q = intervals[i];
      if (i && q.start < intervals[i - 1].end) {
        valid = false;
        break;
      }
      if (q.start != cursor)
        complete = false;
      cursor = q.end;
      weighted += q.price * double(q.end - q.start);
      i++;
    } while (i < used && intervals[i].start / 3600 * 3600 == hour);
    if (complete && cursor == hour + 3600) {
      if (freshCount == MAX_PRICE_HOURS) {
        valid = false;
        break;
      }
      fresh[freshCount++] = {hour, float(weighted / 3600)};
    }
  }
  free(intervals);
  if (!valid || !freshCount)
    return false;
  // Retain older published hours across the provider's midnight rolling window.
  for (int i = 0; i < out.count; i++) {
    auto q = out.hours[i];
    if (q.start < now / 3600 * 3600 - 72 * 3600)
      continue;
    bool exists = false;
    for (int k = 0; k < freshCount; k++)
      exists |= fresh[k].start == q.start;
    if (!exists && freshCount < MAX_PRICE_HOURS)
      fresh[freshCount++] = q;
  }
  std::sort(fresh, fresh + freshCount,
            [](const PriceHour &a, const PriceHour &b) { return a.start < b.start; });
  // If full, keep the nearest 96 hours around the current interval.
  memcpy(out.hours, fresh, sizeof(PriceHour) * freshCount);
  out.count = freshCount;
  out.valid = true;
  out.fetched = now;
  out.error[0] = 0;
  return true;
}
static uint32_t hash(const void *p, size_t n) {
  uint32_t h = 2166136261u;
  auto b = (const uint8_t *)p;
  while (n--)
    h = (h ^ *b++) * 16777619u;
  return h;
}
static uint32_t sourceKey(const EnergyConfig &e) {
  return hash(e.endpoint.c_str(), e.endpoint.length()) ^ uint32_t(e.provider);
}
static bool storage() {
  return LittleFS.begin(false, "/inkstore", 5, "storage");
}
struct CacheHeader {
  uint32_t magic, size, checksum;
};
void loadEnergy() {
  auto cfg = credentials();
  if (!cfg.energy.enabled || !storage())
    return;
  File f = LittleFS.open("/energy.bin", "r");
  CacheHeader header;
  if (!f || f.readBytes((char *)&header, sizeof header) != sizeof header ||
      header.magic != 0x31454e49 || header.size != sizeof(Energy))
    return;
  auto e = (Energy *)heap_caps_malloc(sizeof(Energy), MALLOC_CAP_SPIRAM);
  if (!e)
    return;
  bool good = f.readBytes((char *)e, sizeof(Energy)) == sizeof(Energy) &&
              hash(e, sizeof(Energy)) == header.checksum && e->sourceKey == sourceKey(cfg.energy) &&
              e->count > 0 && e->count <= MAX_PRICE_HOURS;
  if (good) {
    e->enabled = cfg.energy.enabled;
    e->marginCt = cfg.energy.marginCt;
    e->vatPercent = cfg.energy.vatPercent;
    e->transmissionCt = cfg.energy.transmissionCt;
    e->feesIncludeVat = cfg.energy.feesIncludeVat;
    model->energy = *e;
  }
  free(e);
}
static void saveCache(const Energy &e) {
  if (!storage())
    return;
  File file = LittleFS.open("/energy.next", "w");
  if (!file)
    return;
  CacheHeader h = {0x31454e49, sizeof(Energy), hash(&e, sizeof(Energy))};
  bool ok = file.write((uint8_t *)&h, sizeof h) == sizeof h &&
            file.write((const uint8_t *)&e, sizeof e) == sizeof e;
  file.flush();
  file.close();
  if (ok)
    LittleFS.rename("/energy.next", "/energy.bin");
}
void updateEnergy() {
  static uint32_t revision = ~0u, due = 0;
  static bool first = true;
  auto cfg = credentials();
  if (revision != cfg.energyRevision) {
    revision = cfg.energyRevision;
    first = true;
  }
  if (!cfg.energy.enabled || (!first && int32_t(millis() - due) < 0))
    return;
  first = false;
  auto e = (Energy *)heap_caps_malloc(sizeof(Energy), MALLOC_CAP_SPIRAM);
  if (!e)
    return;
  {
    Guard g(modelMutex);
    *e = model->energy;
  }
  uint32_t key = sourceKey(cfg.energy);
  if (e->sourceKey != key) {
    e->count = 0;
    e->valid = false;
  }
  e->sourceKey = key;
  e->enabled = true;
  e->marginCt = cfg.energy.marginCt;
  e->vatPercent = cfg.energy.vatPercent;
  e->transmissionCt = cfg.energy.transmissionCt;
  e->feesIncludeVat = cfg.energy.feesIncludeVat;
  HttpResponse response;
  RequestOptions opts;
  // Public presets canonicalize their query URLs with an HTTPS redirect.
  // No credentials are attached to presets; custom credential endpoints never redirect.
  opts.redirects = cfg.energy.provider != 2;
  opts.userAgent = "InkOS/0.3 personal Finnish electricity display";
  if (cfg.energy.provider == 2) {
    opts.headerName = cfg.energy.apiHeader;
    opts.headerValue = cfg.energy.apiKey;
  }
  String url = cfg.energy.endpoint;
  if (cfg.energy.provider == 0) {
    auto fi = Clock::finland(time(nullptr));
    time_t wall = Clock::epoch(fi.tm_year + 1900, fi.tm_mon + 1, fi.tm_mday);
    auto a = Clock::utc(Clock::fromFinland(wall - 86400)),
         b = Clock::utc(Clock::fromFinland(wall + 2 * 86400));
    char range[160];
    snprintf(range, sizeof range,
             "?quarter&start=%04d-%02d-%02dT%02d:00:00Z&end=%04d-%02d-%02dT%02d:00:00Z",
             a.tm_year + 1900, a.tm_mon + 1, a.tm_mday, a.tm_hour, b.tm_year + 1900, b.tm_mon + 1,
             b.tm_mday, b.tm_hour);
    url += range;
  }
  bool success =
      request(url, response, 131072, "", "", opts) && response.status == 200 &&
      parseEnergyPrices((const char *)response.bytes, *e, time(nullptr), cfg.energy.provider);
  uint32_t retry = 12 * 3600000u;
  if (!success) {
    snprintf(e->error, sizeof e->error, "Price HTTP/data %d; cached if available", response.status);
    retry = uint32_t(std::max(300, std::min(3600, std::max(0, response.retrySeconds)))) * 1000;
  } else {
    saveCache(*e);
    // Fetch near tomorrow's Finnish publication time without polling each display hour.
    auto fi = Clock::finland(time(nullptr));
    time_t wall = Clock::epoch(fi.tm_year + 1900, fi.tm_mon + 1, fi.tm_mday, 14, 15);
    time_t publish = Clock::fromFinland(wall);
    if (publish <= time(nullptr))
      publish = Clock::fromFinland(wall + 86400);
    retry =
        std::min(retry, uint32_t(std::max(int64_t(300), int64_t(publish - time(nullptr)))) * 1000);
    auto tomorrow =
        Clock::fromFinland(Clock::epoch(fi.tm_year + 1900, fi.tm_mon + 1, fi.tm_mday) + 86400);
    if (fi.tm_hour >= 14 && !priceAt(*e, tomorrow))
      retry = std::min<uint32_t>(retry, 3600000);
  }
  due = millis() + retry;
  e->revision++;
  // A setting may change while HTTPS is in progress; never publish old-source data.
  if (credentials().energyRevision == revision) {
    Guard g(modelMutex);
    model->energy = *e;
  }
  Serial.printf("INKOS energy valid=%d hours=%d\n", success, e->count);
  free(e);
  invalidate();
}
} // namespace ink
