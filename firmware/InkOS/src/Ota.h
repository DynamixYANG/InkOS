#pragma once
#include <cJSON.h>
namespace ink {
void startOtaSupervisor();
void otaHeartbeat(unsigned task);
void otaStartupPassed();
bool otaBusy();
cJSON *otaCommand(const char *command,cJSON *request);
}
