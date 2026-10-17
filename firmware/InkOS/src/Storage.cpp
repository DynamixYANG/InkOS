#include "Storage.h"
#include "CjkAssetsData.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <driver/spi_master.h>
#include <esp_flash.h>
#include <esp_flash_spi_init.h>
#include <esp_image_format.h>
#include <nvs.h>
#include <esp_random.h>
namespace ink {
static esp_flash_t *chip;
static const esp_partition_t *partition;
static bool attempted=false,mounted=false;
static uint32_t jedec=0,capacity=0;
static esp_err_t lastError=ESP_OK;
bool beginExternalStorageFlash(){
  if(attempted)return partition!=nullptr;
  attempted=true;
  pinMode(43,OUTPUT);digitalWrite(43,HIGH);
  pinMode(39,OUTPUT);digitalWrite(39,HIGH);
  pinMode(40,OUTPUT);digitalWrite(40,HIGH);delay(100);
  spi_bus_config_t bus={};bus.mosi_io_num=13;bus.miso_io_num=12;bus.sclk_io_num=11;
  bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=4096;
  bus.flags=SPICOMMON_BUSFLAG_MASTER;
  lastError=spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO);
  if(lastError!=ESP_OK)return false;
  esp_flash_spi_device_config_t device={};device.host_id=SPI2_HOST;device.cs_io_num=40;
  device.io_mode=SPI_FLASH_SLOWRD;device.freq_mhz=10;device.clock_source=SPI_CLK_SRC_DEFAULT;
  lastError=spi_bus_add_flash_device(&chip,&device);if(lastError!=ESP_OK)return false;
  lastError=esp_flash_init(chip);if(lastError!=ESP_OK)return false;
  lastError=esp_flash_read_id(chip,&jedec);if(lastError!=ESP_OK)return false;
  lastError=esp_flash_get_size(chip,&capacity);
  if(lastError!=ESP_OK||jedec!=0x5e4016||capacity!=0x400000){lastError=ESP_ERR_INVALID_SIZE;return false;}
  lastError=esp_partition_register_external(chip,0,capacity,"extstore",ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,&partition);
  return lastError==ESP_OK;
}
const esp_partition_t *externalStoragePartition(){return beginExternalStorageFlash()?partition:nullptr;}
bool ensureStorage(){
  if(mounted)return true;
  if(!beginExternalStorageFlash())return false;
  // Never format a failed mount automatically. Migration supplies a verified
  // copy of the former filesystem; LittleFS expands it to the full 4 MiB.
  mounted=LittleFS.begin(false,"/inkstore",5,"extstore");
  if(!mounted)lastError=ESP_FAIL;
  return mounted;
}
bool storageHealthy(){return mounted;}
bool checkStorage(){
  if(!mounted)return false;
  char path[48];snprintf(path,sizeof path,"/.inkos-probe-%08lx",(unsigned long)esp_random());
  if(LittleFS.exists(path))return false;
  uint8_t data[256];for(int i=0;i<256;i++)data[i]=i;
  auto out=LittleFS.open(path,"w");if(!out)return false;
  bool ok=out.write(data,sizeof data)==sizeof data;out.flush();out.close();
  uint8_t received[256];auto in=LittleFS.open(path,"r");ok&=bool(in);
  if(in){ok&=in.size()==sizeof data&&in.read(received,sizeof received)==sizeof received&&!memcmp(data,received,sizeof data);in.close();}
  bool removed=LittleFS.remove(path);return ok&&removed;
}
cJSON *storageInfo(){
  auto out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"inkos",true);
  cJSON_AddBoolToObject(out,"ok",true);cJSON_AddNumberToObject(out,"internal_bytes",0x1000000);
  cJSON_AddNumberToObject(out,"boot_metadata_reserved",0x20000);
  auto parts=cJSON_AddArrayToObject(out,"partitions");
  for(const char *name:{"ota_0","ota_1","fonts"}){
    auto p=esp_partition_find_first(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,name);
    if(!p)continue;auto item=cJSON_CreateObject();cJSON_AddStringToObject(item,"name",name);
    cJSON_AddNumberToObject(item,"offset",p->address);cJSON_AddNumberToObject(item,"bytes",p->size);
    uint32_t used=0;
    if(p->type==ESP_PARTITION_TYPE_APP){esp_partition_pos_t pos={p->address,p->size};esp_image_metadata_t meta={};if(esp_image_get_metadata(&pos,&meta)==ESP_OK)used=meta.image_len;}
    else used=FONT_ARCHIVE_BYTES;
    cJSON_AddNumberToObject(item,"used_bytes",used);cJSON_AddNumberToObject(item,"free_bytes",p->size-used);cJSON_AddItemToArray(parts,item);
  }
  auto ext=cJSON_AddObjectToObject(out,"external");cJSON_AddNumberToObject(ext,"jedec",jedec);
  cJSON_AddNumberToObject(ext,"bytes",capacity);cJSON_AddNumberToObject(ext,"spi_hz",10000000);
  cJSON_AddBoolToObject(ext,"mounted",mounted);cJSON_AddNumberToObject(ext,"init_error",lastError);
  size_t used=mounted?LittleFS.usedBytes():0,total=mounted?LittleFS.totalBytes():0;
  cJSON_AddNumberToObject(ext,"filesystem_bytes",total);cJSON_AddNumberToObject(ext,"used_bytes",used);
  cJSON_AddNumberToObject(ext,"free_bytes",total-used);
  nvs_stats_t stats={};if(nvs_get_stats("nvs",&stats)==ESP_OK){auto n=cJSON_AddObjectToObject(out,"nvs");cJSON_AddNumberToObject(n,"used_entries",stats.used_entries);cJSON_AddNumberToObject(n,"free_entries",stats.free_entries);}
  return out;
}
}
