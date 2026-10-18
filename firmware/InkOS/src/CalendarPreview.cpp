#include "CalendarLayout.h"
#include "Canvas.h"
#include "Zones.h"
#include <cJSON.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <new>
#include <vector>
#include <cmath>
namespace ink {
static String renderPreview(int fixture) {
  if (fixture < 0 || fixture > 13) return "";
  auto m = (Model *)heap_caps_malloc(sizeof(Model), MALLOC_CAP_SPIRAM);
  auto pbm = (uint8_t *)heap_caps_malloc(FRAME_BYTES + 11, MALLOC_CAP_SPIRAM);
  if (!m || !pbm) { free(m); free(pbm); return ""; }
  new (m) Model(); m->wifi = true; m->wifiRssi=-65; m->batteryPercent=73; m->agenda.configured = true;
  m->weather.valid = true; m->weather.temp = 10.3; m->weather.feels = 7; m->weather.wind = 4;
  m->weather.code = 3;
  time_t monday = Clock::epoch(2026, 10, 12), now = fromZone(Clock::epoch(2026, 10, 15, 10, 30), "Europe/Helsinki");
  auto add = [&](int day, int start, int end, const char *title, const char *place = "", bool allDay = false) {
    auto &e = m->agenda.events[m->agenda.count++];
    e.start = fromZone(monday + day * 86400 + start * 60, m->settings.localZone);
    e.end = fromZone(monday + day * 86400 + end * 60, m->settings.localZone);
    e.allDay = allDay; strlcpy(e.title, title, sizeof e.title); strlcpy(e.location, place, sizeof e.location);
  };
  add(0, 0, 3 * 1440, "Release week", "", true);
  add(4, 0, 1440, "Out of office", "", true);
  add(0, 9 * 60, 10 * 60 + 30, "Design review", "Studio");
  add(0, 13 * 60, 16 * 60, "Project planning", "Room A");
  add(1, 10 * 60, 12 * 60, "Research", "Library");
  add(1, 13 * 60, 15 * 60, "Focus time");
  add(2, 9 * 60, 11 * 60, "Workshop", "Room B");
  add(2, 10 * 60, 12 * 60, "Team call", "Online");
  add(3, 9 * 60, 12 * 60, "Build session", "Lab");
  add(3, 14 * 60, 15 * 60, "Walk", "Park");
  add(5, 11 * 60, 13 * 60, "Market", "Oulu");
  add(6, 15 * 60, 17 * 60, "Family time");
  add(7, 9*60, 11*60, "Next week planning", "Office");
  add(8, 13*60, 15*60, "Focus session");
  add(9, 10*60, 12*60, "Project review");
  if (fixture == 2) {
    add(0, 9 * 60 + 15, 11 * 60, "Parallel B"); add(0, 9 * 60 + 30, 10 * 60, "Parallel C");
    add(0, 9 * 60 + 40, 10 * 60 + 5, "Parallel D");
    add(0, 0, 1440, "All-day B", "", true); add(0, 0, 1440, "All-day C", "", true);
    add(4, 30, 60, "Night shift"); add(5, 23 * 60 + 40, 1440 + 45, "Overnight");
    add(6, 17 * 60, 17 * 60 + 5, "Short"); add(6, 17 * 60 + 5, 17 * 60 + 10, "Next");
  }
  if (fixture == 3 || fixture == 5) {
    new (&m->agenda) Agenda();
    now = fromZone(fixture == 3 ? Clock::epoch(2021, 1, 1, 10, 30) : Clock::epoch(2026, 8, 15, 10, 30), "Europe/Helsinki");
  }
  if (fixture == 4) {
    new (&m->agenda) Agenda(); m->agenda.configured = true;
    monday = Clock::epoch(2026, 10, 19);
    now = Clock::epoch(2026, 10, 25, 0, 30);
    add(5, 23 * 60, 25 * 60, "Across midnight");
    auto &e = m->agenda.events[m->agenda.count++];
    e.start = Clock::epoch(2026, 10, 25, 0, 50); e.end = Clock::epoch(2026, 10, 25, 1, 10);
    strlcpy(e.title, "DST return", sizeof e.title);
  }
  if (fixture == 6 || fixture == 8) {
    new (&m->agenda) Agenda(); m->agenda.configured = true;
    add(0, 0, 3 * 1440, "项目发布周 · Release", "", true);
    add(4, 0, 1440, "休假 / 休暇", "", true);
    add(0, 9 * 60, 11 * 60, "设计评审", "会议室 A");
    add(1, 10 * 60, 13 * 60, "研究与阅读", "图书馆");
    add(2, 9 * 60, 11 * 60, "产品讨论", "线上");
    add(2, 10 * 60, 12 * 60, "团队同步会", "会议室 B");
    add(3, 10 * 60, 13 * 60, "中文日程测试 / Calendar", "实验室");
    add(5, 11 * 60, 13 * 60, "市集 · Oulu", "市中心");
    add(6, 15 * 60, 17 * 60, "家庭時光，週末活動", "公園");
  }
  if (fixture == 7) {
    auto &s = m->music; s.authorized = s.valid = s.playing = true;
    s.duration = 240000; s.position = 60000; s.sampleUs = 1000000;
    strlcpy(s.title, "中文歌曲 · 繁體音樂 / Mixed title", sizeof s.title);
    strlcpy(s.artist, "测试歌手 / 測試歌手 · InkOS", sizeof s.artist);
    s.lyricStatus = LyricStatus::Timed; s.lineCount = 3;
    s.lines[0].atMs = 0; s.lines[1].atMs = 60000; s.lines[2].atMs = 120000;
    strlcpy(s.lines[0].text, "前一句歌词 · Previous line", sizeof s.lines[0].text);
    strlcpy(s.lines[1].text, "月光落在窗前，听见远方的声音；這一行測試繁體與中文換行。", sizeof s.lines[1].text);
    strlcpy(s.lines[2].text, "下一句歌詞 · Next line 123", sizeof s.lines[2].text);
  }
  if (fixture==9) {
    auto &w=m->weather;w.fetched=now;w.hourCount=168;
    auto first=agendaFirstDay(now,m->settings.localZone);
    const int codes[]={0,2,3,61,65,75,95};
    for(int d=0;d<7;d++) { auto &day=w.days[d];day.epoch=fromZone(first+d*86400,m->settings.localZone);day.high=8+d;day.low=1+d;day.rain=d*.8;day.probability=d*15;day.wind=3+d;day.code=codes[d]; }
    for(int h=0;h<168;h++) {auto &hour=w.hours[h];hour.epoch=fromZone(first+h*3600,m->settings.localZone);hour.temp=4+h/24+3*sinf(h*3.14159265f/12);hour.rain=h%8==0?.4f:0;hour.day=h%24>=8&&h%24<=18;}
  }
  if(fixture==12) {
    auto &e=m->energy;e.valid=true;e.count=72;e.fetched=now;
    for(int h=0;h<72;h++) {e.hours[h].start=now/3600*3600+(h-24)*3600;e.hours[h].spotNetCt=5+9*sinf(h*.31f);}
  }
  if(fixture==13) {now=fromZone(Clock::epoch(2026,12,31,10,30),m->settings.localZone);monday=Clock::epoch(2026,12,31);new(&m->agenda)Agenda();m->agenda.configured=true;for(int d=0;d<7;d++)add(d,9*60,11*60,"Year boundary event");}
  if (fixture == 0 || fixture == 3 || fixture == 5 || fixture == 8) {
    // Synthetic 7-day outlook for Home (0.7.9): today + next days, including a sub-zero day.
    auto today = inZone(now, m->settings.localZone);
    time_t first = Clock::epoch(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
    const int codes[] = {3, 61, 2, 71, 0, 80, 95};
    const float highs[] = {11, 12, 9, -2, 7, 8, 10}, lows[] = {4, 5, 1, -11, 0, 2, 3};
    for (int d = 0; d < 7; d++) {
      auto &day = m->weather.days[d];
      day.epoch = fromZone(first + d * 86400, m->settings.localZone);
      day.high = highs[d]; day.low = lows[d]; day.code = codes[d];
    }
    // Synthetic Kp forecast (0.7.10): 3-hour bins from yesterday, fetched just now.
    const float kp[] = {1.67f, 2.33f, 3, 4.33f, 3.67f, 2.67f, 2, 1.33f};
    time_t bin = now / KpBinSeconds * KpBinSeconds - 86400;
    m->aurora.count = 32;
    for (int i = 0; i < 32; i++) m->aurora.bins[i] = {bin + i * KpBinSeconds, kp[i % 8], KpKind::Predicted};
    m->aurora.fetched = now;
  }
  memcpy(pbm, "P4\n800 480\n", 11); Canvas canvas(pbm + 11);
  if (fixture == 0 || fixture == 3 || fixture == 5 || fixture == 8) renderHome(canvas, *m, now);
  else if (fixture == 9) renderForecast(canvas,*m,now);
  else if (fixture == 10) renderMoon(canvas,*m,now);
  else if (fixture == 11) renderSun(canvas,*m,now);
  else if (fixture == 12) renderEnergy(canvas,*m,now);
  else if (fixture == 7) renderMusic(canvas, *m, now, 1000000);
  else renderWeek(canvas, *m, now);
  for (int i = 11; i < FRAME_BYTES + 11; i++) pbm[i] = ~pbm[i];
  std::vector<uint8_t> encoded((FRAME_BYTES + 13) / 3 * 4 + 1); size_t length = 0;
  int rc = mbedtls_base64_encode(encoded.data(), encoded.size(), &length, pbm, FRAME_BYTES + 11);
  String result = !rc ? String((const char *)encoded.data(), length) : String();
  free(pbm); free(m); return result;
}
String calendarPreview(int fixture, int offset) {
  static String encoded;
  static int cachedFixture = -1;
  if (!offset) { encoded = renderPreview(fixture); cachedFixture = fixture; }
  auto out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "inkos", true);
  if (fixture == cachedFixture && encoded.length() && offset >= 0 && unsigned(offset) < encoded.length()) {
    cJSON_AddNumberToObject(out, "offset", offset);
    cJSON_AddNumberToObject(out, "total", encoded.length());
    cJSON_AddStringToObject(out, "preview_chunk", encoded.substring(offset, offset + 1024).c_str());
  } else cJSON_AddBoolToObject(out, "saved", false);
  char *raw = cJSON_PrintUnformatted(out); String response = raw ? raw : "{}";
  free(raw); cJSON_Delete(out); return response;
}
}
