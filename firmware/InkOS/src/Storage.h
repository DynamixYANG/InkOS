#pragma once
#include <cJSON.h>
#include <esp_partition.h>
namespace ink {
bool beginExternalStorageFlash();
bool ensureStorage();
bool storageHealthy();
bool checkStorage();
const esp_partition_t *externalStoragePartition();
cJSON *storageInfo();
}
