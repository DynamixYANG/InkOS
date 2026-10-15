#include "Services.h"
#include "Zones.h"
#include <algorithm>
#include <cJSON.h>
#include <esp_heap_caps.h>
namespace ink {
static const cJSON *field(const cJSON *j, const char *k) {
  return cJSON_GetObjectItemCaseSensitive(j, k);
}
static double val(const cJSON *j, const char *k, double def = NAN) {
  auto q = field(j, k);
  return cJSON_IsNumber(q) ? q->valuedouble : def;
}
static double at(const cJSON *j, const char *k, int i, double def = NAN) {
  auto q = cJSON_GetArrayItem(field(j, k), i);
  return cJSON_IsNumber(q) ? q->valuedouble : def;
}
static bool fetchOpenMeteo(Weather &w, const Settings &settings) {
  String zone(settings.localZone);
  zone.replace("/", "%2F");
  String url =
      String("https://api.open-meteo.com/v1/forecast?latitude=") + String(settings.latitude, 4) +
      "&longitude=" + String(settings.longitude, 4) +
      "&current=temperature_2m,relative_humidity_2m,"
      "apparent_temperature,is_day,weather_code,pressure_msl,wind_speed_10m&hourly=temperature_2m,"
      "precipitation,weather_code,cloud_cover,wind_speed_10m,is_day,precipitation_probability&"
      "daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_sum,"
      "precipitation_probability_max,wind_speed_10m_max,uv_index_max&timezone=" +
      zone + "&timeformat=unixtime&wind_speed_unit=ms&forecast_days=7";
  HttpResponse r;
  if (!request(url, r) || r.status != 200) {
    snprintf(w.error, sizeof w.error, "Weather HTTP %d; retrying", r.status);
    return false;
  }
  cJSON *j = cJSON_Parse((char *)r.bytes);
  if (!j) {
    strlcpy(w.error, "Invalid weather response", sizeof w.error);
    return false;
  }
  const auto *c = field(j, "current"), *d = field(j, "daily"), *h = field(j, "hourly");
  bool good = cJSON_IsObject(c) && cJSON_GetArraySize(field(d, "time")) >= 7;
  if (good) {
    w.temp = val(c, "temperature_2m");
    w.feels = val(c, "apparent_temperature");
    w.humidity = val(c, "relative_humidity_2m", -1);
    w.wind = val(c, "wind_speed_10m");
    w.code = val(c, "weather_code", -1);
    w.daylight = val(c, "is_day", 1) > 0;
    w.pressure = val(c, "pressure_msl");
    for (int i = 0; i < 7; i++) {
      auto &q = w.days[i];
      q.epoch = at(d, "time", i, 0);
      q.high = at(d, "temperature_2m_max", i);
      q.low = at(d, "temperature_2m_min", i);
      q.code = at(d, "weather_code", i, -1);
      q.rise = at(d, "sunrise", i, 0);
      q.set = at(d, "sunset", i, 0);
      q.rain = at(d, "precipitation_sum", i);
      q.wind = at(d, "wind_speed_10m_max", i);
      q.probability = at(d, "precipitation_probability_max", i, -1);
      q.uv = at(d, "uv_index_max", i);
    }
    w.hourCount = std::min(170, cJSON_GetArraySize(field(h, "time")));
    for (int i = 0; i < w.hourCount; i++) {
      auto &q = w.hours[i];
      q.epoch = at(h, "time", i, 0);
      q.temp = at(h, "temperature_2m", i);
      q.rain = at(h, "precipitation", i);
      q.wind = at(h, "wind_speed_10m", i);
      q.code = at(h, "weather_code", i, -1);
      q.cloud = at(h, "cloud_cover", i, -1);
      q.probability = at(h, "precipitation_probability", i, -1);
      q.day = at(h, "is_day", i, 1) > 0;
    }
    w.valid = isfinite(w.temp) && w.hourCount > 0;
    w.fetched = time(nullptr);
    w.error[0] = 0;
    w.provider = 0;
  }
  cJSON_Delete(j);
  return good && w.valid;
}
static time_t isoTime(const char *s) {
  int y, m, d, h, mi, se;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &mi, &se) != 6)
    return 0;
  return Clock::epoch(y, m, d, h, mi) + se;
}
static int symbolCode(const char *s) {
  String v(s);
  if (v.indexOf("thunder") >= 0)
    return 95;
  if (v.indexOf("snow") >= 0)
    return v.startsWith("heavy") ? 75 : 73;
  if (v.indexOf("sleet") >= 0)
    return 68;
  if (v.indexOf("rain") >= 0)
    return v.startsWith("heavy") ? 65 : v.startsWith("light") ? 51 : 61;
  if (v.startsWith("clearsky"))
    return 0;
  if (v.startsWith("fair"))
    return 1;
  if (v.startsWith("partlycloudy"))
    return 2;
  if (v.startsWith("cloudy"))
    return 3;
  if (v.startsWith("fog"))
    return 45;
  return -1;
}
bool decodeMet(const char *raw, Weather &w, const Settings &settings) {
  auto j = cJSON_Parse(raw);
  if (!j)
    return false;
  auto series = field(field(j, "properties"), "timeseries");
  int count = cJSON_GetArraySize(series);
  if (!count) {
    cJSON_Delete(j);
    return false;
  }
  auto local = inZone(time(nullptr), settings.localZone);
  time_t wall = Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  for (int d = 0; d < 7; d++) {
    w.days[d] = Day();
    w.days[d].epoch = fromZone(wall + d * 86400, settings.localZone);
    w.days[d].rain = 0;
  }
  w.hourCount = 0;
  bool first = true;
  for (int i = 0; i < count; i++) {
    auto row = cJSON_GetArrayItem(series, i);
    auto stamp = field(row, "time");
    if (!cJSON_IsString(stamp))
      continue;
    time_t t = isoTime(stamp->valuestring);
    auto dt = inZone(t, settings.localZone);
    time_t dw = Clock::epoch(dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday);
    int d = int((dw - wall) / 86400);
    if (d < 0 || d >= 7)
      continue;
    auto data = field(row, "data"), instant = field(field(data, "instant"), "details");
    auto period = field(data, "next_1_hours");
    int hours = 1;
    if (!cJSON_IsObject(period)) {
      period = field(data, "next_6_hours");
      hours = 6;
    }
    auto summary = field(period, "summary"), details = field(period, "details");
    auto symbol = field(summary, "symbol_code");
    const char *name = cJSON_IsString(symbol) ? symbol->valuestring : "";
    float temp = val(instant, "air_temperature"), wind = val(instant, "wind_speed"),
          rain = val(details, "precipitation_amount"),
          uv = val(instant, "ultraviolet_index_clear_sky");
    int probability = int(val(details, "probability_of_precipitation", -1));
    int code = symbolCode(name);
    if (first) {
      w.temp = temp;
      w.feels = val(instant, "apparent_air_temperature");
      w.wind = wind;
      w.humidity = val(instant, "relative_humidity", -1);
      w.pressure = val(instant, "air_pressure_at_sea_level");
      w.code = code;
      w.daylight = String(name).endsWith("_day") ||
                   (!String(name).endsWith("_night") && dt.tm_hour >= 7 && dt.tm_hour < 19);
      first = false;
    }
    if (w.hourCount < 170) {
      auto &q = w.hours[w.hourCount++];
      q = Hour();
      q.epoch = t;
      q.temp = temp;
      q.wind = wind;
      q.rain = isfinite(rain) ? rain / hours : NAN;
      q.probability = probability;
      q.code = code;
      q.cloud = val(instant, "cloud_area_fraction", -1);
      q.periodHours = hours;
      q.day = !String(name).endsWith("_night") &&
              (String(name).endsWith("_day") || (dt.tm_hour >= 7 && dt.tm_hour < 19));
    }
    auto &q = w.days[d];
    if (isfinite(temp)) {
      q.high = isfinite(q.high) ? std::max(q.high, temp) : temp;
      q.low = isfinite(q.low) ? std::min(q.low, temp) : temp;
    }
    float pHigh = val(details, "air_temperature_max"), pLow = val(details, "air_temperature_min");
    if (isfinite(pHigh))
      q.high = isfinite(q.high) ? std::max(q.high, pHigh) : pHigh;
    if (isfinite(pLow))
      q.low = isfinite(q.low) ? std::min(q.low, pLow) : pLow;
    if (isfinite(wind))
      q.wind = isfinite(q.wind) ? std::max(q.wind, wind) : wind;
    if (isfinite(uv))
      q.uv = isfinite(q.uv) ? std::max(q.uv, uv) : uv;
    q.probability = std::max(q.probability, probability);
    if (q.code < 0 || code > q.code)
      q.code = code;
    if (isfinite(rain))
      for (int h = 0; h < hours; h++) {
        auto t2 = inZone(t + h * 3600, settings.localZone);
        int n = int((Clock::epoch(t2.tm_year + 1900, t2.tm_mon + 1, t2.tm_mday) - wall) / 86400);
        if (n >= 0 && n < 7)
          w.days[n].rain += rain / hours;
      }
  }
  cJSON_Delete(j);
  w.valid = !first && isfinite(w.temp) && w.hourCount > 0;
  if (w.valid) {
    w.fetched = time(nullptr);
    w.provider = 1;
    w.error[0] = 0;
  }
  return w.valid;
}
static Weather *metCache = nullptr;
static String metLocation, metModified;
static time_t metNext = 0, metRetryAt = 0;
static String metRetryLocation;
static bool fetchMet(Weather &w, const Settings &s) {
  String location = String(s.latitude, 4) + "," + String(s.longitude, 4);
  if (location == metRetryLocation && time(nullptr) < metRetryAt)
    return false;
  if (location == metLocation && metCache && metCache->valid && time(nullptr) < metNext) {
    w = *metCache;
    return true;
  }
  HttpResponse r;
  RequestOptions options;
  options.redirects = true;
  options.userAgent = "InkOS/0.2 ESP32 personal e-paper display";
  if (*s.metContact)
    options.userAgent += " " + String(s.metContact);
  if (location == metLocation)
    options.lastModified = metModified;
  String url =
      "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=" + String(s.latitude, 4) +
      "&lon=" + String(s.longitude, 4);
  if (!request(url, r, 524288, "", "", options) ||
      (r.status != 200 && r.status != 203 && r.status != 304)) {
    snprintf(w.error, sizeof w.error, "MET Norway HTTP %d; retrying", r.status);
    metRetryAt = time(nullptr) + std::max(60, r.retrySeconds);
    metRetryLocation = location;
    return false;
  }
  tm expiry = {};
  time_t expires = 0;
  if (strptime(r.expires.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &expiry))
    expires = mktime(&expiry);
  metNext = std::max(time(nullptr) + 900, expires);
  metLocation = location;
  if (r.lastModified.length())
    metModified = r.lastModified;
  if (r.status == 304 && metCache && metCache->valid) {
    w = *metCache;
    return true;
  }
  if (!decodeMet((const char *)r.bytes, w, s)) {
    strlcpy(w.error, "Invalid MET Norway response", sizeof w.error);
    return false;
  }
  if (!metCache)
    metCache = (Weather *)heap_caps_malloc(sizeof(Weather), MALLOC_CAP_SPIRAM);
  if (metCache)
    *metCache = w;
  return true;
}
bool fetchWeather(Weather &w) {
  auto cfg = credentials();
  return cfg.settings.weatherSource == 1 ? fetchMet(w, cfg.settings)
                                         : fetchOpenMeteo(w, cfg.settings);
}
} // namespace ink
