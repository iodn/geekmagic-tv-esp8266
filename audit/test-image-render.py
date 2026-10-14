"""Exercise actual image rendering/cache behavior across storage and decoder errors."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/display.cpp').read_text()
def extract(name):
    match=re.search(r'^(?:bool|void) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert match,name
    start=source.index('{',match.start());end=start+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
using std::min;
#define F(x) x
using JRESULT=int;
constexpr int JDR_OK=0,JDR_INP=1,DISPLAY_PATH_BUFFER_SIZE=64,DISPLAY_WIDTH=240,DISPLAY_HEIGHT=240,FONT_BODY=4,TFT_BLACK=0;
class String:public std::string {public:using std::string::string;String(const std::string&s):std::string(s){}String(int value):std::string(std::to_string(value)){} };
struct {bool showImage=true,apMode=false;char imagePath[64]="/images/test.jpg";} displayState;
struct {char message[96]={};} temporaryMessage;
struct ThemePalette {uint16_t surface=1,surfaceAlt=2,text=3,background=4;} theme;
const ThemePalette &activeTheme(){return theme;}
bool temporaryMessageVisible(){return false;}bool temporaryMessageExpired(){return false;}
void clearTemporaryMessage(){}uint32_t temporaryMessageHash(){return 0;}uint32_t apScreenHash(){return 0;}uint8_t activeThemeIdValue(){return 0;}
void drawScreenChrome(const char*){}void drawRoundedPanel(int,int,int,int,uint16_t,uint16_t){}
void drawWrappedCenteredText(const char*,int,int,int,int,uint16_t,uint16_t,int,int){}
void displayRenderAPMode(){}int dashboardRenders=0;void renderDashboardPageCached(){++dashboardRenders;}
uint32_t now=100;uint32_t millis(){return now;}
'''
code+=re.search(r'enum DisplayRenderMode : uint8_t \{[\s\S]*?\};',source).group(0)
code+=re.search(r'struct RenderCache \{[\s\S]*?\};',source).group(0)
code+=r'''
RenderCache renderCache{};uint32_t lastImageAttemptMs=0;
void invalidateRenderCache(){renderCache.valid=false;}
int messages=0,closes=0;String message;
void displayShowMessage(const String &text){++messages;message=text;invalidateRenderCache();}
struct File {bool valid;explicit operator bool()const{return valid;}void close(){++closes;}};
struct {bool present=false,opensSuccessfully=false;int existsCalls=0,opens=0;
 bool exists(const char*){++existsCalls;return present;}File open(const char*,const char*){++opens;return {opensSuccessfully};}
} LittleFS;
int yields=0;void yield(){++yields;}
struct {int depth=0,pushes=0;std::vector<std::array<int,5>> margins;
 int width(){return 240;}int height(){return 240;}
 void pushImage(int,int,uint16_t,uint16_t,uint16_t*){++pushes;}
 void fillRect(int x,int y,int w,int h,uint16_t color){assert(x>=0 && y>=0 && x+w<=240 && y+h<=240);margins.push_back({x,y,w,h,color});}
 void startWrite(){++depth;}void endWrite(){--depth;}
} tft;
struct {int result=JDR_INP,draws=0,headerResult=JDR_OK,scale=1,decodeScale=0;uint16_t width=240,height=240;
 void setJpgScale(int value){scale=value;}
 template<class FS> int getFsJpgSize(uint16_t *w,uint16_t *h,const char*,FS&){*w=width;*h=height;return headerResult;}
 int drawFsJpg(int,int,File&){++draws;decodeScale=scale;assert(tft.depth==1);return result;}
} TJpgDec;
'''
for name in ['tftOutput','displayRenderImage','displayUpdate','displayInvalidateImage']:
    code+='\n'+extract(name)
code+=r'''
int main(){
 displayUpdate();assert(messages==1 && message=="Image not found");assert(renderCache.mode==DISPLAY_MODE_IMAGE_ERROR);
 now+=4999;displayUpdate();assert(messages==1 && LittleFS.existsCalls==1);
 now++;LittleFS.present=true;displayUpdate();assert(messages==2 && LittleFS.opens==1);
 assert(renderCache.mode==DISPLAY_MODE_IMAGE_ERROR);
 now+=5000;LittleFS.opensSuccessfully=true;displayUpdate();assert(messages==3 && TJpgDec.draws==1);
 assert(renderCache.mode==DISPLAY_MODE_IMAGE_ERROR && tft.depth==0 && closes==1);
 now+=5000;TJpgDec.result=JDR_OK;displayUpdate();assert(TJpgDec.draws==2 && renderCache.mode==DISPLAY_MODE_IMAGE);
 now+=30000;displayUpdate();assert(TJpgDec.draws==2 && messages==3);
 displayInvalidateImage(displayState.imagePath);displayUpdate();assert(TJpgDec.draws==3);
 // Changing paths bypasses the previous error's retry delay.
 TJpgDec.result=JDR_INP;displayInvalidateImage(displayState.imagePath);displayUpdate();assert(TJpgDec.draws==4);
 strcpy(displayState.imagePath,"/images/new.jpg");TJpgDec.result=JDR_OK;displayUpdate();assert(TJpgDec.draws==5);
 // Error retry remains bounded and resumes across millis wrap.
 renderCache.valid=false;now=0xfffff800U;TJpgDec.result=JDR_INP;displayUpdate();assert(TJpgDec.draws==6);
 now+=4999U;displayUpdate();assert(TJpgDec.draws==6);now++;TJpgDec.result=JDR_OK;displayUpdate();assert(TJpgDec.draws==7);
 tft.margins.clear();TJpgDec.width=400;TJpgDec.height=200;renderCache.valid=false;
 displayUpdate();assert(TJpgDec.decodeScale==2 && TJpgDec.scale==1);
 assert(tft.margins.size()==2);
 assert((tft.margins[0]==std::array<int,5>{200,0,40,240,0}));
 assert((tft.margins[1]==std::array<int,5>{0,100,200,140,0}));
 TJpgDec.width=1920;TJpgDec.height=1920;tft.margins.clear();renderCache.valid=false;displayUpdate();
 assert(TJpgDec.decodeScale==8 && TJpgDec.scale==1 && tft.margins.empty());
 TJpgDec.width=2048;TJpgDec.height=2048;renderCache.valid=false;displayUpdate();assert(TJpgDec.decodeScale==8);
 TJpgDec.width=31;TJpgDec.height=25;tft.margins.clear();renderCache.valid=false;displayUpdate();
 assert(TJpgDec.decodeScale==1 && tft.margins[0][0]==31 && tft.margins[1][1]==25);
 int before=TJpgDec.draws;TJpgDec.headerResult=JDR_INP;renderCache.valid=false;displayUpdate();
 assert(TJpgDec.draws==before && renderCache.mode==DISPLAY_MODE_IMAGE_ERROR && tft.depth==0);
 assert(tftOutput(240,0,16,16,nullptr));assert(tftOutput(0,240,16,16,nullptr));
 assert(tftOutput(224,224,16,16,nullptr));assert(yields==3 && tft.pushes==1);
 displayState.showImage=false;displayUpdate();assert(dashboardRenders==1);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Image render checks passed: missing/open/decode errors, bounded retries, recovery, invalidation, millis wrap')
