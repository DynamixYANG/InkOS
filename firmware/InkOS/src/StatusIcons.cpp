#include "Canvas.h"
#include "Hardware.h"
#include "Layout.h"
#include <cmath>
namespace ink {
void renderDeviceStatus(Canvas &c, const Model &m) {
  constexpr int y=15,w=28,h=14;
  int x=m.batteryPercent>=0?648:712;
  c.box(x,y,w,h); c.rect(x+w,y+4,3,6);
  if (m.batteryPercent >= 0)
    c.rect(x+3,y+3,(w-6)*m.batteryPercent/100,h-6);
  char b[12];
  if (m.batteryPercent < 0) c.line(x-3,y-3,x+w+3,y+h+3,2);
  else {
    snprintf(b,sizeof b,"%d%%",m.batteryPercent);
    c.text(686,layout::HeaderBaseline,b,14);
  }
  int level=m.wifi?wifiLevel(m.wifiRssi):0;
  for (int ring=1;ring<=3;ring++) {
    int radius=ring*5;
    for (int a=-138;a<=-42;a+=3) {
      if (ring>level && a%9) continue;
      float angle=a*3.14159265f/180;
      c.dot(771+lroundf(radius*cosf(angle)),31+lroundf(radius*sinf(angle)));
    }
  }
  c.circle(771,31,2,2);
  if (!m.wifi) c.line(754,12,786,34,2);
}
}
