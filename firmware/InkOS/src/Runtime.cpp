#include "Astronomy.h"
#include "Ota.h"
#include "Storage.h"
#include "CjkAssets.h"
#include "Canvas.h"
#include "CalendarLayout.h"
#include "Energy.h"
#include "Panel.h"
#include "PageNavigation.h"
#include "RenderSchedule.h"
#include "Hardware.h"
#include "Services.h"
#include "LyricsResolver.h"
#include "RemoteConfiguration.h"
#include "Zones.h"
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include <algorithm>
#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
namespace ink {
static SemaphoreHandle_t frameMutex = nullptr;
static std::atomic<uint32_t> navigationDispatchUs{0}, navigationTitleUs{0}, navigationAcceptUs{0}, renderSnapshots{0};
void selectPage(int p) {
  Guard g(modelMutex);
  model->page = pageAfter(Page::Home, p);
  model->navigationRevision++;
  model->navigationRequestedUs = esp_timer_get_time();
  model->revision++;
  invalidate();
}
void navigatePage(int delta, bool fromEncoder) {
  Guard g(modelMutex);
  if (fromEncoder && encoderLocked()) return;
  model->page = pageAfter(model->page, delta);
  model->navigationRevision++;
  model->navigationRequestedUs = esp_timer_get_time();
  model->revision++;
  invalidate();
}
void networkTask(void *) {
  uint32_t lastAttempt = 0, lastWeather = 0, lastSpotify = 0, lastCalendar = 0;
  bool firstWeather = true, wasConnected = false;
  String activeSsid;
  uint32_t cfgRevision = ~0u, wifiRevision = ~0u;
  Weather *weather = (Weather *)heap_caps_malloc(sizeof(Weather), MALLOC_CAP_SPIRAM);
  if (!weather) {
    Serial.println("INKOS weather allocation failed");
    vTaskDelete(nullptr);
    return;
  }
  new (weather) Weather();
  for (;;) {
    otaHeartbeat(1);
    if(otaBusy()){vTaskDelay(pdMS_TO_TICKS(250));continue;}
    auto cfg = credentials();
    if (cfgRevision != cfg.revision && esp_timer_get_time() >= cfg.wifiApplyAfterUs) {
      cfgRevision = cfg.revision;
      firstWeather = true;
      lastCalendar = 0;
      if (cfg.ssid != activeSsid || wifiRevision != cfg.wifiRevision) {
        wifiRevision = cfg.wifiRevision;
        WiFi.disconnect();
        activeSsid = cfg.ssid;
        lastAttempt = 0;
      }
      lastSpotify = 0;
    }
    bool connected = WiFi.status() == WL_CONNECTED;
    if (!connected && (!lastAttempt || millis() - lastAttempt > 20000)) {
      lastAttempt = millis();
      if (cfg.ssid.length()) {
        WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
        Serial.println("INKOS WiFi connecting");
      }
    }
    if (connected != wasConnected) {
      wasConnected = connected;
      {
        Guard g(modelMutex);
        model->wifi = connected;
        model->wifiRssi = connected ? WiFi.RSSI() : -127;
        if (connected)
          strlcpy(model->ip, WiFi.localIP().toString().c_str(), sizeof model->ip);
        model->revision++;
      }
      invalidate();
      if (connected) {
        configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
        Serial.printf("INKOS WiFi online ip=%s\n", WiFi.localIP().toString().c_str());
      }
    }
    if (connected && Clock::valid()) {
      if (firstWeather || millis() - lastWeather > 900000) {
        lastWeather = millis();
        firstWeather = false;
        {
          Guard g(modelMutex);
          memcpy(weather, &model->weather, sizeof(Weather));
        }
        bool success = fetchWeather(*weather);
        weather->revision++;
        {
          Guard g(modelMutex);
          memcpy(&model->weather, weather, sizeof(Weather));
        }
        Serial.printf("INKOS weather valid=%d hours=%d\n", success, weather->hourCount);
        invalidate();
        if (!success)
          lastWeather = millis() - 840000;
      }
      if (!lastCalendar || millis() - lastCalendar >= 900000) {
        lastCalendar = millis();
        updateCalendars();
      }
      serviceLyricProbe();
      updateEnergy();
      Page page;
      {
        Guard g(modelMutex);
        page = model->page;
      }
      uint32_t cadence = page == Page::Music ? 5000 : 30000;
      if (!lastSpotify || millis() - lastSpotify >= cadence) {
        lastSpotify = millis();
        updateSpotify();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
namespace {
struct DisplayState {
  Page page;
  Settings settings;
  RenderToken token;
  uint32_t delayMs, lockMs;
  int64_t requestedUs;
};
DisplayState displayState(const Model &m, time_t now, int64_t us, int64_t tick) {
  DisplayState s;
  s.page=m.page; s.settings=m.settings;
  s.delayMs=m.pageSwitchDelayMs; s.lockMs=m.encoderLockMs; s.requestedUs=m.navigationRequestedUs;
  s.token.navigation=m.navigationRevision; s.token.model=m.revision; s.token.settings=m.settings.revision;
  s.token.weather=m.weather.revision; s.token.music=m.music.revision; s.token.artwork=m.music.artRevision;
  s.token.agenda=m.agenda.revision; s.token.energy=m.energy.revision; s.token.minute=now/60;
  s.token.animation=m.page==Page::Home && (m.settings.animations || (m.iconTest>=0 && us<m.iconTestUntilUs)) ? tick : 0;
  s.token.stale=m.page==Page::Music && (m.music.stale || (m.music.valid && m.music.playing && us-m.music.sampleUs>30000000LL));
  s.token.lyric=m.page==Page::Music ? currentLyric(m.music,s.token.stale?m.music.sampleUs:us) : -1;
  return s;
}
void renderPage(Canvas &c, Model &m, time_t now, int64_t us, int64_t tick) {
  if(m.music.valid && m.music.playing && us-m.music.sampleUs>30000000LL) m.music.stale=true;
  if(m.agenda.configured && m.agenda.imported && now-m.agenda.imported>1800) m.agenda.stale=true;
  switch(m.page) {
    case Page::Home: renderHome(c,m,now,unsigned(tick%8)); break;
    case Page::Music: renderMusic(c,m,now,us); break;
    case Page::Forecast: renderForecast(c,m,now); break;
    case Page::Week: renderWeek(c,m,now); break;
    case Page::Moon: renderMoon(c,m,now); break;
    case Page::Sun: renderSun(c,m,now); break;
    default: renderEnergy(c,m,now); break;
  }
}
}
void displayTask(void *) {
  auto frame=(uint8_t*)heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM);
  auto snap=(Model*)heap_caps_malloc(sizeof(Model),MALLOC_CAP_SPIRAM);
  if(!frame || !snap) {Serial.println("INKOS render allocation failed");vTaskDelete(nullptr);return;}
  new(snap) Model();
  if(!panel.begin()) {Serial.println("INKOS display init failed");vTaskDelete(nullptr);return;}
  Canvas c(frame);
  Page lastPage=Page::Forecast;
  PageNavigation navigation;
  BackgroundSchedule background;
  RenderToken lastToken, preparedToken;
  bool first=true, dirtyPending=true, prepared=false;
  for(;;) {
    if(panel.healthy()&&panel.frames) otaHeartbeat(0);
    if(panel.powerHeld()) {Guard g(frameMutex);panel.idle();}
    uint8_t event;
    // Real input wakes this queue immediately; no fixed debounce before titles.
    // Idle only checks clocks/lyrics at 100ms, without copying the full model.
    uint32_t waitMs=first?0:(navigation.pending()||background.pending()?5:100);
    bool dirty=xQueueReceive(displayEvents,&event,pdMS_TO_TICKS(waitMs))==pdTRUE;
    dirtyPending |= dirty;
    if(dirty) prepared=false;
    timeval wall={};gettimeofday(&wall,nullptr);
    time_t now=wall.tv_sec;int64_t us=esp_timer_get_time();
    int64_t tick=int64_t(wall.tv_sec)*2+wall.tv_usec/500000;
    DisplayState state;
    {Guard g(modelMutex);state=displayState(*model,now,us,tick);}
    if(!first) {
      navigation.observe(int(state.page),state.token.navigation,state.requestedUs);
      if(navigation.pending()) {
        background.clear();
        if(navigation.needsPreview()) {
          prepared=false;
          memcpy(frame,panel.frame(),FRAME_BYTES);
          renderNavigationTitle(c,state.page,state.settings,now);
          {Guard g(modelMutex);if(model->navigationRevision!=state.token.navigation)continue;}
          bool shown;
          {
            Guard g(frameMutex);
            int64_t dispatch=esp_timer_get_time();
            navigationDispatchUs=uint32_t(std::max(int64_t(0),dispatch-state.requestedUs));
            Serial.printf("INKOS navigation dispatch page=%d sequence=%lu age_us=%lu\n",int(state.page),(unsigned long)state.token.navigation,(unsigned long)navigationDispatchUs.load());
            shown=panel.present(frame,false,RefreshMode::Navigation);
          }
          if(shown) {
            int64_t completed=esp_timer_get_time();
            navigation.previewed(int(state.page),completed);
            navigationTitleUs=uint32_t(std::max(int64_t(0),completed-state.requestedUs));
            Serial.printf("INKOS navigation preview page=%d sequence=%lu\n",int(state.page),(unsigned long)state.token.navigation);
          }
          continue; // Take the newest target after an in-flight waveform.
        }
      }
    }
    bool work=first||navigation.pending()||dirtyPending||!(state.token==lastToken);
    if(!work)continue;
    if(!first&&!navigation.pending()) {
      if(dirty||!background.pending())background.request(us);
      if(!background.ready(us))continue;
    }
    // Prepare once during the quiet/visible-title interval. Data or navigation
    // changes invalidate the cache; idle waits never copy the entire 99KB model.
    if(!prepared||!(preparedToken==state.token)) {
      {Guard g(modelMutex);memcpy(snap,model,sizeof(Model));}
      renderSnapshots++;
      if(snap->navigationRevision!=state.token.navigation) {prepared=false;continue;}
      preparedToken=displayState(*snap,now,us,tick).token;
      renderPage(c,*snap,now,us,tick);
      prepared=true;
    }
    if(navigation.pending()&&!navigation.ready(esp_timer_get_time(),state.delayMs))continue;
    bool full=first||navigation.pending()||snap->page!=lastPage||snap->settings.revision!=lastToken.settings||
              (snap->page==Page::Music&&snap->music.artRevision!=lastToken.artwork);
    {
      Guard g(modelMutex);
      if(model->navigationRevision!=snap->navigationRevision) {prepared=false;continue;}
      if(navigation.pending())lockEncoder(state.lockMs);
    }
    if(navigation.pending()) {
      navigationAcceptUs=uint32_t(std::max(int64_t(0),esp_timer_get_time()-state.requestedUs));
      Serial.printf("INKOS navigation accepted page=%d lock_ms=%lu age_us=%lu\n",int(snap->page),(unsigned long)state.lockMs,(unsigned long)navigationAcceptUs.load());
    }
    bool shown;
    {
      Guard g(frameMutex);
      bool animationOnly=snap->page==Page::Home&&!full&&!dirtyPending&&preparedToken.model==lastToken.model&&
          preparedToken.minute==lastToken.minute&&preparedToken.weather==lastToken.weather&&preparedToken.energy==lastToken.energy;
      shown=panel.present(frame,full,animationOnly?RefreshMode::Animation:snap->page==Page::Music?RefreshMode::Music:RefreshMode::Clock,
                          navigation.pending());
    }
    if(!shown) {prepared=false;continue;}
    if(panel.healthy()&&panel.frames)otaHeartbeat(0);
    if(navigation.pending())Serial.printf("INKOS navigation committed page=%d sequence=%lu\n",int(snap->page),(unsigned long)snap->navigationRevision);
    navigation.committed(int(snap->page));
    lastPage=snap->page;lastToken=preparedToken;
    first=false;dirtyPending=false;prepared=false;background.clear();
  }
}
static cJSON *deviceStatus() {
  Guard g(modelMutex); auto out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "inkos", true); cJSON_AddStringToObject(out, "version", "0.7.6");
  cJSON_AddNumberToObject(out, "page", int(model->page)); cJSON_AddBoolToObject(out, "wifi", model->wifi);
  cJSON_AddStringToObject(out, "ip", model->ip); cJSON_AddBoolToObject(out, "spotify", model->music.authorized);
  cJSON_AddBoolToObject(out, "weather", model->weather.valid);
  cJSON_AddNumberToObject(out, "hours", model->weather.hourCount);
  cJSON_AddNumberToObject(out, "time", time(nullptr));
  cJSON_AddNumberToObject(out, "free_heap", ESP.getFreeHeap());
  cJSON_AddNumberToObject(out, "psram", ESP.getFreePsram());
  cJSON_AddBoolToObject(out, "remote_enabled", remoteEnabled());
  cJSON_AddNumberToObject(out, "battery_percent", model->batteryPercent);
  cJSON_AddNumberToObject(out, "battery_voltage", model->batteryVoltage);
  cJSON_AddNumberToObject(out, "wifi_rssi", model->wifiRssi);
  cJSON_AddBoolToObject(out, "encoder_locked", encoderLocked());
  cJSON_AddNumberToObject(out, "navigation_dispatch_us", navigationDispatchUs.load());
  cJSON_AddNumberToObject(out, "navigation_title_us", navigationTitleUs.load());
  cJSON_AddNumberToObject(out, "navigation_accept_us", navigationAcceptUs.load());
  cJSON_AddNumberToObject(out, "render_snapshots", renderSnapshots.load());
  {
    // Diagnostic copy without frameMutex: never block modelMutex holders behind
    // a 4 s full refresh. A concurrent present() can at worst mix two samples.
    PanelPhases p = panel.lastPhases; uint32_t last = panel.lastDuration;
    cJSON_AddNumberToObject(out, "display_last_ms", last);
    auto phase = cJSON_AddObjectToObject(out, "display_phase_us");
    cJSON_AddNumberToObject(phase, "power_on", p.powerOnUs);
    cJSON_AddNumberToObject(phase, "transfer", p.transferUs);
    cJSON_AddNumberToObject(phase, "waveform", p.waveformUs);
    cJSON_AddNumberToObject(phase, "power_off", p.powerOffUs);
    cJSON_AddNumberToObject(phase, "total", p.totalUs);
  }
  return out;
}
static const char *jsonString(cJSON *j, const char *k) {
  auto f = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(f) ? f->valuestring : "";
}
String executeCommand(cJSON *j, bool usb) {
  cJSON *out = nullptr;
  const char *cmd = jsonString(j, "cmd");
  if (!cJSON_IsObject(j)) out = cJSON_CreateObject();
  else if (!strcmp(cmd, "remote_pair")) {
    if (usb) out = remotePairCommand(j);
  } else if (!strcmp(cmd, "status")) out = deviceStatus();
  else if (!strcmp(cmd, "storage_info")) out = storageInfo();
  else if (usb && !strcmp(cmd, "calendar_preview")) {
    auto value = cJSON_GetObjectItem(j, "fixture");
    auto offset = cJSON_GetObjectItem(j, "offset");
    return calendarPreview(cJSON_IsNumber(value) ? value->valueint : 0, cJSON_IsNumber(offset) ? offset->valueint : 0);
  }
  else if (usb && !strcmp(cmd, "lyrics_status")) out = lyricsDiagnostics();
  else if (usb && !strcmp(cmd, "lyrics_probe")) out = lyricProbeCommand(j);
  else if ((out = otaCommand(cmd,j))) {}
  else if (otaBusy()) {out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"saved",false);cJSON_AddStringToObject(out,"message","OTA in progress; configuration is paused");}
  else if (usb && !strcmp(cmd,"storage_check")) {out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"ok",checkStorage());}
  else if ((out = configurationCommand(cmd, j))) {}
  else if (!strcmp(cmd, "spotify")) {
    const char *id = jsonString(j, "client_id"), *token = jsonString(j, "refresh_token");
    bool valid = strlen(id) == 32 && strlen(token) > 10 && strlen(token) < 2048;
    for (const char *p = id; *p; p++) valid &= isxdigit((unsigned char)*p);
    if (valid) saveSpotify(id, token);
    out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "saved", valid);
  } else if (!strcmp(cmd, "navigate") || !strcmp(cmd, "page")) {
    const char *action = jsonString(j, "action");
    if (!strcmp(cmd, "page")) {
      auto p = cJSON_GetObjectItem(j, "page");
      if (cJSON_IsNumber(p) && p->valueint >= 0 && p->valueint < 7) selectPage(p->valueint);
      else return "{\"inkos\":true,\"saved\":false,\"message\":\"Invalid page\"}";
    } else if (!strcmp(action, "home")) selectPage(0);
    else if (!strcmp(action, "prev") || !strcmp(action, "next")) navigatePage(!strcmp(action, "prev") ? -1 : 1);
    else return "{\"inkos\":true,\"saved\":false,\"message\":\"Invalid navigation\"}";
    out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "ok", true);
  } else if (usb && !strcmp(cmd, "icon_test")) {
    auto code = cJSON_GetObjectItem(j, "code"); Guard g(modelMutex);
    model->iconTest = cJSON_IsNumber(code) ? code->valueint : -1;
    model->iconTestUntilUs = esp_timer_get_time() + 15000000LL; model->revision++; invalidate();
    out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "ok", true);
  }
  if (!out) return "{\"inkos\":true,\"saved\":false,\"message\":\"Unsupported command\"}";
  if (!cJSON_HasObjectItem(out, "inkos")) cJSON_AddBoolToObject(out, "inkos", true);
  char *raw = cJSON_PrintUnformatted(out); String response = raw ? raw : "{}";
  free(raw); cJSON_Delete(out); return response;
}
static void status() {
  auto j = cJSON_CreateObject(); cJSON_AddStringToObject(j, "cmd", "status");
  Serial.println(executeCommand(j, true)); cJSON_Delete(j);
}
// Interrupts capture edges; a small dedicated task publishes detents even while
// the web server or USB configuration is blocked. No serial logging on this path.
static void inputTask(void *) {
  const int pins[] = {2, 3, 5};
  bool down[] = {false, false, false};
  uint32_t changed[3] = {};
  for (int p : pins) pinMode(p, INPUT_PULLUP);
  beginHardware();
  for (;;) {
    int steps = takeEncoderMovement();
    if (steps && !otaBusy()) navigatePage(steps, true);
    for (int i = 0; i < 3; i++) {
      bool pressed = digitalRead(pins[i]) == LOW;
      if (pressed != down[i] && millis() - changed[i] > 70) {
        changed[i] = millis();
        down[i] = pressed;
        if (pressed && !otaBusy()) {
          if (i == 2)
            selectPage(0);
          else
            navigatePage(i == 0 ? -1 : 1);
        }
      }
    }
    ulTaskNotifyTake(pdTRUE, std::max(TickType_t(1), pdMS_TO_TICKS(10)));
  }
}
void controlTask(void *) {
  loadRemoteConfiguration();
  WebServer server(80);
  registerRemoteConfiguration(server);
  server.on("/", HTTP_GET, [&]() {
    server.send(
        200, "text/html",
        "<!doctype html><html lang=en><meta name=viewport "
        "content='width=device-width'><title>InkOS</title><style>body{max-width:720px;margin:50px "
        "auto;font:18px "
        "monospace;padding:24px}button{padding:14px;margin:8px}img{width:100%;image-rendering:"
        "pixelated}</style><h1>InkOS</h1><p>Standalone FreeRTOS e-paper display</p><form "
        "action=/page method=post><button name=n value=0>01 Home</button><button name=n value=1>02 "
        "Music</button><button name=n value=2>03 Forecast</button><button name=n value=3>04 "
        "Week</button><button name=n value=4>05 Moon</button><button name=n value=5>06 "
        "Sun</button><button name=n value=6>07 Energy</button></form><form action=/navigate "
        "method=post><button name=action "
        "value=prev>Previous</button><button name=action value=next>Next</button><button "
        "name=action value=home>Home</button></form><p>Use the device buttons "
        "for navigation. Configure using InkOS Studio over USB or paired IP access.</p><a "
        "href=/frame.pbm>Download exact 1-bit framebuffer</a></html>");
  });
  server.on("/page", HTTP_POST, [&]() {
    selectPage(server.arg("n").toInt());
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/navigate", HTTP_POST, [&]() {
    String action = server.arg("action");
    if (action == "home")
      selectPage(0);
    else if (action == "prev")
      navigatePage(-1);
    else if (action == "next")
      navigatePage(1);
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.on("/display", HTTP_GET, [&]() {
    Guard g(frameMutex);
    char b[384];
    auto &p = panel.lastPhases;
    snprintf(b, sizeof b,
             "{\"frames\":%lu,\"full_frames\":%lu,\"last_ms\":%lu,\"region\":[%d,%d,%d,%d],"
             "\"phase_us\":{\"power_on\":%lu,\"transfer\":%lu,\"waveform\":%lu,\"power_off\":%lu,\"total\":%lu}}",
             (unsigned long)panel.frames, (unsigned long)panel.fullFrames,
             (unsigned long)panel.lastDuration, panel.lastRegion.x, panel.lastRegion.y,
             panel.lastRegion.w, panel.lastRegion.h, (unsigned long)p.powerOnUs,
             (unsigned long)p.transferUs, (unsigned long)p.waveformUs,
             (unsigned long)p.powerOffUs, (unsigned long)p.totalUs);
    server.send(200, "application/json", b);
  });
  server.on("/status", HTTP_GET, [&]() {
    char b[512];
    {
      Guard g(modelMutex);
      snprintf(b, sizeof b,
               "{\"version\":\"0.7.6\",\"page\":%d,\"wifi\":%s,\"weather\":%s,\"spotify\":%s,"
               "\"time\":%lld,\"weather_provider\":%d,\"hours\":%d,\"calendar_configured\":%s,"
               "\"calendar_events\":%d,\"calendar_stale\":%s,\"pages\":7,\"energy\":%s,\"energy_"
               "hours\":%d}",
               int(model->page), model->wifi ? "true" : "false",
               model->weather.valid ? "true" : "false", model->music.authorized ? "true" : "false",
               (long long)time(nullptr), model->weather.provider, model->weather.hourCount,
               model->agenda.configured ? "true" : "false", model->agenda.count,
               model->agenda.stale ? "true" : "false",
               priceAt(model->energy, time(nullptr)) ? "true" : "false", model->energy.count);
    }
    server.send(200, "application/json", b);
  });
  server.on("/energy", HTTP_GET, [&]() {
    auto e = (Energy *)heap_caps_malloc(sizeof(Energy), MALLOC_CAP_SPIRAM);
    if (!e) {
      server.send(503);
      return;
    }
    {
      Guard g(modelMutex);
      *e = model->energy;
    }
    auto out = cJSON_CreateObject();
    time_t now = time(nullptr);
    cJSON_AddNumberToObject(out, "hour_utc", now / 3600 * 3600);
    cJSON_AddNumberToObject(out, "fetched", e->fetched);
    cJSON_AddNumberToObject(out, "vat_percent", e->vatPercent);
    cJSON_AddNumberToObject(out, "margin_ct", e->marginCt);
    cJSON_AddNumberToObject(out, "transmission_ct", e->transmissionCt);
    cJSON_AddBoolToObject(out, "fees_include_vat", e->feesIncludeVat);
    cJSON_AddBoolToObject(out, "enabled", e->enabled);
    auto current = priceAt(*e, now);
    cJSON_AddNumberToObject(out, "current_spot_net_ct", current ? current->spotNetCt : NAN);
    cJSON_AddNumberToObject(out, "current_total_ct", totalPriceCt(*e, now));
    cJSON_AddStringToObject(out, "error", e->error);
    auto hours = cJSON_AddArrayToObject(out, "hours");
    for (int i = 0; i < e->count; i++) {
      auto item = cJSON_CreateObject();
      cJSON_AddNumberToObject(item, "start", e->hours[i].start);
      cJSON_AddNumberToObject(item, "spot_net_ct", e->hours[i].spotNetCt);
      cJSON_AddNumberToObject(item, "total_ct", totalPriceCt(*e, e->hours[i].start));
      cJSON_AddItemToArray(hours, item);
    }
    char *raw = cJSON_PrintUnformatted(out);
    server.send(raw ? 200 : 503, "application/json", raw ? raw : "{}");
    free(raw);
    cJSON_Delete(out);
    free(e);
  });
  server.on("/astronomy", HTTP_GET, [&]() {
    Settings s;
    {
      Guard g(modelMutex);
      s = model->settings;
    }
    time_t now = time(nullptr);
    auto local = inZone(now, s.localZone);
    auto sun =
        solarDay(fromZone(Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, 12),
                          s.localZone),
                 s.latitude, s.longitude);
    auto pair = adjacentSolarEvents(now, s.latitude, s.longitude);
    auto moon = moonState(now);
    char b[768];
    snprintf(b, sizeof b,
             "{\"time\":%lld,\"moon_phase\":%.8f,\"illumination\":%.8f,\"sunrise\":%lld,\"sunset\":"
             "%lld,\"dawn\":%lld,\"dusk\":%lld,\"noon\":%lld,\"previous\":%lld,\"previous_rising\":"
             "%s,\"next\":%lld,\"next_rising\":%s,\"polar_day\":%s,\"polar_night\":%s,\"next_new_"
             "moon\":%lld,\"next_full_moon\":%lld}",
             (long long)now, moon.phase, moon.illuminated, (long long)sun.rise, (long long)sun.set,
             (long long)sun.dawn, (long long)sun.dusk, (long long)sun.noon,
             (long long)pair.previous.at, pair.previous.rising ? "true" : "false",
             (long long)pair.next.at, pair.next.rising ? "true" : "false",
             sun.polarDay ? "true" : "false", sun.polarNight ? "true" : "false",
             (long long)moonPhaseEvent(now, 0), (long long)moonPhaseEvent(now, 2));
    server.send(200, "application/json", b);
  });
  server.on("/frame.pbm", HTTP_GET, [&]() {
    if (!panel.frame()) {
      server.send(503);
      return;
    }
    auto copy = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
    if (!copy) {
      server.send(503);
      return;
    }
    {
      Guard g(frameMutex);
      memcpy(copy, panel.frame(), FRAME_BYTES);
    }
    for (int i = 0; i < FRAME_BYTES; i++)
      copy[i] = ~copy[i];
    server.setContentLength(11 + FRAME_BYTES);
    server.send(200, "image/x-portable-bitmap", "");
    auto client = server.client();
    client.write((const uint8_t *)"P4\n800 480\n", 11);
    client.write(copy, FRAME_BYTES);
    free(copy);
  });
  server.begin();
  String input;
  input.reserve(4096);
  bool overflow = false;
  for (;;) {
    otaHeartbeat(2);
    if (!otaBusy()) updateHardwareStatus();
    otaCommand("ota_tick",nullptr);
    server.handleClient();
    while (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n') {
        if (overflow) {
          overflow = false;
          input = "";
          Serial.println(
              "{\"inkos\":true,\"saved\":false,\"message\":\"Command exceeds 4095 bytes\"}");
          continue;
        }
        if (input.startsWith("{")) {
          cJSON *j = cJSON_Parse(input.c_str());
          if (j) {
            Serial.println(executeCommand(j, true));
            cJSON_Delete(j);
          } else
            Serial.println("{\"inkos\":true,\"saved\":false,\"message\":\"Invalid JSON command\"}");
        } else if (input == "?")
          status();
        else if (input.length() == 1 && input[0] >= '1' && input[0] <= '7')
          selectPage(input[0] - '1');
        else if (input == "<")
          navigatePage(-1);
        else if (input == ">")
          navigatePage(1);
        else if (input == "h")
          selectPage(0);
        input = "";
      } else if (ch != '\r') {
        if (overflow)
          continue;
        if (input.length() < 4095)
          input += ch;
        else {
          input = "";
          overflow = true;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void start() {
  Serial.setRxBufferSize(8192);
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(1200);
  startOtaSupervisor();
  if(!beginCjkAssets()){Serial.println("INKOS font archive verification failed; USB recovery required");return;}
  setenv("TZ", "UTC0", 1);
  tzset();
  frameMutex = xSemaphoreCreateMutex();
  modelMutex = xSemaphoreCreateMutex();
  displayEvents = xQueueCreate(1, sizeof(uint8_t));
  model = (Model *)heap_caps_malloc(sizeof(Model), MALLOC_CAP_SPIRAM);
  if (!model) {
    Serial.println("INKOS missing PSRAM");
    return;
  }
  new (model) Model();
  cJSON_Hooks hooks = {};
  hooks.malloc_fn = [](size_t n) -> void * {
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  };
  hooks.free_fn = free;
  cJSON_InitHooks(&hooks);
  loadConfiguration();
  ensureStorage();
  loadAgenda();
  loadEnergy();
  extern bool selfTest();
  if (!selfTest()) {
    Serial.println("INKOS selftest failed; tasks not started");
    return;
  }
  otaStartupPassed();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  xTaskCreatePinnedToCore(displayTask, "display", 12288, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(networkTask, "network", 16384, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(inputTask, "input", 3072, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(controlTask, "control", 12288, nullptr, 1, nullptr, 1);
  Serial.printf("INKOS boot version=0.7.6 psram=%u bytes model=%u bytes\n", ESP.getPsramSize(),
                unsigned(sizeof(Model)));
}
} // namespace ink
