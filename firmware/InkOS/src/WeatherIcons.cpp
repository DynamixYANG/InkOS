#include "Canvas.h"
#include "WeatherIconsData.h"
#include "WeatherIcons.h"
#include <cmath>
namespace ink {
void drawWeatherIcon(Canvas &c,int x,int y,int size,int code,bool day,unsigned phase,bool animated) {
  if(code<0){c.centered(x,y+size*2/3,size,"?",20);return;}
  // Production layouts use only native masks. Unsupported sizes fit a 40 px mask.
  int native=size>=64?64:40,index=weatherIconIndex(code,day);
  x+=(size-native)/2;y+=(size-native)/2;
  auto bits=weather_assets::bits(index,native);phase%=8;
  for(int row=0;row<native;row++)for(int col=0;col<native;col++) {
    int sx=col,sy=row;
    if(animated&&code==0&&day){
      double a=phase*3.141592653589793/32,mid=(native-1)*.5;
      sx=lround((col-mid)*cos(a)+(row-mid)*sin(a)+mid);
      sy=lround(-(col-mid)*sin(a)+(row-mid)*cos(a)+mid);
    }else if(animated&&code>=51&&row>=native*2/3){
      sy=native*2/3+(row-native*2/3+int(phase%4))%(native-native*2/3);
    }else if(animated&&index>=2&&index<=4){sx-=int(phase<4?phase:7-phase)-1;}
    if(sx>=0&&sx<native&&sy>=0&&sy<native&&!(bits[(sy*native+sx)/8]&(128>>((sy*native+sx)&7))))c.dot(x+col,y+row);
  }
}
void Canvas::metricIcon(int x,int y,int size,WeatherMetric metric) {
  int native=size>=64?64:size>=40?40:size>=24?24:16;
  auto bits=weather_assets::bits(int(metric),native);
  if(!bits)return;
  x+=(size-native)/2;y+=(size-native)/2;
  for(int row=0;row<native;row++)for(int col=0;col<native;col++)
    if(!(bits[(row*native+col)/8]&(128>>((row*native+col)&7))))dot(x+col,y+row);
}
}
