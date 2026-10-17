#pragma once
#include <Arduino.h>
#include <cJSON.h>
class WebServer;
namespace ink {
// A pairing key is provisioned over USB only. HTTP carries authenticated ciphertext.
void loadRemoteConfiguration();
cJSON *remotePairCommand(cJSON *command);
bool remoteEnabled();
void registerRemoteConfiguration(WebServer &server);
String executeCommand(cJSON *command, bool usb);
}
