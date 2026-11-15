"""Regenerate checked-in Unicode 17.0 text property tables from unicode.org. Run at repo root."""
from pathlib import Path
import urllib.request
classes=['L','R','AL','EN','ES','ET','AN','CS','NSM','BN','B','S','WS','ON','LRE','LRO','RLE','RLO','PDF','LRI','RLI','FSI','PDI']
names={'Left_To_Right':'L','Right_To_Left':'R','Arabic_Letter':'AL','European_Terminator':'ET'}
text=urllib.request.urlopen('https://www.unicode.org/Public/17.0.0/ucd/extracted/DerivedBidiClass.txt').read().decode()
values=bytearray(0x110000)
for missing in [True,False]:
 for line in text.splitlines():
  if missing:
   if '@missing:' not in line:continue
   line=line.split('@missing:')[1]
  else:line=line.split('#')[0]
  if ';' not in line:continue
  r,c=map(str.strip,line.split(';')[:2]);c=names.get(c,c)
  if c not in classes:continue
  ends=r.split('..');a=int(ends[0],16);b=int(ends[-1],16);values[a:b+1]=bytes([classes.index(c)])*(b-a+1)
ranges=[]
for cp,c in enumerate(values):
 if ranges and ranges[-1][2]==c:ranges[-1][1]=cp
 else:ranges.append([cp,cp,c])
Path('src/etch_bidi_data.h').write_text('// Unicode 17.0.0 extracted/DerivedBidiClass.txt.\n// https://www.unicode.org/Public/17.0.0/ucd/\n// Unicode data license: https://www.unicode.org/license.txt\nstruct BidiRange { uint32_t first, last; uint8_t cls; };\nstatic constexpr BidiRange kBidiRanges[] = {\n'+''.join('{0x%Xu,0x%Xu,%du},\n'%(a,b,c) for a,b,c in ranges)+'};\n')

from pathlib import Path
import urllib.request
base='https://www.unicode.org/Public/17.0.0/ucd/'
props=['Other','CR','LF','Control','Extend','ZWJ','Regional_Indicator','Prepend','SpacingMark','L','V','T','LV','LVT']
ranges=[]
for line in urllib.request.urlopen(base+'auxiliary/GraphemeBreakProperty.txt').read().decode().splitlines():
 line=line.split('#')[0].strip()
 if not line:continue
 r,c=map(str.strip,line.split(';'));ends=r.split('..');ranges.append((int(ends[0],16),int(ends[-1],16),props.index(c)))
for line in urllib.request.urlopen(base+'emoji/emoji-data.txt').read().decode().splitlines():
 line=line.split('#')[0].strip()
 if not line:continue
 r,c=map(str.strip,line.split(';'))
 if c!='Extended_Pictographic':continue
 ends=r.split('..');ranges.append((int(ends[0],16),int(ends[-1],16),14))
gcb=sorted(r for r in ranges if r[2]!=14);ep=sorted(r for r in ranges if r[2]==14)
Path('src/etch_grapheme_data.h').write_text('// Unicode 17.0.0 GraphemeBreakProperty.txt and emoji-data.txt.\n// https://www.unicode.org/Public/17.0.0/ucd/\n// Unicode data license: https://www.unicode.org/license.txt\nstruct GraphemeRange { uint32_t first, last; uint8_t prop; };\n'+''.join('static constexpr GraphemeRange '+name+'[] = {\n'+''.join('{0x%Xu,0x%Xu,%du},\n'%r for r in rows)+'};\n' for name,rows in [('kGraphemeRanges',gcb),('kPictographicRanges',ep)]))

rows=[]
for line in urllib.request.urlopen('https://www.unicode.org/Public/17.0.0/ucd/BidiBrackets.txt').read().decode().splitlines():
 line=line.split('#')[0].strip()
 if not line:continue
 a,b,t=map(str.strip,line.split(';'));rows.append((int(a,16),int(b,16),t=='o'))
p=Path('src/etch_bidi_data.h');s=p.read_text();s+='\nstruct BidiBracket { uint32_t cp, pair; bool open; };\nstatic constexpr BidiBracket kBidiBrackets[] = {\n'+''.join('{0x%Xu,0x%Xu,%s},\n'%(a,b,str(t).lower()) for a,b,t in rows)+'};\n';p.write_text(s)
