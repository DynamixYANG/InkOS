#include "Ota.h"
#include "CjkAssets.h"
#include "CjkAssetsData.h"
#include "Storage.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <math.h>
#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) || !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error InkOS OTA requires the rollback-enabled ESP-IDF bootloader and SDK
#endif
// Arduino otherwise confirms PENDING_VERIFY in initArduino(), before our font,
// self-test and panel checks run. Defer confirmation to supervise() below.
extern "C" bool verifyRollbackLater(){return true;}
namespace ink {
constexpr char LAYOUT[]="inkos-ab594-extfs-v2",BOARD[]="xiao-esp32s3-plus-16m",VERSION[]="0.7.6";
static std::atomic<uint32_t> beats[3];
static std::atomic<bool> startup{false},uploading{false},confirmed{false};
static uint32_t bootId;
static esp_ota_handle_t handle=0;
static const esp_partition_t *target=nullptr;
static uint32_t expected=0,written=0,lastOffset=0,lastLength=0,touched=0;
static uint8_t expectedHash[32],lastHash[32];
static char session[33]={};
static mbedtls_sha256_context sha;
static int64_t restartAt=0;
static const char *string(cJSON*j,const char*k){auto f=cJSON_GetObjectItem(j,k);return cJSON_IsString(f)?f->valuestring:"";}
static bool integer(cJSON*j,const char*k,uint32_t&v){auto f=cJSON_GetObjectItem(j,k);if(!cJSON_IsNumber(f)||!isfinite(f->valuedouble)||f->valuedouble<0||f->valuedouble>UINT32_MAX||floor(f->valuedouble)!=f->valuedouble)return false;v=f->valuedouble;return true;}
static bool unhex(const char*s,uint8_t*out,size_t n){if(strlen(s)!=n*2)return false;for(size_t i=0;i<n;i++){char b[3]={s[2*i],s[2*i+1],0};if(!isxdigit(b[0])||!isxdigit(b[1]))return false;out[i]=strtoul(b,nullptr,16);}return true;}
static cJSON* reply(bool ok,const char*message=""){auto j=cJSON_CreateObject();cJSON_AddBoolToObject(j,"inkos",true);cJSON_AddBoolToObject(j,"ok",ok);if(*message)cJSON_AddStringToObject(j,"message",message);return j;}
static void abortUpload(){if(handle){esp_ota_abort(handle);handle=0;mbedtls_sha256_free(&sha);}uploading=false;written=expected=0;session[0]=0;}
bool otaBusy(){return uploading.load();}
void otaHeartbeat(unsigned task){if(task<3)beats[task]=millis();}
void otaStartupPassed(){startup=true;}
static void supervise(void*){
 auto running=esp_ota_get_running_partition();esp_ota_img_states_t state;bool pending=running&&esp_ota_get_state_partition(running,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
 uint32_t start=millis();
 for(;;){uint32_t now=millis();bool alive=startup&&cjkAssetsHealthy()&&storageHealthy();for(auto&b:beats){auto v=b.load();alive&=v&&now-v<60000;}
 if(!confirmed&&alive&&now-start>=30000){if(!pending||esp_ota_mark_app_valid_cancel_rollback()==ESP_OK){confirmed=true;Serial.println("INKOS OTA healthy; boot confirmed");}}
 if(pending&&!confirmed&&now-start>90000){Serial.println("INKOS OTA health failed; reverting");esp_ota_mark_app_invalid_rollback_and_reboot();}
 vTaskDelay(pdMS_TO_TICKS(250));}
}
void startOtaSupervisor(){bootId=esp_random();xTaskCreatePinnedToCore(supervise,"ota-health",4096,nullptr,1,nullptr,0);}
// Called from the single command/control task. No concurrent session mutation.
cJSON *otaCommand(const char*cmd,cJSON*j){
 if(uploading&&millis()-touched>90000)abortUpload();
 if(!strcmp(cmd,"ota_info")){
 auto out=reply(true);auto running=esp_ota_get_running_partition();auto next=esp_ota_get_next_update_partition(nullptr);esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;if(running)esp_ota_get_state_partition(running,&state);
 cJSON_AddStringToObject(out,"version",VERSION);cJSON_AddStringToObject(out,"board",BOARD);cJSON_AddStringToObject(out,"layout",LAYOUT);cJSON_AddStringToObject(out,"font_sha256",FONT_SHA256);
 cJSON_AddStringToObject(out,"slot",running?running->label:"");cJSON_AddNumberToObject(out,"slot_bytes",next?next->size:0);cJSON_AddNumberToObject(out,"boot_id",bootId);cJSON_AddNumberToObject(out,"state",state);cJSON_AddBoolToObject(out,"healthy",confirmed);cJSON_AddBoolToObject(out,"uploading",uploading);cJSON_AddNumberToObject(out,"received",written);return out;
 }
 if(!strcmp(cmd,"ota_restart")){restartAt=esp_timer_get_time()+1500000;return reply(true);}
 if(!strcmp(cmd,"ota_tick")){if(restartAt&&esp_timer_get_time()>=restartAt)ESP.restart();return nullptr;}
 if(!strcmp(cmd,"ota_begin")){
 if(uploading||restartAt||!confirmed)return reply(false,"Device is busy or boot health has not been confirmed");
 uint32_t size=0;uint8_t digest[32],header[288];size_t headerSize=0;const char*encoded=string(j,"header");
 target=esp_ota_get_next_update_partition(nullptr);
 if(!target||target->size!=0x5f0000||strcmp(string(j,"board"),BOARD)||strcmp(string(j,"layout"),LAYOUT)||strcmp(string(j,"font_sha256"),FONT_SHA256)||!integer(j,"size",size)||size<288||size>target->size||!unhex(string(j,"sha256"),digest,32)||strlen(encoded)>384||mbedtls_base64_decode(header,sizeof header,&headerSize,(const uint8_t*)encoded,strlen(encoded))||headerSize!=288)return reply(false,"Incompatible board, layout, fonts, size or image header");
 auto image=(const esp_image_header_t*)header;auto desc=(const esp_app_desc_t*)(header+sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t));
 if(image->magic!=ESP_IMAGE_HEADER_MAGIC||image->chip_id!=ESP_CHIP_ID_ESP32S3||desc->magic_word!=ESP_APP_DESC_MAGIC_WORD)return reply(false,"Not an ESP32-S3 application image");
 if(esp_ota_begin(target,size,&handle)!=ESP_OK){handle=0;return reply(false,"Could not open inactive OTA slot");}
 expected=size;written=lastLength=0;memcpy(expectedHash,digest,32);mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);uint8_t random[16];esp_fill_random(random,sizeof random);for(int i=0;i<16;i++)sprintf(session+2*i,"%02x",random[i]);uploading=true;touched=millis();auto out=reply(true);cJSON_AddStringToObject(out,"session",session);cJSON_AddStringToObject(out,"slot",target->label);cJSON_AddNumberToObject(out,"chunk_bytes",2560);return out;
 }
 if(strncmp(cmd,"ota_",4))return nullptr;
 if(!uploading||strcmp(string(j,"session"),session))return reply(false,"No matching OTA session");
 touched=millis();
 if(!strcmp(cmd,"ota_abort")){abortUpload();return reply(true);}
 if(!strcmp(cmd,"ota_chunk")){
 uint32_t offset;uint8_t data[2560],digest[32];size_t n=0;const char*encoded=string(j,"data");
 if(!integer(j,"offset",offset)||strlen(encoded)>3416||mbedtls_base64_decode(data,sizeof data,&n,(const uint8_t*)encoded,strlen(encoded))||!n)return reply(false,"Invalid OTA chunk");
 mbedtls_sha256(data,n,digest,0);
 if(lastLength&&offset==lastOffset&&n==lastLength&&!memcmp(lastHash,digest,32)){auto out=reply(true);cJSON_AddNumberToObject(out,"received",written);return out;}
 if(offset!=written||n>expected-written)return reply(false,"Unexpected OTA offset or length");
 if(esp_ota_write(handle,data,n)!=ESP_OK){abortUpload();return reply(false,"Inactive slot write failed");}
 mbedtls_sha256_update(&sha,data,n);lastOffset=written;lastLength=n;memcpy(lastHash,digest,32);written+=n;auto out=reply(true);cJSON_AddNumberToObject(out,"received",written);return out;
 }
 if(!strcmp(cmd,"ota_finish")){
 if(written!=expected)return reply(false,"Incomplete OTA image");
 uint8_t digest[32];mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);
 if(memcmp(digest,expectedHash,32)){abortUpload();return reply(false,"Firmware SHA-256 mismatch");}
 auto h=handle;handle=0;auto result=esp_ota_end(h);uploading=false;session[0]=0;
 if(result!=ESP_OK)return reply(false,"Application image verification failed");
 if(esp_ota_set_boot_partition(target)!=ESP_OK)return reply(false,"Could not select verified OTA slot");
 restartAt=esp_timer_get_time()+1500000;return reply(true,"Verified; restarting into inactive slot");
 }
 return reply(false,"Unsupported OTA command");
}
}
