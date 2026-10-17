// USB-only maintenance bridge. Never accesses the ESP32 boot flash or NVS.
#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
SPIClass bus(FSPI);
// This owner's EE04 board, verified by static GPIO tests and 5E4016 RDID.
// The supplied schematic's CLK/MOSI GPIO labels are reversed on this board.
// U5: pin1 CS40, pin2 DO12, pin5 DI13, pin6 CLK11. XIAO pin7/D6 is GPIO43.
constexpr int CS=40, CLK=11, MISO_PIN=12, MOSI_PIN=13, POWER_EN=43, FONT_CS=39;
constexpr uint32_t EXPECTED_ID=0x5e4016;
uint32_t capacity=0,spiHz=100000;
int clockPin=CLK,mosiPin=MOSI_PIN;
bool spiReady=false;
bool beginBus(){return spiReady=bus.begin(clockPin,MISO_PIN,mosiPin,CS);}
bool routePins(cJSON *j){
  auto swapped=cJSON_GetObjectItem(j,"swapped");if(!cJSON_IsBool(swapped))return false;
  digitalWrite(CS,HIGH);bus.end();capacity=0;
  bool swap=cJSON_IsTrue(swapped);clockPin=swap?MOSI_PIN:CLK;mosiPin=swap?CLK:MOSI_PIN;
  return beginBus();
}
void highOutput(int pin){gpio_set_level(static_cast<gpio_num_t>(pin),1);pinMode(pin,OUTPUT);digitalWrite(pin,HIGH);}
// GPIO identity test: keep BOTH slave selects high throughout, so no command
// can be accepted. The next info command restores the hardware SPI mux.
bool pinLevels(cJSON *j){
  auto m=cJSON_GetObjectItem(j,"mosi"),c=cJSON_GetObjectItem(j,"clk");
  if(!cJSON_IsNumber(m)||!cJSON_IsNumber(c)||
     (m->valuedouble!=0&&m->valuedouble!=1)||(c->valuedouble!=0&&c->valuedouble!=1))return false;
  highOutput(CS);highOutput(FONT_CS);bus.end();spiReady=false;capacity=0;
  pinMode(CLK,OUTPUT);pinMode(MOSI_PIN,OUTPUT);pinMode(MISO_PIN,INPUT);
  digitalWrite(CLK,c->valueint);digitalWrite(MOSI_PIN,m->valueint);return true;
}
// ZB25VQ32 table 8.6: CS setup/hold >=5ns; deselect >=100ns.
// Explicit 1us guards also exceed reset-command tRCH >=40ns.
void selectChip(){bus.beginTransaction(SPISettings(spiHz,MSBFIRST,SPI_MODE0));delayMicroseconds(1);digitalWrite(CS,LOW);delayMicroseconds(1);}
void releaseChip(){delayMicroseconds(1);digitalWrite(CS,HIGH);delayMicroseconds(1);bus.endTransaction();}
void address(uint32_t a){bus.transfer(a>>16);bus.transfer(a>>8);bus.transfer(a);}
void enableWrite(){selectChip();bus.transfer(6);releaseChip();}
bool waitReady(){uint32_t start=millis();for(;;){selectChip();bus.transfer(5);auto s=bus.transfer(0);releaseChip();if(!(s&1))return true;if(millis()-start>5000)return false;delay(1);}}
// Both modes latch DI on rising CLK and shift DO on falling CLK (section 6.1.1).
uint8_t bitTransfer(uint8_t value,int mode){uint8_t out=0;for(int i=7;i>=0;i--){digitalWrite(CLK,LOW);digitalWrite(MOSI_PIN,(value>>i)&1);delayMicroseconds(5);digitalWrite(CLK,HIGH);delayMicroseconds(5);out=(out<<1)|digitalRead(MISO_PIN);if(!mode)digitalWrite(CLK,LOW);}return out;}
uint32_t bitId(int cs,int mode){digitalWrite(CLK,mode?HIGH:LOW);delayMicroseconds(1);digitalWrite(cs,LOW);delayMicroseconds(1);bitTransfer(0x9f,mode);uint32_t id=uint32_t(bitTransfer(0,mode))<<16;id|=uint32_t(bitTransfer(0,mode))<<8;id|=bitTransfer(0,mode);delayMicroseconds(1);digitalWrite(cs,HIGH);delayMicroseconds(1);return id;}
cJSON*probe(){
  digitalWrite(CS,HIGH);bus.end();spiReady=false;capacity=0;
  for(int pin: {CLK,MOSI_PIN,MISO_PIN,FONT_CS,CS})pinMode(pin,INPUT);
  digitalWrite(POWER_EN,LOW);delay(200);digitalWrite(POWER_EN,HIGH);delay(1000);
  highOutput(FONT_CS);highOutput(CS);
  pinMode(CLK,OUTPUT);digitalWrite(CLK,LOW);
  pinMode(MOSI_PIN,OUTPUT);digitalWrite(MOSI_PIN,LOW);pinMode(MISO_PIN,INPUT_PULLUP);
  // Release deep power-down, then reset. No WREN/erase/program/status writes.
  for(int instruction: {0xab,0x66,0x99}){
    digitalWrite(CLK,LOW);digitalWrite(CS,LOW);delayMicroseconds(1);
    bitTransfer(instruction,0);delayMicroseconds(1);digitalWrite(CS,HIGH);delay(2);
  }
  auto out=cJSON_CreateArray();
  for(int mode: {0,1}){
    auto item=cJSON_CreateObject();cJSON_AddNumberToObject(item,"cs",CS);
    cJSON_AddNumberToObject(item,"mode",mode?3:0);
    cJSON_AddNumberToObject(item,"id",bitId(CS,mode));cJSON_AddItemToArray(out,item);
  }
  clockPin=CLK;mosiPin=MOSI_PIN;beginBus();return out;
}
void setup(){
  highOutput(POWER_EN);
  delay(100); // tVSL >=10us; schematic R31 pulls U5 CS# up during power-up.
  Serial.setRxBufferSize(8192);Serial.begin(115200);
  highOutput(FONT_CS);
  highOutput(CS);
  beginBus();
}
void loop(){static String input;while(Serial.available()){char c=Serial.read();if(c!='\n'){if(input.length()<7000)input+=c;continue;}auto j=cJSON_Parse(input.c_str());input="";if(!j)continue;auto get=[&](const char*k){return cJSON_GetObjectItem(j,k);};auto cmd=get("cmd");auto out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"bridge",true);bool ok=false;uint32_t a=cJSON_IsNumber(get("offset"))?get("offset")->valueint:0,n=cJSON_IsNumber(get("length"))?get("length")->valueint:0;
if(cJSON_IsString(cmd)&&!strcmp(cmd->valuestring,"speed")){auto hz=get("hz");if(cJSON_IsNumber(hz)&&hz->valuedouble>=100000&&hz->valuedouble<=10000000&&hz->valuedouble==hz->valueint){spiHz=hz->valueint;ok=true;}cJSON_AddNumberToObject(out,"spi_hz",spiHz);}
else if(cJSON_IsString(cmd)&&!strcmp(cmd->valuestring,"route")){ok=routePins(j);}
else if(cJSON_IsString(cmd)&&!strcmp(cmd->valuestring,"pins")){ok=pinLevels(j);}
else if(cJSON_IsString(cmd)&&!strcmp(cmd->valuestring,"probe")){cJSON_AddItemToObject(out,"probe",probe());ok=true;}
else if(cJSON_IsString(cmd)&&!strcmp(cmd->valuestring,"info")){if(!spiReady)beginBus();selectChip();bus.transfer(0x9f);uint32_t id=bus.transfer(0)<<16;id|=bus.transfer(0)<<8;id|=bus.transfer(0);releaseChip();capacity=spiReady&&id==EXPECTED_ID?0x400000:0;cJSON_AddBoolToObject(out,"spi_ready",spiReady);cJSON_AddNumberToObject(out,"expected_jedec",EXPECTED_ID);cJSON_AddNumberToObject(out,"clk_gpio",clockPin);cJSON_AddNumberToObject(out,"mosi_gpio",mosiPin);cJSON_AddNumberToObject(out,"spi_hz",spiHz);cJSON_AddNumberToObject(out,"uptime_ms",millis());cJSON_AddNumberToObject(out,"pwr_en",digitalRead(43));cJSON_AddNumberToObject(out,"jedec",id);cJSON_AddNumberToObject(out,"capacity",capacity);ok=capacity>0;}
else if(capacity&&cJSON_IsString(cmd)&&a<capacity&&n<=capacity-a){if(!strcmp(cmd->valuestring,"read")&&n<=4096){static uint8_t data[4096],encoded[5468];size_t count;selectChip();bus.transfer(3);address(a);bus.transferBytes(nullptr,data,n);releaseChip();mbedtls_base64_encode(encoded,sizeof encoded,&count,data,n);encoded[count]=0;cJSON_AddStringToObject(out,"data",(char*)encoded);ok=true;}
else if(!strcmp(cmd->valuestring,"erase")&&a%4096==0&&n==4096){enableWrite();selectChip();bus.transfer(0x20);address(a);releaseChip();ok=waitReady();}
else if(!strcmp(cmd->valuestring,"write")&&n<=256&&n>0&&(a/256==(a+n-1)/256)){static uint8_t data[256];size_t count=0;auto d=get("data");if(cJSON_IsString(d)&&!mbedtls_base64_decode(data,sizeof data,&count,(uint8_t*)d->valuestring,strlen(d->valuestring))&&count==n){enableWrite();selectChip();bus.transfer(2);address(a);bus.writeBytes(data,n);releaseChip();ok=waitReady();}}}
cJSON_AddBoolToObject(out,"ok",ok);char*raw=cJSON_PrintUnformatted(out);Serial.println(raw);free(raw);cJSON_Delete(j);cJSON_Delete(out);}}
