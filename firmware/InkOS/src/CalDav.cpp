#include "CalDav.h"
#include "ICalendar.h"
#include <pugixml.hpp>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
namespace ink {
bool validICloudUrl(const String &url) {
  if (!url.startsWith("https://") || url.length()>1024 || url.indexOf('#')>=0 || url.indexOf('?')>=0 ||
      url.indexOf('\\')>=0 || url.indexOf('\r')>=0 || url.indexOf('\n')>=0 || url.indexOf(' ')>=0) return false;
  int slash=url.indexOf('/',8);if(slash<0)return false;
  String host=url.substring(8,slash);host.toLowerCase();
  if(host.endsWith(":443"))host.remove(host.length()-4);
  if(host=="caldav.icloud.com")return true;
  if(!host.startsWith("p")||!host.endsWith("-caldav.icloud.com"))return false;
  int n=host.length()-strlen("-caldav.icloud.com");
  if(n<2||n>5)return false;
  for(int i=1;i<n;i++)if(!isdigit(host[i]))return false;
  return true;
}
String calDavQuery(time_t begin,time_t end) {
  char a[24],b[24];auto s=Clock::utc(begin),e=Clock::utc(end);
  strftime(a,sizeof a,"%Y%m%dT%H%M%SZ",&s);strftime(b,sizeof b,"%Y%m%dT%H%M%SZ",&e);
  return String("<?xml version=\"1.0\" encoding=\"UTF-8\"?><c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag/><c:calendar-data/></d:prop><c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\"><c:time-range start=\"")+a+"\" end=\""+b+"\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>";
}
namespace {
constexpr const char *Dav="DAV:",*Cal="urn:ietf:params:xml:ns:caldav";
bool named(pugi::xml_node node,const char *space,const char *local) {
  const char *name=node.name(),*colon=strchr(name,':');
  if(strcmp(colon?colon+1:name,local))return false;
  String attr="xmlns";if(colon){attr+=":";attr+=String(name,colon-name);}
  for(auto scope=node;scope;scope=scope.parent())if(auto ns=scope.attribute(attr.c_str()))return !strcmp(ns.value(),space);
  return false;
}
pugi::xml_node child(pugi::xml_node node,const char *space,const char *local) {
  for(auto item:node.children())if(named(item,space,local))return item;return {};
}
bool success(pugi::xml_node status) {
  char protocol[16];int code=0;return sscanf(status.child_value(),"%15s %d",protocol,&code)==2&&code==200;
}
}
bool parseCalDav(char *xml,size_t length,const char *zone,time_t begin,time_t end,Agenda &agenda) {
  if(!xml||length>524288||strstr(xml,"<!DOCTYPE")||strstr(xml,"<!ENTITY"))return false;
  // This library is used only by the calendar network task and startup checks.
  pugi::set_memory_management_functions([](size_t n)->void*{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);},free);
  pugi::xml_document doc;
  if(!doc.load_buffer_inplace(xml,length,pugi::parse_default,pugi::encoding_utf8))return false;
  auto root=doc.document_element();if(!named(root,Dav,"multistatus"))return false;
  int responses=0;
  for(auto response:root.children()) {
    if(!named(response,Dav,"response"))continue;
    if(++responses>256)return false;
    bool found=false;
    for(auto propstat:response.children()) {
      if(!named(propstat,Dav,"propstat"))continue;
      auto prop=child(propstat,Dav,"prop"),data=child(prop,Cal,"calendar-data");
      if(!data)continue;
      if(!success(child(propstat,Dav,"status")))return false;
      String calendar;
      for(auto text:data.children()) {
        if(text.type()!=pugi::node_pcdata&&text.type()!=pugi::node_cdata)return false;
        calendar+=text.value();
      }
      if(!parseCalendar(calendar.c_str(),zone,begin,end,agenda))return false;
      found=true;
    }
    if(!found)return false;
  }
  return true; // An empty successful multistatus is an empty calendar.
}
bool fetchICloud(const ICloudConfig &cfg,const char *zone,time_t begin,time_t end,Agenda &agenda,int &status) {
  status=-1;if(!validICloudUrl(cfg.calendarUrl))return false;
  String plain=cfg.username+":"+cfg.password;char auth[512];size_t written=0;
  int result=mbedtls_base64_encode((unsigned char*)auth,sizeof auth-1,&written,(const unsigned char*)plain.c_str(),plain.length());
  if(result)return false;auth[written]=0;
  RequestOptions options;options.method="REPORT";options.contentType="application/xml; charset=utf-8";
  options.depth="1";options.redirects=true;options.icloudOnly=true;options.timeoutMs=12000;
  HttpResponse response;
  bool online=request(cfg.calendarUrl,response,524288,String("Basic ")+auth,calDavQuery(begin,end),options);
  memset(auth,0,sizeof auth);plain="";status=response.status;
  if(!online||status!=207)return false;
  return parseCalDav((char*)response.bytes,response.length,zone,begin,end,agenda);
}
}
