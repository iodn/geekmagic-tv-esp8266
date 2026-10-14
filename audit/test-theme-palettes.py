"""Verify shared presets, LCD contrast, generated swatches and compatible IDs."""
from pathlib import Path
import json,re,runpy,tempfile,subprocess
root=Path(__file__).resolve().parents[1]
catalog=json.loads((root/'src/device-themes.json').read_text())
palettes=catalog['palettes']
assert [p['label'] for p in palettes[:3]]==['Aurora','Sunset','Terminal']
assert [p['id'] for p in palettes]==list(range(len(palettes)))
roles=('background','surface','surfaceAlt','accent','accentSoft','text','muted','positive','negative','warning')
def rgb(color):return tuple(int(color[i:i+2],16) for i in (1,3,5))
def quantized(color):
 r,g,b=rgb(color);return ((r>>3)*255//31,(g>>2)*255//63,(b>>3)*255//31)
def luminance(color):
 values=[c/255 for c in quantized(color)]
 return sum((c/12.92 if c<=0.04045 else ((c+.055)/1.055)**2.4)*weight for c,weight in zip(values,(.2126,.7152,.0722)))
def contrast(a,b):
 light,dark=sorted((luminance(a),luminance(b)),reverse=True)
 return (light+.05)/(dark+.05)
minimum=100
for palette in palettes:
 assert palette['source'].startswith('https://') and palette['family']
 for fg in ('text','muted','accent','positive','negative','warning'):
  for bg in ('background','surface','surfaceAlt','accentSoft'):
   ratio=contrast(palette['colors'][fg],palette['colors'][bg]);minimum=min(minimum,ratio)
   assert ratio>=4.5,(palette['label'],fg,bg,ratio)
with tempfile.TemporaryDirectory() as folder:
 class Env:
  def subst(self,key):return str(root) if key=='$PROJECT_DIR' else folder
  def Append(self,**kwargs):pass
 module=runpy.run_path(str(root/'scripts/compress-webui.py'),init_globals={'Import':lambda name:None,'env':Env()})
 header=(Path(folder)/'generated/device_theme_palettes.h').read_text()
 actual=[tuple(map(int,c)) for c in re.findall(r'rgb565\((\d+), (\d+), (\d+)\)',header)]
 expected=[rgb(p['colors'][role]) for p in palettes for role in roles]
 assert actual==expected
 web=module['expand_webui'](root/'src/webui.h')
 labels=json.loads(re.search(r'const themeLabels = ([^\n]+);',web).group(1))
 assert len(labels)==len(palettes)
 for palette,label in zip(palettes,labels):
  assert label['value']==palette['id'] and label['label']==palette['label']
  assert label['mode']==palette['mode'] and label['family']==palette['family']
  assert label['tones']==['#%02X%02X%02X'%quantized(palette['colors'][role]) for role in ('background','surface','accent')]
  assert label['text']=='#%02X%02X%02X'%quantized(palette['colors']['text'])
 node_source=subprocess.check_output(['node','-e',"process.stdout.write(require('./audit/read-webui.cjs')())"],cwd=root,text=True)
 assert node_source==web
 # Generated palette declarations use the real enum and RGB conversion at compile time.
 (Path(folder)/'Arduino.h').write_text('#pragma once\n#include <cstdint>\n#include <string>\nusing String=std::string;\n')
 (Path(folder)/'check.cpp').write_text('#include "dashboard.h"\n#include <cassert>\nstruct ThemePalette{uint16_t '+','.join(roles)+';};\nconstexpr uint16_t rgb565(int r,int g,int b){return ((r&248)<<8)|((g&252)<<3)|(b>>3);}\n#include "generated/device_theme_palettes.h"\nint main(){static_assert(DASHBOARD_THEME_AURORA==0 && DASHBOARD_THEME_SUNSET==1 && DASHBOARD_THEME_TERMINAL==2);assert(DASHBOARD_THEME_COUNT=='+str(len(palettes))+');}\n')
 subprocess.run(['g++','-std=c++17','-I'+folder,'-I'+str(root/'src'),str(Path(folder)/'check.cpp'),'-o',str(Path(folder)/'check')],check=True)
 subprocess.run([str(Path(folder)/'check')],check=True)
print(f'Theme palettes passed: {len(palettes)} presets, source parity, compatible IDs, RGB565 contrast min {minimum:.2f}:1')
