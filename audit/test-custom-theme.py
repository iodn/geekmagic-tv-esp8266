"""Execute custom-theme derivation against light/dark LCD backgrounds."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/display.cpp').read_text()
def function(name):
    match=re.search(r'^(?:Rgb888|uint16_t|float|ThemePalette|void|const ThemePalette&) '+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert match,name
    end=source.index('{',match.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
code='''#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>
using std::min;using std::max;
#define DISPLAY_WIDTH 240
#define DASHBOARD_THEME_COUNT 14
#define constrain(v,a,b) std::min(std::max(int(v),int(a)),int(b))
'''
code+=source[source.index('constexpr uint16_t rgb565'):source.index('int savedBrightness')]
# Parser is bool; include its actual implementation too.
start=source.index('bool parseHexColor888(');end=source.index('Rgb888 blendRgb',start)
code+=source[start:end]
for name in ('rgb565ToRgb888','rgb888ToRgb565','blendRgb','themeLuminance','readableDerivedColor','buildCustomTheme'):
    code+=function(name)+'\n'
code+='''
constexpr uint32_t kFnvOffset=2166136261UL,kFnvPrime=16777619UL;
ThemeSelection selection={0,true,"#FFFFFF","#808080","#00AAFF","#000000"};
ThemeSelection effectiveThemeSelection(){return selection;}
'''
for name in ('hashBytes','hashValue','hashCString','hashThemeConfig','activeTheme'):
    if name=='hashValue':code+='template <typename TValue>\n'
    code+=function(name)+'\n'
code+='''
float lum(uint16_t c){auto r=rgb565ToRgb888(c);float sum=0;int v[]={r.red,r.green,r.blue};float w[]={.2126f,.7152f,.0722f};for(int i=0;i<3;i++){float x=v[i]/255.f;sum+=w[i]*(x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f));}return sum;}
float ratio(uint16_t a,uint16_t b){return (std::max(lum(a),lum(b))+.05f)/(std::min(lum(a),lum(b))+.05f);}
int main(){
 auto first=activeTheme();selection.background="#000000";auto second=activeTheme();assert(first.background!=second.background);
 selection.text="#ABCDEF";auto third=activeTheme();assert(third.text==rgb565(171,205,239));
 selection.customThemeEnabled=false;selection.theme=13;assert(activeTheme().background==kThemes[13].background);
 selection.customThemeEnabled=true;selection.background="#FFFFFF";assert(activeTheme().background==rgb565(255,255,255));
 for(auto bg:{"#FFFFFF","#000000","#808080","#1A2548"})for(auto &base:kThemes){
 auto t=buildCustomTheme(base,bg,"#808080","#00AAFF","#FFFFFF");
 assert(t.surface==rgb565(128,128,128)&&t.accent==rgb565(0,170,255)&&t.text==rgb565(255,255,255));
 for(auto c:{t.muted,t.positive,t.negative,t.warning})assert(ratio(c,t.background)>=4.5f);
}}
'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'check.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-I'+str(root/'.pio/build/nodemcuv2/generated'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
print('Custom theme checks passed: derived LCD colors readable on light/dark backgrounds; explicit colors preserved')
