#include "CjkAssets.h"
#include "CjkAssetsData.h"
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <miniz.h>
namespace ink {
static SemaphoreHandle_t lock;
static const uint8_t *packed;
static spi_flash_mmap_handle_t mapping;
static std::atomic<bool> healthy{false};
static uint32_t count,indexOffset,dataOffset,blockCount,clockAge;
constexpr uint32_t BLOCK=262144;
struct Cache {uint8_t *data=nullptr;uint32_t index=UINT32_MAX,age=0;};
static Cache cache[3];
static tinfl_decompressor *decoder;
static uint32_t u32(const uint8_t*p){uint32_t v;memcpy(&v,p,4);return v;}
// Caller holds lock. ROM miniz state lives in PSRAM, never on the task stack.
static const uint8_t *blockData(uint32_t index){
  if(index>=blockCount)return nullptr;
  Cache *entry=nullptr;
  for(auto &c:cache)if(c.index==index){c.age=++clockAge;return c.data;}
  for(auto &c:cache)if(!entry||c.age<entry->age)entry=&c;
  uint32_t start=u32(packed+64+index*4),end=u32(packed+64+(index+1)*4);
  if(start<64+(blockCount+1)*4||end<=start||end>FONT_ARCHIVE_BYTES)return nullptr;
  size_t input=end-start,output=std::min(BLOCK,FONT_RAW_BYTES-index*BLOCK);
  size_t expected=output;
  tinfl_init(decoder);
  auto status=tinfl_decompress(decoder,packed+start,&input,entry->data,entry->data,&output,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if(status!=TINFL_STATUS_DONE||output!=expected||input!=end-start){entry->index=UINT32_MAX;return nullptr;}
  entry->index=index;entry->age=++clockAge;return entry->data;
}
static bool readArchive(uint32_t offset,void *target,size_t length){
  if(offset>FONT_RAW_BYTES||length>FONT_RAW_BYTES-offset)return false;
  xSemaphoreTake(lock,portMAX_DELAY);auto out=(uint8_t*)target;bool ok=true;
  while(length){auto bytes=blockData(offset/BLOCK);if(!bytes){ok=false;break;}size_t n=std::min(length,size_t(BLOCK-offset%BLOCK));memcpy(out,bytes+offset%BLOCK,n);out+=n;offset+=n;length-=n;}
  xSemaphoreGive(lock);return ok;
}
bool beginCjkAssets(){
  lock=xSemaphoreCreateMutex();
  auto p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"fonts");
  if(!lock||!p||p->size!=0x400000||FONT_ARCHIVE_BYTES>p->size||esp_partition_mmap(p,0,p->size,ESP_PARTITION_MMAP_DATA,(const void**)&packed,&mapping)!=ESP_OK)return false;
  if(memcmp(packed,"INKFZ1\0\0",8)||u32(packed+8)!=1||u32(packed+12)!=FONT_ARCHIVE_BYTES||u32(packed+16)!=FONT_RAW_BYTES||u32(packed+20)!=BLOCK||u32(packed+28)!=64)return false;
  blockCount=u32(packed+24);if(blockCount!=(FONT_RAW_BYTES+BLOCK-1)/BLOCK||u32(packed+64+blockCount*4)!=FONT_ARCHIVE_BYTES)return false;
  uint8_t digest[32];mbedtls_sha256(packed,FONT_ARCHIVE_BYTES,digest,0);char hex[65];for(int i=0;i<32;i++)sprintf(hex+2*i,"%02x",digest[i]);if(strcmp(hex,FONT_SHA256))return false;
  decoder=(tinfl_decompressor*)heap_caps_calloc(1,sizeof(tinfl_decompressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!decoder)return false;
  for(auto&c:cache){c.data=(uint8_t*)heap_caps_malloc(BLOCK,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!c.data)return false;}
  // Also verify the decoded stream so the ROM decoder and every block are checked before startup.
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts(&sha,0);
  xSemaphoreTake(lock,portMAX_DELAY);bool ok=true;
  for(uint32_t i=0;i<blockCount;i++){auto b=blockData(i);if(!b){ok=false;break;}mbedtls_sha256_update(&sha,b,std::min(BLOCK,FONT_RAW_BYTES-i*BLOCK));delay(1);}
  xSemaphoreGive(lock);mbedtls_sha256_finish(&sha,digest);mbedtls_sha256_free(&sha);if(!ok||memcmp(digest,packed+32,32))return false;
  uint8_t header[64];if(!readArchive(0,header,sizeof header)||memcmp(header,"INKFNT1\0",8))return false;
  count=u32(header+16);indexOffset=u32(header+28);dataOffset=u32(header+32);
  // Validate the archive contract, not a particular font release's glyph count.
  if(!count||count>65536||u32(header+8)!=1||u32(header+12)!=FONT_RAW_BYTES||
     u32(header+20)!=4||u32(header+24)!=64||
     indexOffset!=((64+count*2+15)&~15u)||
     dataOffset!=((indexOffset+count*4*8+15)&~15u)||dataOffset>FONT_RAW_BYTES)return false;
  healthy=true;return true;
}
bool cjkAssetsHealthy(){return healthy;}
bool cjkLookup(uint32_t code,int size,CjkGlyph &g,int &step){
  if(!healthy||code>65535)return false;int l=0,r=count-1;
  while(l<=r){int m=(l+r)/2;uint16_t cp;if(!readArchive(64+m*2,&cp,2))return false;
    if(cp==code){int face=size<=16?0:size<=20?1:size<=24?2:3;step=face==0?14:face==1?18:face==2?24:28;return readArchive(indexOffset+(face*count+m)*8,&g,8);}
    if(cp<code)l=m+1;else r=m-1;
  }return false;
}
bool cjkRead(const CjkGlyph&g,uint8_t*p,size_t cap){size_t n=(unsigned(g.w)*g.h+7)/8;return healthy&&n<=cap&&g.offset<=FONT_RAW_BYTES-dataOffset&&readArchive(dataOffset+g.offset,p,n);}
}
