#pragma once
#include <Arduino.h>
#include <time.h>
#include "Aurora.h"
namespace ink {
constexpr int WIDTH = 800, HEIGHT = 480, FRAME_BYTES = 48000, ART = 144, MAX_LYRICS = 320;
enum class Page : uint8_t { Home, Music, Forecast, Week, Moon, Sun, Energy, Count };
constexpr int PAGE_COUNT = int(Page::Count), MAX_EVENTS = 96;
Page pageAfter(Page, int delta);
struct CalendarEvent {
  time_t start = 0, end = 0;
  bool allDay = false;
  char title[128] = {0}, location[96] = {0};
};
struct Agenda {
  bool configured = false;
  time_t imported = 0, validUntil = 0;
  CalendarEvent events[MAX_EVENTS];
  int count = 0;
  uint32_t revision = 0;
  bool stale = false, truncated = false;
  char error[80] = {0};
};
struct WorldClock {
  char label[24] = {0}, zone[40] = {0};
};
struct Settings {
  char location[32] = "Oulu", localZone[40] = "Europe/Helsinki";
  double latitude = 65.0121, longitude = 25.4651;
  WorldClock worlds[2] = {{"Beijing", "Asia/Shanghai"}, {"London", "Europe/London"}};
  int weatherSource = 0;
  bool animations = false; // Quality first; frequent Home updates are opt-in.
  char metContact[96] = {0};
  uint32_t revision = 0;
};
const CalendarEvent *nextEvent(const Agenda &, time_t);
time_t localMidnight(time_t);
time_t weekMonday(time_t);
struct Rect {
  int x, y, w, h;
  bool empty() const {
    return w <= 0 || h <= 0;
  }
};
struct Day {
  time_t epoch = 0, rise = 0, set = 0;
  float high = NAN, low = NAN, rain = NAN, wind = NAN, uv = NAN;
  int code = -1, probability = -1;
};
struct Hour {
  time_t epoch = 0;
  float temp = NAN, rain = NAN, wind = NAN;
  int code = -1, cloud = -1, probability = -1;
  bool day = true;
  int periodHours = 1;
};
struct Weather {
  bool valid = false;
  time_t fetched = 0;
  float temp = NAN, feels = NAN, wind = NAN, pressure = NAN;
  int humidity = -1, code = -1;
  bool daylight = true;
  Day days[7];
  Hour hours[170];
  int hourCount = 0;
  uint32_t revision = 0;
  char error[64] = {0};
  int provider = 0;
};
constexpr int MAX_PRICE_HOURS = 96;
struct PriceHour {
  time_t start = 0;
  float spotNetCt = NAN;
};
struct Energy {
  bool enabled = true, valid = false;
  double vatPercent = 25.5, marginCt = .45, transmissionCt = 6;
  bool feesIncludeVat = false;
  time_t fetched = 0;
  PriceHour hours[MAX_PRICE_HOURS];
  int count = 0;
  uint32_t revision = 0, sourceKey = 0;
  char error[64] = {0};
};
struct Lyric {
  uint32_t atMs = 0;
  char text[192] = {0};
};
enum class LyricStatus : uint8_t { Waiting, Timed, Missing, Instrumental, Unavailable, Untimed };
struct Music {
  bool authorized = false, valid = false, playing = false, stale = false, artValid = false;
  char id[96] = {0}, title[192] = {0}, artist[192] = {0}, album[192] = {0}, error[80] = {0};
  uint32_t position = 0, duration = 0, revision = 0, artRevision = 0;
  int64_t sampleUs = 0;
  uint8_t art[ART * ART / 8] = {0};
  Lyric lines[MAX_LYRICS];
  int lineCount = 0;
  LyricStatus lyricStatus = LyricStatus::Waiting;
};
struct Model {
  Page page = Page::Home;
  uint32_t navigationRevision = 0, pageSwitchDelayMs = 500, encoderLockMs = 2000;
  int64_t navigationRequestedUs = 0;
  bool wifi = false;
  int wifiRssi = -127, batteryPercent = -1;
  float batteryVoltage = NAN;
  char ip[24] = {0};
  Weather weather;
  Energy energy;
  Music music;
  Agenda agenda;
  Aurora aurora;
  Settings settings;
  int iconTest = -1;
  int64_t iconTestUntilUs = 0;
  uint32_t revision = 0;
};
extern Model *model;
extern SemaphoreHandle_t modelMutex;
extern QueueHandle_t displayEvents;
struct Guard {
  SemaphoreHandle_t m;
  explicit Guard(SemaphoreHandle_t v) : m(v) {
    xSemaphoreTake(m, portMAX_DELAY);
  }
  ~Guard() {
    xSemaphoreGive(m);
  }
};
void invalidate();
struct Clock {
  static bool valid();
  static tm utc(time_t);
  static tm finland(time_t);
  static tm beijing(time_t);
  static tm london(time_t);
  static time_t fromFinland(time_t wallTime);
  static time_t epoch(int, int, int, int = 0, int = 0);
  static int monthDays(int, int);
};
struct Holiday {
  int month, day;
  const char *name;
  bool publicHoliday, flag;
};
int holidays(int year, Holiday *result, int capacity);
uint32_t playbackPosition(const Music &, int64_t nowUs);
int currentLyric(const Music &, int64_t nowUs, int offsetMs = 0);
const char *condition(int code);
} // namespace ink
