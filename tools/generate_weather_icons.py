"""Render pinned Lucide rounded outlines as thin, native monochrome panel masks."""
from pathlib import Path
import hashlib,json,math,xml.etree.ElementTree as ET
from fontTools.pens.basePen import BasePen
from fontTools.svgLib.path import parse_path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets/icons/lucide'
NAMES=['sun','moon-star','cloud-sun','cloud-moon','cloud','cloud-fog','cloud-rain','cloud-rain-wind','cloud-snow','cloud-snow','cloud-drizzle','cloud-lightning','cloud-hail','thermometer','droplet','wind']
SIZES=[16,24,40,64]
STROKE={16:1.,24:1.,40:1.25,64:2.}
class StrokePen(BasePen):
 def __init__(self):super().__init__(None);self.paths=[];self.path=[]
 def _moveTo(self,p):
  if self.path:self._endPath()
  self.path=[p]
 def _lineTo(self,p):self.path.append(p)
 def _curveToOne(self,a,b,c):
  p=self.path[-1]
  for i in range(1,65):
   t=i/64;u=1-t;self.path.append(tuple(u*u*u*p[k]+3*u*u*t*a[k]+3*u*t*t*b[k]+t*t*t*c[k] for k in [0,1]))
 def _qCurveToOne(self,a,b):
  p=self.path[-1]
  for i in range(1,65):
   t=i/64;u=1-t;self.path.append(tuple(u*u*p[k]+2*u*t*a[k]+t*t*b[k] for k in [0,1]))
 def _closePath(self):
  if self.path:self.paths.append((self.path+[self.path[0]],True));self.path=[]
 def _endPath(self):
  if self.path:self.paths.append((self.path,False));self.path=[]
def paths_for(name):
 pen=StrokePen()
 for e in ET.fromstring((ASSETS/(name+'.svg')).read_bytes()):
  tag=e.tag.rsplit('}',1)[-1]
  if tag=='path':parse_path(e.attrib['d'],pen)
  elif tag=='circle':
   x,y,r=map(float,[e.attrib['cx'],e.attrib['cy'],e.attrib['r']]);points=[(x+r*math.cos(i*math.tau/256),y+r*math.sin(i*math.tau/256)) for i in range(257)];pen.paths.append((points,True))
  elif tag=='line':pen.paths.append(([(float(e.attrib['x1']),float(e.attrib['y1'])),(float(e.attrib['x2']),float(e.attrib['y2']))],False))
  elif tag in ['polyline','polygon']:
   values=list(map(float,e.attrib['points'].replace(',',' ').split()));points=list(zip(values[::2],values[1::2]));pen.paths.append((points+[points[0]] if tag=='polygon' else points,tag=='polygon'))
  else:raise ValueError('Unsupported Lucide element: '+tag)
 pen._endPath();return pen.paths

def raster(paths,size,index):
 scale=8;factor=size*scale/24;image=Image.new('L',(size*scale,size*scale),255);draw=ImageDraw.Draw(image)
 width=round(STROKE[size]*scale);radius=width/2
 for points,closed in paths:
  coords=[(x*factor-scale*.5,y*factor-scale*.5) for x,y in points]
  draw.line(coords,fill=0,width=width,joint='curve')
  if not closed:
   for x,y in [coords[0],coords[-1]]:draw.ellipse((x-radius,y-radius,x+radius,y+radius),fill=0)
 # Keep the source SVG unchanged; heavy snow adds small flake strokes to its
 # six source snow points so intensity is distinguishable at native sizes.
 if index==9:
  for x,y in [(8,15),(8,19),(12,17),(12,21),(16,15),(16,19)]:
   for dx,dy in [(0.7,0),(0,0.7)]:draw.line([((x-dx)*factor-scale*.5,(y-dy)*factor-scale*.5),((x+dx)*factor-scale*.5,(y+dy)*factor-scale*.5)],fill=0,width=width)
 return image.resize((size,size),Image.Resampling.BOX).point(lambda p:255 if p>=128 else 0,mode='1')

def main():
 upstream=json.loads((ASSETS/'upstream.json').read_text());output=['// Generated from pinned Lucide SVGs; ISC/MIT notices in assets/icons/lucide.','#pragma once','#include <stdint.h>','namespace ink::weather_assets {'];images=[];manifest={}
 for index,name in enumerate(NAMES):
  path=ASSETS/(name+'.svg');manifest[name]=hashlib.sha256(path.read_bytes()).hexdigest();paths=paths_for(name)
  for size in SIZES:
   image=raster(paths,size,index);images.append((index,size,image))
   assert image.mode=='1' and len(image.tobytes())==size*size//8
   output.append(f'static const uint8_t i{index}_{size}[]={{'+','.join(map(str,image.tobytes()))+'};')
 output.append('inline const uint8_t *bits(int index,int size){switch(index){')
 for index in range(len(NAMES)):output.append(f'case {index}: switch(size){{case 16:return i{index}_16;case 24:return i{index}_24;case 40:return i{index}_40;case 64:return i{index}_64;default:return nullptr;}}')
 output+=['default:return nullptr;}}','}']
 (ROOT/'firmware/InkOS/src/WeatherIconsData.h').write_text('\n'.join(output)+'\n',encoding='utf-8')
 (ASSETS/'manifest.json').write_text(json.dumps(dict(**upstream,style='thin rounded stroke',stroke_pixels=STROKE,native_sizes=SIZES,sha256=manifest,derivatives={'heavy snow':'add six small snowflake crosses'}),indent=2)+'\n')
 (ASSETS/'SOURCE.md').write_text('# Lucide thin weather symbols\n\nPinned source: https://github.com/lucide-icons/lucide/tree/'+upstream['commit']+'\n\nOriginal SVGs are unchanged. LICENSE preserves Lucide ISC and inherited Feather MIT notices. InkOS renders rounded outlines at 16/24/40/64 native pixels using 1/1/1.25/2 px target strokes and a single monochrome threshold. Heavy snow adds small flake crosses. No runtime scaling, grey antialiasing or image generation is used.\n',encoding='utf-8')
 gallery=Image.new('RGB',(8*116,2*138),'white');d=ImageDraw.Draw(gallery);font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',11)
 labels=['SUN','NIGHT','PARTLY DAY','PARTLY NIGHT','CLOUD','FOG','RAIN','HEAVY RAIN','SNOW','HEAVY SNOW','MIXED RAIN','THUNDER','HAIL','TEMPERATURE','PRECIPITATION','WIND']
 for index in range(len(NAMES)):
  x=(index%8)*116;y=(index//8)*138
  for i,size,image in images:
   if i==index and size in [16,40,64]:
    if size==64:gallery.paste(image.convert('RGB'),(x+26,y+5))
    elif size==40:gallery.paste(image.convert('RGB'),(x+12,y+76))
    else:gallery.paste(image.convert('RGB'),(x+74,y+85))
  d.text((x+4,y+120),labels[index],font=font,fill='black')
 destination=ROOT/'out/lucide-thin-weather-icons.png';destination.parent.mkdir(exist_ok=True);gallery.save(destination)
 print('Generated',len(NAMES),'Lucide thin symbols in',len(SIZES),'native sizes.')
if __name__=='__main__':main()
