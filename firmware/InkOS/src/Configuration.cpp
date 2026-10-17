#include "Services.h"
#include "Zones.h"
#include "CalDav.h"
#include "Panel.h"
#include "Log.h"
#include "LogStore.h"
#include <Preferences.h>
#include <esp_timer.h>
namespace ink {
static SemaphoreHandle_t configMutex;
static Credentials config;
static const char *stringAt(cJSON *j, const char *k, const char *fallback = "") {
  auto v = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(v) ? v->valuestring : fallback;
}
static bool writeSettings(const Settings &s) {
  Preferences n;
  if (!n.begin("inkos", false))
    return false;
  size_t written = n.putBytes("settings2", &s, sizeof s);
  n.end();
  return written == sizeof s;
}
static void applyDisplayTuning(const Credentials &c);
void loadConfiguration() {
  configMutex = xSemaphoreCreateMutex();
  Preferences n;
  n.begin("wifi", true);
  config.ssid = n.getString("ssid", "");
  config.password = n.getString("pass", "");
  auto wifi = cJSON_Parse(n.getString("network1", "").c_str());
  if (wifi) {
    config.ssid = stringAt(wifi, "ssid"); config.password = stringAt(wifi, "password");
    cJSON_Delete(wifi);
  }
  n.end();
  n.begin("inkos", true);
  config.clientId = n.getString("client_id", "");
  config.refreshToken = n.getString("refresh", "");
  auto icloud = cJSON_Parse(n.getString("icloud1", "").c_str());
  if (icloud) {
    config.icloud.enabled = cJSON_IsTrue(cJSON_GetObjectItem(icloud,"enabled"));
    config.icloud.username = stringAt(icloud,"username");config.icloud.password = stringAt(icloud,"password");
    config.icloud.calendarUrl = stringAt(icloud,"calendar_url");config.icloud.calendarName = stringAt(icloud,"calendar_name");
    if (!validICloudUrl(config.icloud.calendarUrl) || config.icloud.password.isEmpty()) config.icloud.enabled=false;
    cJSON_Delete(icloud);
  }
  config.pageSwitchDelayMs = n.getUInt("page_delay", 500);
  if (config.pageSwitchDelayMs < 100 || config.pageSwitchDelayMs > 5000)
    config.pageSwitchDelayMs = 500;
  config.encoderLockMs = n.getUInt("encoder_lock", 2000);
  if (config.encoderLockMs > 10000) config.encoderLockMs = 2000;
  config.navHoldPower = n.getBool("nav_hold", true);
  config.navFastFull = n.getBool("nav_fast", true);
  config.loggingEnabled = n.getBool("log_on", true);
  setLoggingEnabled(config.loggingEnabled);
  config.logRetentionDays = n.getUShort("log_ret", LogRetentionDefaultDays);
  if (!validRetentionDays(config.logRetentionDays)) config.logRetentionDays = LogRetentionDefaultDays;
  setLogRetentionDays(config.logRetentionDays);
  config.navIdleOffMs = n.getUInt("nav_idle", 3000);
  if (config.navIdleOffMs < 500 || config.navIdleOffMs > 10000) config.navIdleOffMs = 3000;
  if (n.getBytesLength("settings2") == sizeof(Settings))
    n.getBytes("settings2", &config.settings, sizeof(Settings));
  auto energy = cJSON_Parse(n.getString("energy1", "").c_str());
  if (energy) {
    auto &e = config.energy;
    e.enabled = cJSON_IsTrue(cJSON_GetObjectItem(energy, "enabled"));
    auto margin = cJSON_GetObjectItem(energy, "margin_ct");
    if (cJSON_IsNumber(margin) && isfinite(margin->valuedouble) && margin->valuedouble >= -100 &&
        margin->valuedouble <= 1000)
      e.marginCt = margin->valuedouble;
    auto provider = cJSON_GetObjectItem(energy, "provider");
    e.provider = cJSON_IsNumber(provider) && provider->valueint >= 0 && provider->valueint <= 2
                     ? provider->valueint
                     : 0;
    if (e.provider == 1)
      e.endpoint = "https://api.porssisahko.net/v2/latest-prices.json";
    else if (e.provider == 2)
      e.endpoint = stringAt(energy, "endpoint", e.endpoint.c_str());
    auto vat = cJSON_GetObjectItem(energy, "vat_percent"),
         transmission = cJSON_GetObjectItem(energy, "transmission_ct");
    if (cJSON_IsNumber(vat) && isfinite(vat->valuedouble) && vat->valuedouble >= 0 &&
        vat->valuedouble <= 100)
      e.vatPercent = vat->valuedouble;
    if (cJSON_IsNumber(transmission) && isfinite(transmission->valuedouble) &&
        transmission->valuedouble >= 0 && transmission->valuedouble <= 1000)
      e.transmissionCt = transmission->valuedouble;
    e.feesIncludeVat = cJSON_IsTrue(cJSON_GetObjectItem(energy, "fees_include_vat"));
    e.apiHeader = stringAt(energy, "api_header", "Authorization");
    e.apiKey = stringAt(energy, "api_key");
    cJSON_Delete(energy);
  }
  for (int i = 0; i < 3; i++) {
    char key[16];
    snprintf(key, sizeof key, "feed%d", i);
    String raw = n.getString(key, "");
    auto j = cJSON_Parse(raw.c_str());
    if (!j)
      continue;
    config.feeds[i].name = stringAt(j, "name");
    config.feeds[i].provider = stringAt(j, "provider");
    config.feeds[i].url = stringAt(j, "url");
    config.feeds[i].enabled = cJSON_IsTrue(cJSON_GetObjectItem(j, "enabled"));
    cJSON_Delete(j);
  }
  n.end();
  model->settings = config.settings;
  model->pageSwitchDelayMs = config.pageSwitchDelayMs;
  model->encoderLockMs = config.encoderLockMs;
  applyDisplayTuning(config);
  model->energy.enabled = config.energy.enabled;
  model->energy.marginCt = config.energy.marginCt;
  model->energy.vatPercent = config.energy.vatPercent;
  model->energy.transmissionCt = config.energy.transmissionCt;
  model->energy.feesIncludeVat = config.energy.feesIncludeVat;
}
Credentials credentials() {
  Guard g(configMutex);
  return config;
}
bool saveWifi(const String &s, const String &p) {
  Guard g(configMutex);
  Preferences n;
  if (!n.begin("wifi", false)) return false;
  // One NVS blob avoids a partially updated SSID/password pair on power loss.
  auto j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "ssid", s.c_str()); cJSON_AddStringToObject(j, "password", p.c_str());
  char *raw = cJSON_PrintUnformatted(j);
  bool saved = raw && n.putString("network1", raw) > 0;
  free(raw); cJSON_Delete(j); n.end();
  if (!saved) return false;
  config.ssid = s; config.password = p; config.wifiRevision++;
  config.wifiApplyAfterUs = esp_timer_get_time() + 1500000;
  config.revision++;
  return true;
}
void saveSpotify(const String &id, const String &token) {
  Guard g(configMutex);
  config.clientId = id;
  config.refreshToken = token;
  config.authRevision++;
  Preferences n;
  n.begin("inkos", false);
  n.putString("client_id", id);
  n.putString("refresh", token);
  n.end();
  config.revision++;
}
void rotateToken(const String &t) {
  Guard g(configMutex);
  config.refreshToken = t;
  Preferences n;
  n.begin("inkos", false);
  n.putString("refresh", t);
  n.end();
}
static void applyDisplayTuning(const Credentials &c) {
  panel.holdNavigationPower = c.navHoldPower;
  panel.holdIdleMs = c.navIdleOffMs;
  panel.fastNavigationFull = c.navFastFull;
}
static cJSON *displayTuning(const Credentials &c) {
  auto out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "inkos", true);
  cJSON_AddBoolToObject(out, "hold_power", c.navHoldPower);
  cJSON_AddNumberToObject(out, "idle_off_ms", c.navIdleOffMs);
  cJSON_AddBoolToObject(out, "fast_full", c.navFastFull);
  cJSON_AddBoolToObject(out, "panel_held", panel.powerHeld());
  return out;
}
static cJSON *ack(bool ok, const char *message = "") {
  auto j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "inkos", true);
  cJSON_AddBoolToObject(j, "saved", ok);
  if (*message) cJSON_AddStringToObject(j, "message", message);
  return j;
}
cJSON *configurationCommand(const char *cmd, cJSON *j) {
  if (!strcmp(cmd, "logging_set")) { // Persist + apply without a settings revision/full refresh.
    auto on = cJSON_GetObjectItem(j, "enabled"), days = cJSON_GetObjectItem(j, "retention_days");
    if (!on && !days) return ack(false, "Give enabled and/or retention_days");
    if (on && !cJSON_IsBool(on)) return ack(false, "enabled must be true or false");
    if (days && (!cJSON_IsNumber(days) || !validRetentionDays(days->valueint) || days->valuedouble != days->valueint))
      return ack(false, "retention_days must be 7, 21, 35, 90 or 180");
    Credentials current = credentials();
    bool enabled = on ? cJSON_IsTrue(on) : current.loggingEnabled;
    uint16_t retention = days ? uint16_t(days->valueint) : current.logRetentionDays;
    {
      Guard g(configMutex);
      Preferences n;
      if (!n.begin("inkos", false)) return ack(false, "Could not save logging setting");
      bool saved = (!on || n.putBool("log_on", enabled) == 1) &&
                   (!days || n.putUShort("log_ret", retention) == sizeof(uint16_t));
      n.end();
      if (!saved) return ack(false, "Could not save logging setting");
      config.loggingEnabled = enabled;
      config.logRetentionDays = retention;
    }
    if (days) setLogRetentionDays(retention);
    if (on) applyLoggingEnabled(enabled);
    auto out = ack(true, "Logging settings saved");
    cJSON_AddBoolToObject(out, "logging_enabled", loggingEnabled());
    cJSON_AddNumberToObject(out, "log_retention_days", logRetentionDays());
    return out;
  }
  if (!strcmp(cmd, "logging_get")) {
    auto out = ack(true, "");
    cJSON_AddBoolToObject(out, "logging_enabled", loggingEnabled());
    cJSON_AddNumberToObject(out, "log_retention_days", logRetentionDays());
    return out;
  }
  if (!strcmp(cmd, "display_tuning_get")) return displayTuning(credentials());
  if (!strcmp(cmd, "display_tuning_set")) {
    Credentials next = credentials();
    auto hold = cJSON_GetObjectItem(j, "hold_power"), idle = cJSON_GetObjectItem(j, "idle_off_ms"),
         fast = cJSON_GetObjectItem(j, "fast_full");
    if (fast) {
      if (!cJSON_IsBool(fast)) return ack(false, "fast_full must be true or false");
      next.navFastFull = cJSON_IsTrue(fast);
    }
    if (hold) {
      if (!cJSON_IsBool(hold)) return ack(false, "hold_power must be true or false");
      next.navHoldPower = cJSON_IsTrue(hold);
    }
    if (idle) {
      if (!cJSON_IsNumber(idle) || !isfinite(idle->valuedouble) || idle->valuedouble < 500 ||
          idle->valuedouble > 10000 || idle->valuedouble != idle->valueint)
        return ack(false, "idle_off_ms must be 500-10000 integer milliseconds");
      next.navIdleOffMs = idle->valueint;
    }
    {
      Guard g(configMutex);
      Preferences n;
      if (!n.begin("inkos", false)) return ack(false, "Could not save display tuning");
      bool saved = n.putBool("nav_hold", next.navHoldPower) == 1 &&
                   n.putUInt("nav_idle", next.navIdleOffMs) == sizeof(uint32_t) &&
                   n.putBool("nav_fast", next.navFastFull) == 1;
      n.end();
      if (!saved) return ack(false, "Could not save display tuning");
      config.navHoldPower = next.navHoldPower;
      config.navIdleOffMs = next.navIdleOffMs;
      config.navFastFull = next.navFastFull;
      applyDisplayTuning(config);
    }
    auto out = displayTuning(credentials());
    cJSON_AddBoolToObject(out, "saved", true);
    return out;
  }
  if (!strcmp(cmd,"icloud_get")) {
    auto v=credentials().icloud;auto out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"inkos",true);
    cJSON_AddBoolToObject(out,"enabled",v.enabled);cJSON_AddBoolToObject(out,"has_password",v.password.length()>0);
    cJSON_AddStringToObject(out,"username",v.username.c_str());cJSON_AddStringToObject(out,"calendar_url",v.calendarUrl.c_str());
    cJSON_AddStringToObject(out,"calendar_name",v.calendarName.c_str());return out;
  }
  if (!strcmp(cmd,"icloud_set") || !strcmp(cmd,"icloud_disconnect")) {
    auto current=credentials().icloud;ICloudConfig v=current;
    bool remove=!strcmp(cmd,"icloud_disconnect");
    if(remove)v=ICloudConfig();
    else {
      v.enabled=cJSON_IsTrue(cJSON_GetObjectItem(j,"enabled"));
      v.username=stringAt(j,"username");v.calendarUrl=stringAt(j,"calendar_url");v.calendarName=stringAt(j,"calendar_name");
      auto field=cJSON_GetObjectItem(j,"password_mode");int mode=cJSON_IsNumber(field)?field->valueint:0;
      if(mode==1)v.password=stringAt(j,"password");
      if(mode<0||mode>1||(mode==0&&v.username!=current.username)||v.username.length()>254||
         v.username.indexOf(':')>=0||v.username.indexOf('\r')>=0||v.username.indexOf('\n')>=0||
         v.password.length()>128||v.password.indexOf('\r')>=0||v.password.indexOf('\n')>=0||
         v.calendarName.length()>127||(!v.calendarUrl.isEmpty()&&!validICloudUrl(v.calendarUrl))||
         (v.enabled&&(v.username.isEmpty()||v.password.length()<8||v.calendarUrl.isEmpty())))
        return ack(false,"Select an iCloud calendar and use an Apple app-specific password");
    }
    auto data=cJSON_CreateObject();cJSON_AddBoolToObject(data,"enabled",v.enabled);
    cJSON_AddStringToObject(data,"username",v.username.c_str());cJSON_AddStringToObject(data,"password",v.password.c_str());
    cJSON_AddStringToObject(data,"calendar_url",v.calendarUrl.c_str());cJSON_AddStringToObject(data,"calendar_name",v.calendarName.c_str());
    char *raw=cJSON_PrintUnformatted(data);bool saved=false;
    if(raw){Guard g(configMutex);Preferences n;if(n.begin("inkos",false)){saved=n.putString("icloud1",raw)>0;n.end();}
      if(saved){config.icloud=v;config.revision++;}}
    if(raw){memset(raw,0,strlen(raw));free(raw);}cJSON_Delete(data);
    if(saved && (v.enabled!=current.enabled || v.username!=current.username || v.calendarUrl!=current.calendarUrl)) {
      // Never display events from a previously selected private calendar as the new one.
      auto cfg=credentials();Guard g(modelMutex);
      uint32_t revision=model->agenda.revision+1;memset(&model->agenda,0,sizeof model->agenda);model->agenda.revision=revision;
      model->agenda.configured=cfg.icloud.enabled;
      for(auto &feed:cfg.feeds)model->agenda.configured|=feed.enabled;
      model->revision++;invalidate();
    }
    return ack(saved,saved?"":"Could not save iCloud configuration");
  }
  if (!strcmp(cmd, "wifi_get")) {
    auto cfg = credentials(); auto out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "inkos", true);
    cJSON_AddStringToObject(out, "ssid", cfg.ssid.c_str());
    cJSON_AddBoolToObject(out, "has_password", cfg.password.length() > 0);
    return out;
  }
  if (!strcmp(cmd, "wifi")) {
    auto cfg = credentials();
    String ssid = stringAt(j, "ssid"), password = stringAt(j, "password");
    auto modeField = cJSON_GetObjectItem(j, "password_mode");
    int mode = cJSON_IsNumber(modeField) ? modeField->valueint : 1;
    if (mode == 0) password = cfg.password;
    if (mode == 2) password = "";
    if (ssid.isEmpty() || ssid.length() > 32 || mode < 0 || mode > 2 ||
        (mode == 1 && (password.length() < 8 || password.length() > 63)) ||
        (mode == 0 && (ssid != cfg.ssid || password.isEmpty())))
      return ack(false, "Use a 1-32 byte SSID and 8-63 byte password, or select an open network");
    return ack(saveWifi(ssid, password), "Wi-Fi saved; reconnect using the new IP if needed");
  }
  if (!strcmp(cmd, "energy_set")) {
    auto v = credentials().energy;
    auto s = cJSON_GetObjectItem(j, "energy");
    auto margin = cJSON_GetObjectItem(s, "margin_ct"),
         provider = cJSON_GetObjectItem(s, "provider");
    auto vat = cJSON_GetObjectItem(s, "vat_percent"),
         transmission = cJSON_GetObjectItem(s, "transmission_ct");
    if (!cJSON_IsObject(s) || !cJSON_IsNumber(margin) || !isfinite(margin->valuedouble) ||
        margin->valuedouble < -100 || margin->valuedouble > 1000 || !cJSON_IsNumber(provider) ||
        provider->valueint < 0 || provider->valueint > 2 || !cJSON_IsNumber(vat) ||
        !isfinite(vat->valuedouble) || vat->valuedouble < 0 || vat->valuedouble > 100 ||
        !cJSON_IsNumber(transmission) || !isfinite(transmission->valuedouble) ||
        transmission->valuedouble < 0 || transmission->valuedouble > 1000) {
      return ack(false, "Invalid provider, VAT, margin or transmission");
    }
    v.enabled = cJSON_IsTrue(cJSON_GetObjectItem(s, "enabled"));
    v.marginCt = margin->valuedouble;
    v.provider = provider->valueint;
    v.vatPercent = vat->valuedouble;
    v.transmissionCt = transmission->valuedouble;
    v.feesIncludeVat = cJSON_IsTrue(cJSON_GetObjectItem(s, "fees_include_vat"));
    v.endpoint = v.provider == 0   ? "https://sahkotin.fi/prices"
                 : v.provider == 1 ? "https://api.porssisahko.net/v2/latest-prices.json"
                                   : stringAt(s, "endpoint");
    v.apiHeader = stringAt(s, "api_header", "Authorization");
    auto mode = cJSON_GetObjectItem(s, "key_mode");
    int keyMode = cJSON_IsNumber(mode) ? mode->valueint : 0;
    if (keyMode == 1)
      v.apiKey = stringAt(s, "api_key");
    else if (keyMode == 2)
      v.apiKey = "";
    bool badHeader = v.apiHeader != "Authorization" && v.apiHeader != "X-API-Key";
    if (!v.endpoint.startsWith("https://") || v.endpoint.length() < 9 ||
        v.endpoint.length() > 512 || v.endpoint.indexOf('\r') >= 0 ||
        v.endpoint.indexOf('\n') >= 0 || badHeader || v.apiKey.length() > 256 ||
        v.apiKey.indexOf('\r') >= 0 || v.apiKey.indexOf('\n') >= 0 || keyMode < 0 || keyMode > 2) {
      return ack(false, "Use an HTTPS endpoint and a valid API header/value");
    }
    auto out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "enabled", v.enabled);
    cJSON_AddNumberToObject(out, "provider", v.provider);
    cJSON_AddNumberToObject(out, "margin_ct", v.marginCt);
    cJSON_AddNumberToObject(out, "vat_percent", v.vatPercent);
    cJSON_AddNumberToObject(out, "transmission_ct", v.transmissionCt);
    cJSON_AddBoolToObject(out, "fees_include_vat", v.feesIncludeVat);
    cJSON_AddStringToObject(out, "endpoint", v.endpoint.c_str());
    cJSON_AddStringToObject(out, "api_header", v.apiHeader.c_str());
    cJSON_AddStringToObject(out, "api_key", v.apiKey.c_str());
    char *raw = cJSON_PrintUnformatted(out);
    bool saved = false, sourceChanged = false;
    if (raw) {
      Guard g(configMutex);
      Preferences n;
      if (n.begin("inkos", false)) {
        saved = n.putString("energy1", raw) > 0;
        n.end();
      }
      if (saved) {
        sourceChanged =
            config.energy.provider != v.provider || config.energy.endpoint != v.endpoint;
        config.energy = v;
        config.energyRevision++;
      }
    }
    free(raw);
    cJSON_Delete(out);
    if (saved) {
      Guard g(modelMutex);
      auto &e = model->energy;
      e.enabled = v.enabled;
      e.marginCt = v.marginCt;
      e.vatPercent = v.vatPercent;
      e.transmissionCt = v.transmissionCt;
      e.feesIncludeVat = v.feesIncludeVat;
      if (sourceChanged) {
        e.valid = false;
        e.count = 0;
        e.fetched = 0;
      }
      e.revision++;
      model->revision++;
    }
    if (saved)
      invalidate();
    return ack(saved, saved ? "" : "Could not save energy settings");
  }
  if (!strcmp(cmd, "spotify_disconnect")) {
    {
      Guard g(configMutex);
      config.clientId = "";
      config.refreshToken = "";
      config.authRevision++;
      config.revision++;
      Preferences n;
      n.begin("inkos", false);
      n.remove("client_id");
      n.remove("refresh");
      n.end();
    }
    {
      Guard g(modelMutex);
      model->music.authorized = false;
      model->music.valid = false;
      model->music.artValid = false;
      model->music.playing = false;
      model->music.lineCount = 0;
      model->music.revision++;
      model->music.artRevision++;
    }
    invalidate();
    return ack(true);
  }
  if (!strcmp(cmd, "settings_get")) {
    auto cfg = credentials();
    auto out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "inkos", true);
    auto energy = cJSON_AddObjectToObject(out, "energy");
    cJSON_AddBoolToObject(energy, "enabled", cfg.energy.enabled);
    cJSON_AddNumberToObject(energy, "provider", cfg.energy.provider);
    cJSON_AddNumberToObject(energy, "margin_ct", cfg.energy.marginCt);
    cJSON_AddNumberToObject(energy, "vat_percent", cfg.energy.vatPercent);
    cJSON_AddNumberToObject(energy, "transmission_ct", cfg.energy.transmissionCt);
    cJSON_AddBoolToObject(energy, "fees_include_vat", cfg.energy.feesIncludeVat);
    cJSON_AddStringToObject(energy, "endpoint", cfg.energy.endpoint.c_str());
    cJSON_AddStringToObject(energy, "api_header", cfg.energy.apiHeader.c_str());
    cJSON_AddBoolToObject(energy, "has_key", cfg.energy.apiKey.length() > 0);
    auto s = cJSON_AddObjectToObject(out, "settings");
    auto &v = cfg.settings;
    cJSON_AddStringToObject(s, "location", v.location);
    cJSON_AddStringToObject(s, "zone", v.localZone);
    cJSON_AddNumberToObject(s, "latitude", v.latitude);
    cJSON_AddNumberToObject(s, "longitude", v.longitude);
    cJSON_AddNumberToObject(s, "weather_source", v.weatherSource);
    cJSON_AddBoolToObject(s, "animations", v.animations);
    cJSON_AddNumberToObject(s, "page_switch_delay_ms", cfg.pageSwitchDelayMs);
    cJSON_AddNumberToObject(s, "encoder_lock_ms", cfg.encoderLockMs);
    cJSON_AddBoolToObject(s, "logging_enabled", cfg.loggingEnabled);
    cJSON_AddNumberToObject(s, "log_retention_days", cfg.logRetentionDays);
    cJSON_AddStringToObject(s, "met_contact", v.metContact);
    auto worlds = cJSON_AddArrayToObject(s, "worlds");
    for (auto &w : v.worlds) {
      auto row = cJSON_CreateObject();
      cJSON_AddStringToObject(row, "label", w.label);
      cJSON_AddStringToObject(row, "zone", w.zone);
      cJSON_AddItemToArray(worlds, row);
    }
    auto feeds = cJSON_AddArrayToObject(out, "feeds");
    for (auto &f : cfg.feeds) {
      auto row = cJSON_CreateObject();
      cJSON_AddStringToObject(row, "name", f.name.c_str());
      cJSON_AddStringToObject(row, "provider", f.provider.c_str());
      cJSON_AddStringToObject(row, "url", f.url.c_str());
      cJSON_AddBoolToObject(row, "enabled", f.enabled);
      cJSON_AddItemToArray(feeds, row);
    }
    return out;
  }
  if (!strcmp(cmd, "settings_set")) {
    auto current = credentials();
    Settings v = current.settings;
    uint32_t pageDelay = current.pageSwitchDelayMs, encoderLock = current.encoderLockMs;
    bool logging = current.loggingEnabled;
    uint16_t retention = current.logRetentionDays;
    auto s = cJSON_GetObjectItem(j, "settings");
    if (!cJSON_IsObject(s)) {
      return ack(false, "Missing settings");
    }
    auto delayField = cJSON_GetObjectItem(s, "page_switch_delay_ms");
    if (delayField) {
      if (!cJSON_IsNumber(delayField) || !isfinite(delayField->valuedouble) ||
          delayField->valuedouble < 100 || delayField->valuedouble > 5000 ||
          delayField->valuedouble != delayField->valueint)
        return ack(false, "Page switch delay must be 100-5000 integer milliseconds");
      pageDelay = delayField->valueint;
    }
    auto lockField = cJSON_GetObjectItem(s, "encoder_lock_ms");
    if (lockField) {
      if (!cJSON_IsNumber(lockField) || !isfinite(lockField->valuedouble) ||
          lockField->valuedouble < 0 || lockField->valuedouble > 10000 ||
          lockField->valuedouble != lockField->valueint)
        return ack(false, "Encoder lock must be 0-10000 integer milliseconds");
      encoderLock = lockField->valueint;
    }
    auto loggingField = cJSON_GetObjectItem(s, "logging_enabled");
    if (loggingField) {
      if (!cJSON_IsBool(loggingField)) return ack(false, "logging_enabled must be true or false");
      logging = cJSON_IsTrue(loggingField);
    }
    auto retentionField = cJSON_GetObjectItem(s, "log_retention_days");
    if (retentionField) {
      if (!cJSON_IsNumber(retentionField) || !validRetentionDays(retentionField->valueint) ||
          retentionField->valuedouble != retentionField->valueint)
        return ack(false, "log_retention_days must be 7, 21, 35, 90 or 180");
      retention = uint16_t(retentionField->valueint);
    }
    strlcpy(v.location, stringAt(s, "location"), sizeof v.location);
    strlcpy(v.localZone, stringAt(s, "zone"), sizeof v.localZone);
    auto lat = cJSON_GetObjectItem(s, "latitude"), lon = cJSON_GetObjectItem(s, "longitude"),
         source = cJSON_GetObjectItem(s, "weather_source");
    if (!*v.location || !findZone(v.localZone) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) ||
        fabs(lat->valuedouble) > 90 || fabs(lon->valuedouble) > 180 || !cJSON_IsNumber(source) ||
        source->valueint < 0 || source->valueint > 1) {
      return ack(false, "Invalid place, coordinates, time zone or source");
    }
    v.latitude = lat->valuedouble;
    v.longitude = lon->valuedouble;
    v.weatherSource = source->valueint;
    v.animations = cJSON_IsTrue(cJSON_GetObjectItem(s, "animations"));
    strlcpy(v.metContact, stringAt(s, "met_contact"), sizeof v.metContact);
    auto worlds = cJSON_GetObjectItem(s, "worlds");
    if (cJSON_GetArraySize(worlds) != 2) {
      return ack(false, "Two world clocks are required");
    }
    for (int i = 0; i < 2; i++) {
      auto w = cJSON_GetArrayItem(worlds, i);
      const char *zone = stringAt(w, "zone"), *label = stringAt(w, "label");
      if (!*label || !findZone(zone)) {
        return ack(false, "Unsupported clock time zone");
      }
      strlcpy(v.worlds[i].zone, zone, sizeof v.worlds[i].zone);
      strlcpy(v.worlds[i].label, label, sizeof v.worlds[i].label);
    }
    if (strchr(v.metContact, '\r') || strchr(v.metContact, '\n')) {
      return ack(false, "Invalid contact");
    }
    {
      Guard g(configMutex);
      v.revision = config.settings.revision + 1;
      if (!writeSettings(v)) {
        return ack(false, "Configuration storage is full");
      }
      if (pageDelay != config.pageSwitchDelayMs) {
        Preferences n;
        if (!n.begin("inkos", false)) return ack(false, "Could not save page switch delay");
        bool saved = n.putUInt("page_delay", pageDelay) == sizeof(uint32_t);
        n.end();
        if (!saved) return ack(false, "Could not save page switch delay");
      }
      if (encoderLock != config.encoderLockMs) {
        Preferences n;
        if (!n.begin("inkos", false)) return ack(false, "Could not save encoder lock");
        bool saved = n.putUInt("encoder_lock", encoderLock) == sizeof(uint32_t);
        n.end();
        if (!saved) return ack(false, "Could not save encoder lock");
      }
      if (logging != config.loggingEnabled) {
        Preferences n;
        if (!n.begin("inkos", false)) return ack(false, "Could not save logging setting");
        bool saved = n.putBool("log_on", logging) == 1;
        n.end();
        if (!saved) return ack(false, "Could not save logging setting");
      }
      if (retention != config.logRetentionDays) {
        Preferences n;
        if (!n.begin("inkos", false)) return ack(false, "Could not save log retention");
        bool saved = n.putUShort("log_ret", retention) == sizeof(uint16_t);
        n.end();
        if (!saved) return ack(false, "Could not save log retention");
        config.logRetentionDays = retention;
      }
      config.encoderLockMs = encoderLock;
      config.settings = v;
      config.pageSwitchDelayMs = pageDelay;
      config.revision++;
    }
    if (retention != current.logRetentionDays) setLogRetentionDays(retention);
    if (logging != current.loggingEnabled) {
      {Guard g(configMutex); config.loggingEnabled = logging;}
      applyLoggingEnabled(logging); // Takes effect now; flushes on disable.
    }
    {
      Guard g(modelMutex);
      model->settings = v;
      model->pageSwitchDelayMs = pageDelay;
      model->encoderLockMs = encoderLock;
      model->revision++;
    }
    invalidate();
    return ack(true);
  }
  if (!strcmp(cmd, "feed_set")) {
    auto index = cJSON_GetObjectItem(j, "index");
    int i = cJSON_IsNumber(index) ? index->valueint : -1;
    String url = stringAt(j, "url");
    if (url.startsWith("webcal://"))
      url = "https://" + url.substring(9);
    bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(j, "enabled"));
    if (i < 0 || i >= 3 || url.length() > 1500 || (url.length() && !url.startsWith("https://")) ||
        (enabled && url.isEmpty()) || url.indexOf('\r') >= 0 || url.indexOf('\n') >= 0) {
      return ack(false, "Use an HTTPS or webcal ICS subscription URL");
    }
    auto data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "url", url.c_str());
    cJSON_AddStringToObject(data, "name", stringAt(j, "name"));
    cJSON_AddStringToObject(data, "provider", stringAt(j, "provider"));
    cJSON_AddBoolToObject(data, "enabled", enabled);
    char *raw = cJSON_PrintUnformatted(data);
    char key[16];
    snprintf(key, sizeof key, "feed%d", i);
    bool saved = false;
    if (raw) {
      Guard g(configMutex);
      Preferences n;
      n.begin("inkos", false);
      saved = n.putString(key, raw) > 0;
      n.end();
      if (saved) {
        auto &f = config.feeds[i];
        f.url = url;
        f.name = stringAt(j, "name");
        f.provider = stringAt(j, "provider");
        f.enabled = enabled;
        config.revision++;
      }
    }
    if (raw)
      free(raw);
    cJSON_Delete(data);
    return ack(saved, saved ? "" : "Could not save feed");
  }
  return nullptr;
}
} // namespace ink
