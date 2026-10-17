// USB maintenance only: back up external NOR before cloning the internal FS.
#include <Arduino.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include "../../firmware/InkOS/src/Storage.cpp"
using namespace ink;
static String input;
static bool hashPartition(const esp_partition_t*p,char hex[65]){
  static uint8_t data[4096];uint8_t digest[32];mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
  for(uint32_t pos=0;pos<p->size;pos+=sizeof data){size_t n=min(size_t(p->size-pos),sizeof data);if(esp_partition_read(p,pos,data,n)!=ESP_OK){mbedtls_sha256_free(&sha);return false;}mbedtls_sha256_update(&sha,data,n);delay(1);}
  mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);for(int i=0;i<32;i++)sprintf(hex+i*2,"%02x",digest[i]);return true;
}
static bool cloneStorage(cJSON*j,const esp_partition_t*dst){
  auto source=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"storage");
  auto before=cJSON_GetObjectItem(j,"external_before_sha256"),expected=cJSON_GetObjectItem(j,"internal_sha256");
  char hash[65];if(!source||source->size!=0x3e0000||!cJSON_IsString(before)||!cJSON_IsString(expected)||
    !hashPartition(dst,hash)||strcmp(hash,before->valuestring)||!hashPartition(source,hash)||strcmp(hash,expected->valuestring))return false;
  static uint8_t data[4096];
  for(uint32_t pos=0;pos<dst->size;pos+=sizeof data){
    if(esp_partition_erase_range(dst,pos,sizeof data)!=ESP_OK)return false;
    if(pos<source->size&&(esp_partition_read(source,pos,data,sizeof data)!=ESP_OK||esp_partition_write(dst,pos,data,sizeof data)!=ESP_OK))return false;
    if(pos%0x40000==0)Serial.printf("CLONE %lu%%\n",100UL*pos/dst->size);delay(1);
  }
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);uint8_t digest[32];
  for(uint32_t pos=0;pos<source->size;pos+=sizeof data){if(esp_partition_read(dst,pos,data,sizeof data)!=ESP_OK)return false;mbedtls_sha256_update(&sha,data,sizeof data);delay(1);}
  mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);for(int i=0;i<32;i++)sprintf(hash+i*2,"%02x",digest[i]);return !strcmp(hash,expected->valuestring);
}
void setup(){Serial.setRxBufferSize(8192);Serial.setTxBufferSize(8192);Serial.begin(115200);}
void loop(){
  while(Serial.available()){
    char ch=Serial.read();if(ch!='\n'){if(input.length()<7000)input+=ch;continue;}
    auto j=cJSON_Parse(input.c_str());input="";if(!j)continue;
    auto cmd=cJSON_GetObjectItem(j,"cmd"),offset=cJSON_GetObjectItem(j,"offset"),length=cJSON_GetObjectItem(j,"length");
    auto out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"bridge",true);
    auto p=externalStoragePartition();bool ok=false;
    if(p&&cJSON_IsString(cmd)){
      if(!strcmp(cmd->valuestring,"info")){uint32_t id=0;esp_flash_read_id(p->flash_chip,&id);cJSON_AddNumberToObject(out,"jedec",id);cJSON_AddNumberToObject(out,"capacity",p->size);ok=true;}
      else if((!strcmp(cmd->valuestring,"read")||!strcmp(cmd->valuestring,"read_internal"))&&cJSON_IsNumber(offset)&&cJSON_IsNumber(length)){
        if(!strcmp(cmd->valuestring,"read_internal"))p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"storage");
        uint32_t a=offset->valueint,n=length->valueint;
        if(p&&n>0&&n<=4096&&a<p->size&&n<=p->size-a){static uint8_t data[4096],encoded[5468];size_t count;
          ok=esp_partition_read(p,a,data,n)==ESP_OK;if(ok){mbedtls_base64_encode(encoded,sizeof encoded,&count,data,n);encoded[count]=0;cJSON_AddStringToObject(out,"data",(char*)encoded);}}
      }else if(!strcmp(cmd->valuestring,"clone"))ok=cloneStorage(j,p);
      else if(!strcmp(cmd->valuestring,"mount")){ok=ensureStorage();if(ok){cJSON_AddNumberToObject(out,"total",LittleFS.totalBytes());cJSON_AddNumberToObject(out,"used",LittleFS.usedBytes());int count=0;auto root=LittleFS.open("/");for(auto f=root.openNextFile();f;f=root.openNextFile()){count++;f.close();}cJSON_AddNumberToObject(out,"files",count);}}
    }
    cJSON_AddBoolToObject(out,"ok",ok);char*raw=cJSON_PrintUnformatted(out);Serial.println(raw);free(raw);cJSON_Delete(out);cJSON_Delete(j);
  }
}
