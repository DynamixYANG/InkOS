#include "Services.h"
#include "Zones.h"
#include <Preferences.h>
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
void loadConfiguration() {
  configMutex = xSemaphoreCreateMutex();
  Preferences n;
  n.begin("wifi", true);
  config.ssid = n.getString("ssid", "");
  config.password = n.getString("pass", "");
  n.end();
  n.begin("inkos", true);
  config.clientId = n.getString("client_id", "");
  config.refreshToken = n.getString("refresh", "");
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
void saveWifi(const String &s, const String &p) {
  Guard g(configMutex);
  config.ssid = s;
  config.password = p;
  Preferences n;
  n.begin("wifi", false);
  n.putString("ssid", s);
  n.putString("pass", p);
  n.end();
  config.revision++;
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
static void reply(cJSON *j) {
  char *p = cJSON_PrintUnformatted(j);
  if (p) {
    Serial.println(p);
    free(p);
  }
  cJSON_Delete(j);
}
static void ack(bool ok, const char *message = "") {
  auto j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "inkos", true);
  cJSON_AddBoolToObject(j, "saved", ok);
  if (*message)
    cJSON_AddStringToObject(j, "message", message);
  reply(j);
}
bool configurationCommand(const char *cmd, cJSON *j) {
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
      ack(false, "Invalid provider, VAT, margin or transmission");
      return true;
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
      ack(false, "Use an HTTPS endpoint and a valid API header/value");
      return true;
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
    ack(saved, saved ? "" : "Could not save energy settings");
    return true;
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
    ack(true);
    return true;
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
    reply(out);
    return true;
  }
  if (!strcmp(cmd, "settings_set")) {
    Settings v = credentials().settings;
    auto s = cJSON_GetObjectItem(j, "settings");
    if (!cJSON_IsObject(s)) {
      ack(false, "Missing settings");
      return true;
    }
    strlcpy(v.location, stringAt(s, "location"), sizeof v.location);
    strlcpy(v.localZone, stringAt(s, "zone"), sizeof v.localZone);
    auto lat = cJSON_GetObjectItem(s, "latitude"), lon = cJSON_GetObjectItem(s, "longitude"),
         source = cJSON_GetObjectItem(s, "weather_source");
    if (!*v.location || !findZone(v.localZone) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) ||
        fabs(lat->valuedouble) > 90 || fabs(lon->valuedouble) > 180 || !cJSON_IsNumber(source) ||
        source->valueint < 0 || source->valueint > 1) {
      ack(false, "Invalid place, coordinates, time zone or source");
      return true;
    }
    v.latitude = lat->valuedouble;
    v.longitude = lon->valuedouble;
    v.weatherSource = source->valueint;
    v.animations = cJSON_IsTrue(cJSON_GetObjectItem(s, "animations"));
    strlcpy(v.metContact, stringAt(s, "met_contact"), sizeof v.metContact);
    auto worlds = cJSON_GetObjectItem(s, "worlds");
    if (cJSON_GetArraySize(worlds) != 2) {
      ack(false, "Two world clocks are required");
      return true;
    }
    for (int i = 0; i < 2; i++) {
      auto w = cJSON_GetArrayItem(worlds, i);
      const char *zone = stringAt(w, "zone"), *label = stringAt(w, "label");
      if (!*label || !findZone(zone)) {
        ack(false, "Unsupported clock time zone");
        return true;
      }
      strlcpy(v.worlds[i].zone, zone, sizeof v.worlds[i].zone);
      strlcpy(v.worlds[i].label, label, sizeof v.worlds[i].label);
    }
    if (strchr(v.metContact, '\r') || strchr(v.metContact, '\n')) {
      ack(false, "Invalid contact");
      return true;
    }
    {
      Guard g(configMutex);
      v.revision = config.settings.revision + 1;
      if (!writeSettings(v)) {
        ack(false, "Configuration storage is full");
        return true;
      }
      config.settings = v;
      config.revision++;
    }
    {
      Guard g(modelMutex);
      model->settings = v;
      model->revision++;
    }
    invalidate();
    ack(true);
    return true;
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
      ack(false, "Use an HTTPS or webcal ICS subscription URL");
      return true;
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
    ack(saved, saved ? "" : "Could not save feed");
    return true;
  }
  return false;
}
} // namespace ink
