#pragma once
#include "Domain.h"
#include <cJSON.h>
namespace ink {
struct EnergyConfig {
  bool enabled = true;
  int provider = 0; // 0: Sahkotin net EUR/MWh, 1: Porssisahko VAT-inclusive ct, 2: custom net ct.
  double vatPercent = 25.5, marginCt = .45, transmissionCt = 6;
  bool feesIncludeVat = false;
  String endpoint = "https://sahkotin.fi/prices";
  String apiHeader = "Authorization", apiKey;
};
void networkTask(void *);
void controlTask(void *);
void displayTask(void *);
struct Credentials {
  String ssid, password, clientId, refreshToken;
  uint32_t authRevision = 0;
  uint32_t revision = 0;
  Settings settings;
  EnergyConfig energy;
  uint32_t energyRevision = 0;
  struct Feed {
    String name, provider, url;
    bool enabled = false;
  } feeds[3];
};
void loadConfiguration();
bool configurationCommand(const char *, cJSON *);
Credentials credentials();
void saveWifi(const String &, const String &);
void saveSpotify(const String &, const String &);
void rotateToken(const String &);
void selectPage(int);
void navigatePage(int);
void updateCalendars();
void loadAgenda();
class HttpResponse {
public:
  int status = 0, retrySeconds = 0;
  uint8_t *bytes = nullptr;
  size_t length = 0, capacity = 0;
  String etag, lastModified, expires;
  ~HttpResponse();
};
struct RequestOptions {
  String userAgent, etag, lastModified;
  String headerName, headerValue;
  bool redirects = false;
};
bool request(const String &url, HttpResponse &, size_t maximum = 131072, const String &auth = "",
             const String &body = "", const RequestOptions &options = RequestOptions());
bool fetchWeather(Weather &);
void updateSpotify();
} // namespace ink
