#include "Log.h"
#include <Arduino.h>
#include <cstdio>
#include <cassert>
#include <sstream>
#include <vector>
FakeSerial Serial;
using namespace ink;
static int notes=0; static void note(){notes++;}
static void checkSeq(const std::string&s,int&count,int&first,int&last){
  std::istringstream in(s); std::string l; int prev=-1; count=0; first=-1;
  while(std::getline(in,l)){ int q; assert(sscanf(l.c_str(),"INKOS test seq=%d",&q)==1); std::string pad(q%200,'x');
    assert(l=="INKOS test seq="+std::to_string(q)+" "+pad); assert(prev==-1||q==prev+1); if(first<0)first=q; prev=q; count++; }
  last=prev;
}
int main(){
  beginLog(); setLogFileNotify(note);
  Serial.room=100;
  for(int i=0;i<2000;i++){ std::string pad(i%200,'x'); logf("INKOS test seq=%d %s\n",i,pad.c_str()); drainLog(); }
  assert(Serial.out.empty()); assert(notes>0);
  printf("buffered=%u usb_dropped=%u file_dropped=%u notes=%d\n",bufferedLogBytes(),droppedLogLines(),droppedFileLogLines(),notes);
  // File reader drains independently (whole lines, small cap).
  std::string file; char buf[700]; size_t n;
  while((n=takeLogLines(LogReader::File,buf,sizeof buf))) { assert(buf[n-1]=='\n'); file.append(buf,n); }
  int c,f,l; checkSeq(file,c,f,l); assert(l==1999 && uint32_t(2000-c)==droppedFileLogLines());
  printf("file reader lines=%d first=%d last=%d\n",c,f,l);
  // USB still has its own copy.
  Serial.room=4096; for(int k=0;k<100;k++) drainLog();
  checkSeq(Serial.out,c,f,l); assert(l==1999 && uint32_t(2000-c)==droppedLogLines());
  printf("usb reader lines=%d first=%d last=%d\n",c,f,l);
  // Tail returns the newest whole lines.
  char t[3072]; n=tailLog(t,sizeof t); std::string tail(t,n); checkSeq(tail,c,f,l); assert(l==1999 && n<=3072 && c>0);
  printf("tail lines=%d bytes=%zu\n",c,n);
  // Disabled: no-op.
  setLoggingEnabled(false); uint32_t before=bufferedLogBytes(); logf("INKOS test seq=5000 \n"); assert(bufferedLogBytes()==before); assert(!pendingLogBytes(LogReader::File));
  setLoggingEnabled(true);
  clearLog(); assert(bufferedLogBytes()==0 && !pendingLogBytes(LogReader::Usb) && !tailLog(t,sizeof t));
  std::string big(1000,'y'); Serial.out.clear(); logf("%s",big.c_str()); drainLog(); assert(Serial.out.size()==319 && Serial.out.back()=='\n');
  puts("OK");
}
