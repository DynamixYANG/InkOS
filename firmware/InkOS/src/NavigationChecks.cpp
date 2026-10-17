#include "PageNavigation.h"
#include "RenderSchedule.h"
#include "NavigationWaveform.h"
#include "WeatherIcons.h"
#include "WeatherIconsData.h"
namespace ink {
void navigationChecks(int &passed,int &total) {
  auto check=[&](bool ok){total++;if(ok)passed++;};
  PageNavigation n;n.committed(0);
  n.observe(1,1,100000);
  check(n.pending()&&n.needsPreview()&&n.previewReady(100000));
  check(!n.ready(10000000,500)); // Never commit an unseen target.
  n.previewed(1,1200000);
  check(!n.ready(1299999,500)&&n.ready(1300000,500)); // Quiet overlaps waveform; 100ms visible hold.
  n.observe(1,2,1250000);
  check(!n.needsPreview()&&!n.ready(1749999,500)&&n.ready(1750000,500));
  n.observe(2,3,1300000);n.observe(3,4,1400000);
  check(n.previewReady(1400000)&&!n.ready(10000000,500));
  n.previewed(2,2300000);
  check(n.needsPreview()&&!n.ready(10000000,500));
  n.previewed(3,3400000);
  check(!n.ready(3499999,500)&&n.ready(3500000,500));
  check(!n.ready(6399999,5000)&&n.ready(6400000,5000));
  n.committed(3);check(!n.pending());
  n.observe(3,5,6500000);check(!n.pending());
  n.observe(4,6,6600000);n.previewed(4,7700000);n.observe(3,7,7800000);
  check(n.pending()&&n.needsPreview());
  n.previewed(3,8900000);check(!n.ready(8999999,100)&&n.ready(9000000,100));
  n.committed(3);n.observe(5,8,9100000);
  check(n.pending()&&n.previewReady(9100000));
  // Long configured quiet time is measured from the last input, not each render.
  n.previewed(5,10000000);check(!n.ready(14100000-1,5000)&&n.ready(14100000,5000));
  BackgroundSchedule background;
  check(!background.pending()&&!background.ready(1000000));
  background.request(100000);check(!background.ready(139999)&&background.ready(140000));
  background.request(130000);check(!background.ready(169999)&&background.ready(170000));
  background.request(240000);check(background.ready(250000)); // Publisher burst cap.
  background.clear();check(!background.pending());
  check(navigationWindowAllowed(1,37,4,34));
  check(!navigationWindowAllowed(0,37,4,34)&&!navigationWindowAllowed(1,38,4,34));
  check(!navigationWindowAllowed(1,37,3,34)&&!navigationWindowAllowed(1,37,4,35));
  check(!navigationWindowAllowed(5,4,4,34)&&!navigationWindowAllowed(1,37,20,19));
  check(navigationLutBytes(0)==60&&navigationLutBytes(1)==42&&navigationLutBytes(2)==60&&navigationLutBytes(3)==60&&navigationLutBytes(4)==60&&navigationLutBytes(5)==42);
  uint8_t kw[60],wk[60];makeNavigationLut(2,kw);makeNavigationLut(3,wk);
  check(kw[0]==0x5a&&wk[0]==0x84&&kw[1]==6&&kw[3]==6&&kw[2]==0&&kw[4]==0&&kw[5]==1&&wk[1]==wk[3]);
  bool cleared=true,neutral=true;
  for(int i=6;i<60;i++)cleared &= kw[i]==0&&wk[i]==0;
  check(cleared);
  const int neutralLuts[]={0,1,4,5};
  for(int lut:neutralLuts) {uint8_t table[60];makeNavigationLut(lut,table);neutral &= table[0]==0&&table[1]==table[3]&&table[2]==table[4];}
  check(neutral);
  check(weatherIconIndex(0,true)==0&&weatherIconIndex(0,false)==1);
  check(weatherIconIndex(1,true)==2&&weatherIconIndex(2,false)==3);
  check(weatherIconIndex(3,true)==4&&weatherIconIndex(45,true)==5&&weatherIconIndex(48,false)==5);
  check(weatherIconIndex(61,true)==6&&weatherIconIndex(81,true)==6&&weatherIconIndex(65,true)==7&&weatherIconIndex(82,false)==7);
  check(weatherIconIndex(71,true)==8&&weatherIconIndex(75,true)==9&&weatherIconIndex(86,false)==9);
  check(weatherIconIndex(56,true)==10&&weatherIconIndex(67,false)==10);
  check(weatherIconIndex(95,true)==11&&weatherIconIndex(96,true)==12&&weatherIconIndex(99,false)==12);
  check(weatherIconIndex(-1,true)==4&&weatherIconIndex(999,true)==4);
  const int sizes[]={16,24,40,64};bool masks=true;
  for(int i=0;i<16;i++)for(int size:sizes){auto bits=weather_assets::bits(i,size);int black=0;if(!bits){masks=false;continue;}for(int p=0;p<size*size;p++)if(!(bits[p/8]&(128>>(p%8))))black++;masks &= black>=8&&black<size*size/2;}
  check(masks);
  RenderToken a,b;check(a==b);
  b.agenda++;check(!(a==b));b=a;b.navigation++;check(!(a==b));
  b=a;b.lyric++;check(!(a==b));b=a;b.minute++;check(!(a==b));
  b=a;b.animation++;check(!(a==b));b=a;b.stale=true;check(!(a==b));
}
} // namespace ink
