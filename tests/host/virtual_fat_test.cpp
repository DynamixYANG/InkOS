#include "VirtualFat.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>
using namespace ink;
std::map<std::string,std::string> data;
bool rd(const char*n,uint32_t o,uint8_t*out,size_t k){auto&d=data[n]; if(o+k>d.size())return false; memcpy(out,d.data()+o,k); return true;}
int main(){
  VirtualFatFile f[18]={}; int n=0;
  for(int i=1;i<=16;i++){ char nm[32]; snprintf(nm,32,"inkos-%06d.tar.gz",i); std::string d; for(int k=0;k<9000+i*1234;k++) d+=char('a'+(k*7+i)%26); data[nm]=d; strcpy(f[n].name,nm); f[n].size=d.size(); f[n].mtime=1790000000+i*60; n++;}
  std::string cur; for(int k=0;k<3000;k++) cur+="line "+std::to_string(k)+"\n"; data["current.log"]=cur; strcpy(f[n].name,"current.log"); f[n].size=cur.size(); n++;
  strcpy(f[n].name,"empty.log"); f[n].size=0; data["empty.log"]=""; n++;
  VirtualFat v; int kept=v.load(f,n,0x1234abcd); fprintf(stderr,"kept %d\n",kept);
  FILE*o=fopen("img.bin","wb"); uint8_t s[512];
  for(uint32_t l=0;l<VirtualFat::TotalSectors;l++){v.sector(l,s,rd); fwrite(s,1,512,o);} fclose(o);
}
