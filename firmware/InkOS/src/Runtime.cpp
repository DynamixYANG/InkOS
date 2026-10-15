#include "Astronomy.h"
#include "Canvas.h"
#include "Energy.h"
#include "Panel.h"
#include "Services.h"
#include "Zones.h"
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
namespace ink {
static SemaphoreHandle_t frameMutex = nullptr;
void selectPage(int p) {
  Guard g(modelMutex);
  model->page = pageAfter(Page::Home, p);
  model->revision++;
  invalidate();
}
void navigatePage(int delta) {
  Guard g(modelMutex);
  model->page = pageAfter(model->page, delta);
  model->revision++;
  invalidate();
}
void networkTask(void *) {
  uint32_t lastAttempt = 0, lastWeather = 0, lastSpotify = 0, lastCalendar = 0;
  bool firstWeather = true, wasConnected = false;
  String activeSsid;
  uint32_t cfgRevision = ~0u;
  Weather *weather = (Weather *)heap_caps_malloc(sizeof(Weather), MALLOC_CAP_SPIRAM);
  if (!weather) {
    Serial.println("INKOS weather allocation failed");
    vTaskDelete(nullptr);
    return;
  }
  new (weather) Weather();
  for (;;) {
    auto cfg = credentials();
    if (cfgRevision != cfg.revision) {
      cfgRevision = cfg.revision;
      firstWeather = true;
      lastCalendar = 0;
      if (cfg.ssid != activeSsid) {
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
void displayTask(void *) {
  uint8_t *frame = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  Model *snap = (Model *)heap_caps_malloc(sizeof(Model), MALLOC_CAP_SPIRAM);
  if (!frame || !snap) {
    Serial.println("INKOS render allocation failed");
    vTaskDelete(nullptr);
    return;
  }
  new (snap) Model();
  if (!frame || !panel.begin()) {
    Serial.println("INKOS display init failed");
    vTaskDelete(nullptr);
    return;
  }
  Canvas c(frame);
  int64_t lastMinute = -1, lastTick = -1;
  int lyric = -99;
  uint32_t rev = ~0u, musicRev = ~0u, weatherRev = ~0u, artRev = ~0u, modelSettingsRevision = ~0u,
           energyRev = ~0u;
  Page lastPage = Page::Forecast;
  bool first = true, lastStale = false;
  for (;;) {
    uint8_t event;
    bool dirty = xQueueReceive(displayEvents, &event, pdMS_TO_TICKS(15)) == pdTRUE;
    timeval wallClock = {};
    gettimeofday(&wallClock, nullptr);
    time_t now = wallClock.tv_sec;
    int64_t us = esp_timer_get_time();
    {
      Guard g(modelMutex);
      memcpy(snap, model, sizeof(Model));
    }
    if (snap->music.valid && snap->music.playing && us - snap->music.sampleUs > 30000000LL)
      snap->music.stale = true;
    if (snap->agenda.configured && snap->agenda.imported && now - snap->agenda.imported > 1800)
      snap->agenda.stale = true;
    int index = currentLyric(snap->music, us);
    int64_t tick = int64_t(wallClock.tv_sec) * 2 + wallClock.tv_usec / 500000;
    bool animationDue =
        snap->page == Page::Home &&
        (snap->settings.animations || (snap->iconTest >= 0 && us < snap->iconTestUntilUs)) &&
        tick != lastTick;
    bool changedPage = snap->page != lastPage;
    bool full = first || changedPage || snap->settings.revision != modelSettingsRevision ||
                (snap->page == Page::Music && artRev != snap->music.artRevision);
    bool draw =
        animationDue || full || dirty || rev != snap->revision || lastMinute != int64_t(now / 60) ||
        (snap->page == Page::Music &&
         (index != lyric || musicRev != snap->music.revision || lastStale != snap->music.stale)) ||
        (snap->page != Page::Music && weatherRev != snap->weather.revision) ||
        ((snap->page == Page::Home || snap->page == Page::Energy) &&
         energyRev != snap->energy.revision);
    if (draw) {
      if (snap->page == Page::Home)
        renderHome(c, *snap, now, unsigned(tick % 8));
      else if (snap->page == Page::Music)
        renderMusic(c, *snap, now, us);
      else if (snap->page == Page::Forecast)
        renderForecast(c, *snap, now);
      else if (snap->page == Page::Week)
        renderWeek(c, *snap, now);
      else if (snap->page == Page::Moon)
        renderMoon(c, *snap, now);
      else if (snap->page == Page::Sun)
        renderSun(c, *snap, now);
      else
        renderEnergy(c, *snap, now);
      {
        Guard g(frameMutex);
        bool animationOnly = snap->page == Page::Home && !full && !dirty && rev == snap->revision &&
                             lastMinute == int64_t(now / 60) &&
                             weatherRev == snap->weather.revision &&
                             energyRev == snap->energy.revision;
        panel.present(frame, full,
                      animationOnly               ? RefreshMode::Animation
                      : snap->page == Page::Music ? RefreshMode::Music
                                                  : RefreshMode::Clock);
      }
      first = false;
      lastPage = snap->page;
      lastMinute = now / 60;
      lastTick = tick;
      modelSettingsRevision = snap->settings.revision;
      lyric = index;
      rev = snap->revision;
      musicRev = snap->music.revision;
      weatherRev = snap->weather.revision;
      energyRev = snap->energy.revision;
      artRev = snap->music.artRevision;
      lastStale = snap->music.stale;
    }
  }
}
static void status() {
  Guard g(modelMutex);
  Serial.printf(
      "{\"inkos\":true,\"version\":\"0.3.1\",\"page\":%u,\"wifi\":%s,\"ip\":\"%s\",\"weather\":%s,"
      "\"hours\":%d,\"spotify\":%s,\"time\":%lld,\"free_heap\":%u,\"psram\":%u}\n",
      unsigned(model->page), model->wifi ? "true" : "false", model->ip,
      model->weather.valid ? "true" : "false", model->weather.hourCount,
      model->music.authorized ? "true" : "false", (long long)time(nullptr), ESP.getFreeHeap(),
      ESP.getFreePsram());
}
static const char *jsonString(cJSON *j, const char *k) {
  auto f = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(f) ? f->valuestring : "";
}
void controlTask(void *) {
  WebServer server(80);
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
        "for navigation. Spotify and energy settings are configured over USB.</p><a "
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
    char b[256];
    snprintf(b, sizeof b,
             "{\"frames\":%lu,\"full_frames\":%lu,\"last_ms\":%lu,\"region\":[%d,%d,%d,%d]}",
             (unsigned long)panel.frames, (unsigned long)panel.fullFrames,
             (unsigned long)panel.lastDuration, panel.lastRegion.x, panel.lastRegion.y,
             panel.lastRegion.w, panel.lastRegion.h);
    server.send(200, "application/json", b);
  });
  server.on("/status", HTTP_GET, [&]() {
    char b[512];
    {
      Guard g(modelMutex);
      snprintf(b, sizeof b,
               "{\"version\":\"0.3.1\",\"page\":%d,\"wifi\":%s,\"weather\":%s,\"spotify\":%s,"
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
  const int pins[] = {2, 3, 5};
  bool down[] = {false, false, false};
  uint32_t changed[3] = {};
  for (int p : pins)
    pinMode(p, INPUT_PULLUP);
  for (;;) {
    server.handleClient();
    for (int i = 0; i < 3; i++) {
      bool pressed = digitalRead(pins[i]) == LOW;
      if (pressed != down[i] && millis() - changed[i] > 70) {
        changed[i] = millis();
        down[i] = pressed;
        if (pressed) {
          if (i == 2)
            selectPage(0);
          else
            navigatePage(i == 0 ? -1 : 1);
          Serial.printf("INKOS button page=%d\n", i);
        }
      }
    }
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
            const char *cmd = jsonString(j, "cmd");
            if (!strcmp(cmd, "icon_test")) {
              auto code = cJSON_GetObjectItem(j, "code");
              int value = cJSON_IsNumber(code) ? code->valueint : -1;
              {
                Guard g(modelMutex);
                model->iconTest = value;
                model->iconTestUntilUs = esp_timer_get_time() + 15000000LL;
                model->revision++;
              }
              invalidate();
              Serial.println("{\"inkos\":true,\"ok\":true}");
            } else if (configurationCommand(cmd, j)) {
            } else if (!strcmp(cmd, "navigate")) {
              const char *action = jsonString(j, "action");
              if (!strcmp(action, "home"))
                selectPage(0);
              else
                navigatePage(!strcmp(action, "prev") ? -1 : 1);
              Serial.println("{\"inkos\":true,\"ok\":true}");
            } else if (!strcmp(cmd, "status"))
              status();
            else if (!strcmp(cmd, "page")) {
              auto p = cJSON_GetObjectItemCaseSensitive(j, "page");
              selectPage(cJSON_IsNumber(p) ? p->valueint : 0);
              Serial.println("{\"inkos\":true,\"ok\":true}");
            } else if (!strcmp(cmd, "spotify")) {
              const char *id = jsonString(j, "client_id"), *token = jsonString(j, "refresh_token");
              if (strlen(id) == 32 && strlen(token) > 10 && strlen(token) < 2048) {
                saveSpotify(id, token);
                Serial.println("{\"inkos\":true,\"saved\":true}");
              } else
                Serial.println("{\"inkos\":true,\"saved\":false}");
            } else if (!strcmp(cmd, "wifi")) {
              saveWifi(jsonString(j, "ssid"), jsonString(j, "password"));
              Serial.println("{\"inkos\":true,\"saved\":true}");
            }
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
  loadAgenda();
  loadEnergy();
  extern bool selfTest();
  if (!selfTest()) {
    Serial.println("INKOS selftest failed; tasks not started");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  xTaskCreatePinnedToCore(displayTask, "display", 12288, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(networkTask, "network", 16384, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(controlTask, "control", 12288, nullptr, 1, nullptr, 1);
  Serial.printf("INKOS boot version=0.3.1 psram=%u bytes model=%u bytes\n", ESP.getPsramSize(),
                unsigned(sizeof(Model)));
}
} // namespace ink
