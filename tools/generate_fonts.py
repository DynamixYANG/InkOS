"""Native bitmap UI text plus native-size IBM Plex Mono Light display numerals."""
import json,urllib.request
from pathlib import Path
from PIL import ImageFont
ROOT=Path(__file__).resolve().parents[1];assets=ROOT/'assets/fonts';assets.mkdir(parents=True,exist_ok=True)
base='https://raw.githubusercontent.com/IBM/plex/master/packages/plex-mono/fonts/complete/ttf/'
for name in ['IBMPlexMono-Light.ttf','IBMPlexMono-Regular.ttf','license.txt']:
    p=assets/name
    if not p.exists():p.write_bytes(urllib.request.urlopen(base+name,timeout=30).read())
chars=sorted(set(range(32,127))|set(range(160,384))|{0x2013,0x2014,0x2018,0x2019,0x201c,0x201d,0x2022,0x2026,0x2190,0x2192,0x20ac})
sizes=[14,16,18,20,24,28,32,48,76,96]
def bitmap_font(size):
    path=assets/'terminus'/f'ter-u{size}n.bdf'
    data=path.read_text(encoding='ascii');result={};advance=None
    for block in data.split('STARTCHAR ')[1:]:
        fields=block.splitlines();cp=int(next(s for s in fields if s.startswith('ENCODING ')).split()[1])
        w,h,x,y=map(int,next(s for s in fields if s.startswith('BBX ')).split()[1:])
        a=int(next(s for s in fields if s.startswith('DWIDTH ')).split()[1])
        if advance is None:advance=a
        assert a==advance,'UI font must be monospaced'
        start=fields.index('BITMAP')+1;rows=fields[start:start+h];bits=[]
        for row in rows:
            value=int(row,16);padded=len(row)*4
            bits.extend(255 if value & (1 << (padded-1-i)) else 0 for i in range(w))
        result[cp]=(w,h,(x,-y-h),bits)
    # A real pixel font keeps identical one-pixel vertical stems at each UI size.
    w,h,_,pixels=result[ord('H')]
    stems=[[pixels[y*w+x]>0 for x in range(w)] for y in range(h)]
    sparse=min((r for r in stems if any(r)),key=sum)
    assert sum(sparse)==2 and not any(a and b for a,b in zip(sparse,sparse[1:])), 'Expected one-pixel H stems'
    assert all(cp in result for cp in list(range(32,127))+list(map(ord,'ÅåÄäÖö°')))
    return result,advance

source=['// Generated from Terminus + IBM Plex Mono (OFL). Native 1-bit glyphs; no runtime scaling.','#include "Canvas.h"','namespace ink {']
counts={}
for size in sizes:
    bitmap,advance=bitmap_font(size) if size<=24 else (None,None)
    if bitmap is None:
        f=ImageFont.truetype(str(assets/'IBMPlexMono-Light.ttf'),size)
        advance=round(f.getlength('0'))
    bits=[];glyphs=[]
    for cp in chars:
        if bitmap is not None:
            if cp not in bitmap:continue
            w,h,offset,data=bitmap[cp]
        else:
            mask,offset=f.getmask2(chr(cp),mode='1',anchor='ls');w,h=mask.size;data=list(mask)
        glyphs.append((cp,len(bits),*offset,w,h))
        for start in range(0,len(data),8):bits.append(sum((128>>i) for i,v in enumerate(data[start:start+8]) if v))
    source.append(f'static const uint8_t bits{size}[]={{'+','.join(map(str,bits))+'};')
    source.append(f'static const Glyph glyphs{size}[]={{'+','.join('{'+','.join(map(str,g))+'}' for g in glyphs)+'};')
    source.append(f'static const Font font{size}={{bits{size},glyphs{size},{len(glyphs)},{advance},{size}}};')
    counts[size]=len(glyphs)
source.append('const Font& font(int size){switch(size){'+''.join(f'case {s}:return font{s};' for s in sizes)+'default:return font20;}}\n}')
(ROOT/'firmware/InkOS/src/Fonts.cpp').write_text('\n'.join(source),encoding='utf-8')
(assets/'manifest.json').write_text(json.dumps({'ui_family':'Terminus bitmap normal, <=24px; original hand-tuned one-bit grid, one-pixel stems','display_family':'IBM Plex Mono Light, >24px','source':base,'bitmap_source':'https://terminus-font.sourceforge.net/','bitmap_version':'4.49.1','sizes':sizes,'raster':'Original BDF pixels for UI; FreeType native monochrome for display; no runtime scaling or antialiasing','glyphs_per_size':counts,'coverage':'ASCII, Latin-1, Latin Extended-A, selected punctuation; additional scripts need another font provider','checks':'Monospaced advances; ASCII/Finnish/degree coverage; one-pixel H stems at all UI sizes'},indent=2),encoding='utf-8')
print('Generated native-size mono fonts:',sizes)
