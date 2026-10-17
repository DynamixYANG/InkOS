#pragma once
#include "Services.h"
namespace ink {
bool validICloudUrl(const String &);
String calDavQuery(time_t begin, time_t end);
bool parseCalDav(char *xml, size_t length, const char *zone, time_t begin, time_t end, Agenda &);
bool fetchICloud(const ICloudConfig &, const char *zone, time_t begin, time_t end, Agenda &, int &http);
}
