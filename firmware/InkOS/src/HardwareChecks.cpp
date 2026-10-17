#include "Hardware.h"
#include "CalDav.h"
#include "CalendarLayout.h"
#include "Canvas.h"
#include <esp_heap_caps.h>
namespace ink {
void hardwareCalendarChecks(int &passed,int &total) {
  auto check=[&](bool ok){total++;if(ok)passed++;else Serial.printf("INKOS selftest failed_case=%d\n",total);};
  EncoderDecoder e;int sum=0;e.begin(3);
  for(int s:{1,0,2,3})sum+=e.update(s);check(sum==1);
  sum=0;for(int s:{2,0,1,3})sum+=e.update(s);check(sum==-1);
  sum=0;for(int s:{1,3,1,0,1,0,2,3})sum+=e.update(s);check(sum==1);
  sum=0;for(int s:{0,3,0,3})sum+=e.update(s);check(sum==0);
  sum=0;for(int i=0;i<8;i++)for(int s:{1,0,2,3})sum+=e.update(s);check(sum==8);
  EncoderInput input; input.begin(3);
  for(int s:{1,0,2,3})input.edge(s,100);
  input.lock(3,1000,2000);
  check(input.take(1000)==0&&input.locked(2000999)&&!input.locked(2001000));
  for(int s:{1,0,2,3})input.edge(s,1500000);
  check(input.take(2001000)==0);
  for(int s:{1,0,2,3})input.edge(s,2001000);
  check(input.take(2001000)==1);
  input.lock(3,3000000,1000); input.edge(1,3999999);
  for(int s:{0,2,3})input.edge(s,4000000);
  check(input.take(4000000)==0);
  for(int s:{2,0,1,3})input.edge(s,4000001);
  check(input.take(4000001)==-1);
  input.lock(3,5000000,0);
  for(int s:{1,0,2,3})input.edge(s,5000000);
  check(input.take(5000000)==1&&!input.locked(5000000));
  for(int s:{1,0,2,3})input.edge(s,5000001);
  input.lock(3,5000001,0);
  check(input.take(5000001)==1); // No swallowed pending detent when disabled.
  check(batteryPercent(NAN)==-1&&batteryPercent(0)==-1&&batteryPercent(5)==-1);
  check(batteryPercent(3.2f)==0&&batteryPercent(4.2f)==100&&batteryPercent(3.8f)==40);
  check(batteryPercent(3.9f)>batteryPercent(3.8f)&&batteryPercent(3.75f)<batteryPercent(3.8f));
  check(wifiLevel(-50)==3&&wifiLevel(-65)==2&&wifiLevel(-85)==1);
  for(auto stamp:{Clock::epoch(2026,10,18,12),Clock::epoch(2026,12,31,23),Clock::epoch(2026,10,25,1)}) {
    auto first=agendaFirstDay(stamp,"Europe/Helsinki");auto local=Clock::utc(first);
    check(local.tm_hour==0&&local.tm_min==0);
  }
  auto day=agendaFirstDay(Clock::epoch(2026,10,18,12),"Europe/Helsinki");
  check(Clock::utc(day).tm_wday==0&&Clock::utc(day+6*86400).tm_wday==6);
  day=agendaFirstDay(Clock::epoch(2026,12,31,12),"Europe/Helsinki");
  check(Clock::utc(day+6*86400).tm_year==127&&Clock::utc(day+6*86400).tm_mday==6);
  check(validICloudUrl("https://p123-caldav.icloud.com/123/calendars/test/")&&validICloudUrl("https://caldav.icloud.com:443/123/"));
  for(auto url:{"http://caldav.icloud.com/","https://caldav.icloud.com.evil.invalid/","https://evil@caldav.icloud.com/","https://p1-caldav.icloud.com:8443/","https://p-caldav.icloud.com/","https://caldav.icloud.com/\r\nX: y"})check(!validICloudUrl(url));
  auto agenda=(Agenda*)heap_caps_calloc(1,sizeof(Agenda),MALLOC_CAP_SPIRAM);
  if(!agenda){check(false);return;}
  time_t begin=Clock::epoch(2026,10,17),end=begin+7*86400;
  String query=calDavQuery(begin,end);check(query.indexOf("20261017T000000Z")>=0&&query.indexOf("20261024T000000Z")>=0&&query.indexOf("VEVENT")>=0);
  auto parse=[&](const char *xml){auto copy=strdup(xml);bool ok=parseCalDav(copy,strlen(copy),"Europe/Helsinki",begin,end,*agenda);free(copy);return ok;};
  check(parse("<d:multistatus xmlns:d=\"DAV:\"/>"));
  check(parse("<multistatus xmlns=\"DAV:\"><response><propstat><prop><calendar-data xmlns=\"urn:ietf:params:xml:ns:caldav\"><![CDATA[BEGIN:VCALENDAR\nVERSION:2.0\nBEGIN:VEVENT\nUID:fixture\nDTSTART:20261017T100000Z\nDTEND:20261017T110000Z\nSUMMARY:Apple & calendar\nEND:VEVENT\nEND:VCALENDAR\n]]></calendar-data></prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>")&&agenda->count==1&&!strcmp(agenda->events[0].title,"Apple & calendar"));
  check(!parse("<d:multistatus xmlns:d=\"urn:wrong\"/>"));
  check(!parse("<!DOCTYPE multistatus [<!ENTITY secret SYSTEM 'file:///private'>]><multistatus xmlns=\"DAV:\"/>"));
  check(!parse("<d:multistatus xmlns:d=\"DAV:\"><d:response><d:status>HTTP/1.1 403 Forbidden</d:status></d:response></d:multistatus>"));
  check(!parse("<multistatus xmlns=\"DAV:\">"));
  free(agenda);
}
}
