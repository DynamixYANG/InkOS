#include "Canvas.h"
namespace ink {
void drawWeatherIcon(Canvas &,int,int,int,int,bool,unsigned,bool);
void Canvas::animatedIcon(int x,int y,int s,int code,bool day,unsigned phase) {
  drawWeatherIcon(*this,x,y,s,code,day,phase,true);
}
}
