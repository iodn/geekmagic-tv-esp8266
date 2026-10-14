"""Host regression checks for display formatting, font bounds, and cache lifecycle."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/display.cpp").read_text()


def function(name):
    match = re.search(r"^(?:String|bool|void|int) " + name + r"\([^;{]*\)\s*\{", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using StringBase = std::string;
struct String : StringBase {
 using StringBase::StringBase;
 String(float value, uint8_t precision){ char b[80];snprintf(b,sizeof b,"%.*f",precision,value);assign(b); }
 int indexOf(char c) const {auto n=find(c);return n==npos ? -1 : int(n);}
 bool endsWith(const char *s) const {return size()>=strlen(s) && compare(size()-strlen(s),strlen(s),s)==0;}
 void remove(size_t n){erase(n);}
};
constexpr int FONT_INFO=1,FONT_LABEL=2,FONT_BODY=4,FONT_TITLE=6,FONT_HUGE=7,DISPLAY_HEIGHT=240;
struct Canvas {
 int clearHeight=0;
 void fillRect(int,int,int,int h,uint16_t){clearHeight=h;}
 int textWidth(const String &s,int font){return s.length()*(font==6 ? 27 : font==7 ? 32 : font==4 ? 16 : font==2 ? 8 : 6);}
 int currentFont=1,lastY=0,lastFont=0;String lastText;uint16_t lastColor=0;
 void setTextDatum(int){} void setTextFont(int font){currentFont=font;}
 void setTextColor(uint16_t color,uint16_t){lastColor=color;}
 void drawString(const String &text,int,int y,int font){lastY=y;lastFont=font;lastText=text;}
 int fontHeight(int font){return font==6 ? 48 : font==7 ? 48 : font==4 ? 26 : font==2 ? 16 : 8;}
} tft;
struct {char imagePath[64];} displayState;
int invalidations=0,updates=0;
void invalidateRenderCache(){++invalidations;}
bool expired=false;
bool temporaryMessageExpired(){return expired;}
void displayUpdate(){++updates;}
'''
for name in ("trimTrailingZeros", "fontSupportsText", "chooseCanvasFittingFont", "chooseFittingFont", "displayInvalidateImage", "displayServiceTemporaryMessage"):
    if name == "chooseCanvasFittingFont":
        code += "template <typename TCanvas>\n"
    code += function(name) + "\n"
code += r'''
constexpr int TC_DATUM=1,TL_DATUM=2;
struct ThemePalette {uint16_t text=1,muted=2,accent=3,accentSoft=4,warning=5,surface=6;} theme;
const ThemePalette &activeTheme(){return theme;}
struct HomeAssistantSlotData {bool hasData=true,numeric=true,stale=false;char state[24]="123",lastError[64]={};uint32_t lastSuccessMs=0;} slot;
const HomeAssistantSlotData *feedsHomeAssistantSlotData(uint8_t){return &slot;}
String homeAssistantSlotLabel(uint8_t){return "Temperature";}
String homeAssistantSlotUnit(uint8_t){return "C";}
String safeCString(const char *s){return s;}
bool feedsHomeAssistantConfigured(){return true;}
bool homeAssistantSlotConfigured(uint8_t){return true;}
bool homeAssistantWaitingForSync(){return false;}
uint32_t millis(){return 120000;}
void drawRoundedPanel(int,int,int,int,uint16_t,uint16_t){}
String lastAdaptiveText;
void drawAdaptiveText(const String &text,int,int,int,uint8_t,const int *,size_t,uint16_t,uint16_t){lastAdaptiveText=text;}
void drawAdaptiveBadge(int,int,int,int,const String &,const int *,size_t,uint16_t,uint16_t){}
struct WorldClockData {bool enabled=true;char label[16]="Tokyo";long offsetSeconds=0;};
constexpr int DASHBOARD_WORLD_CLOCK_COUNT=2;
struct {WorldClockData worldClocks[2];} dashboardData;
String formatWorldTime(long){return "23:59";}
using std::max;
'''
# Adapt the Arduino integer String constructor and rounded accent primitive for host mocks.
code = code.replace(" using StringBase::StringBase;", " using StringBase::StringBase;\n String(uint32_t n){assign(std::to_string(n));}")
code = code.replace(" void setTextDatum(int)", " void fillRoundRect(int,int,int,int,int,uint16_t){}\n void setTextDatum(int)")
code += function("drawHomeAssistantCard") + "\n"
code += function("updateWorldDynamicArea") + "\n"
code += r'''
int yields=0;
void yield(){++yields;}
struct File {
 bool valid=true;size_t position=0;
 std::string bytes=std::string("\xff\xd8\x01\x02\xff\xd9",6);
 explicit operator bool() const{return valid;}
 size_t size(){return bytes.size();}
 int read(){return position<bytes.size() ? static_cast<unsigned char>(bytes[position++]) : -1;}
 bool seek(size_t target){if(target>bytes.size())return false;position=target;return true;}
 void close(){}
};
struct {bool present=true;File file;bool exists(const char *){return present;} File open(const char *,const char *){return file;}} LittleFS;
constexpr int JDR_OK=0,JDR_INP=1;
bool tftOutput(int16_t,int16_t,uint16_t,uint16_t,uint16_t *){assert(false && "validation must not draw");return false;}
using Callback=bool (*)(int16_t,int16_t,uint16_t,uint16_t,uint16_t *);
struct Decoder {
 Callback callback=tftOutput;
 int headerResult=JDR_OK,decodeResult=JDR_OK,drawCalls=0;
 uint16_t width=240,height=240;
 void setCallback(Callback value){callback=value;}
 template<typename FS> int getFsJpgSize(uint16_t *w,uint16_t *h,const char *,FS &){*w=width;*h=height;return headerResult;}
 template<typename FS> int drawFsJpg(int,int,const char *,FS &){++drawCalls;assert(callback(0,0,8,8,nullptr));return decodeResult;}
} TJpgDec;
'''
code += function("displayValidateImage") + "\n"
progress = re.search(r"uint32_t elapsedSeconds = (.*);", source).group(1)
code += "uint32_t progress(uint32_t boundedRemaining,uint32_t totalSeconds){return " + progress + ";}\n"
code += r'''
int main(){
 assert(!displayValidateImage(nullptr));assert(TJpgDec.drawCalls==0);
 LittleFS.present=false;assert(!displayValidateImage("test.jpg"));LittleFS.present=true;
 std::string validJpeg=LittleFS.file.bytes;
 for(std::string invalid : {std::string("bad"),std::string("\xff\xd8",2),validJpeg.substr(0,validJpeg.size()-1),validJpeg.substr(0,validJpeg.size()-2),std::string("\x00\x00\xff\xd9",4)}) {
  LittleFS.file.bytes=invalid;assert(!displayValidateImage("test.jpg"));assert(TJpgDec.drawCalls==0);
 }
 LittleFS.file.bytes=validJpeg;
 LittleFS.file.valid=false;assert(!displayValidateImage("test.jpg"));LittleFS.file.valid=true;
 TJpgDec.headerResult=JDR_INP;assert(!displayValidateImage("test.jpg"));assert(TJpgDec.drawCalls==0);TJpgDec.headerResult=JDR_OK;
 TJpgDec.width=0;assert(!displayValidateImage("test.jpg"));assert(TJpgDec.drawCalls==0);TJpgDec.width=240;
 assert(displayValidateImage("test.jpg"));assert(TJpgDec.callback==tftOutput);assert(yields==1);
 TJpgDec.decodeResult=JDR_INP;assert(!displayValidateImage("test.jpg"));assert(TJpgDec.callback==tftOutput);assert(yields==2);
 assert(trimTrailingZeros(1000,0)=="1000");
 assert(trimTrailingZeros(1500,0)=="1500");
 assert(trimTrailingZeros(10000,0)=="10000");
 assert(trimTrailingZeros(-1500,0)=="-1500");
 assert(trimTrailingZeros(0,0)=="0");
 assert(trimTrailingZeros(0,2)=="0");
 assert(trimTrailingZeros(10.50f,2)=="10.5");
 const int fonts[]={7,6,4,2,1};
 for(const char *text : {"21C","70F","in 2h 3m","Now","$100","1,500","\"","Home"}) {
   int font=chooseFittingFont(text,200,fonts,5);
   assert(font!=6 && font!=7);
 }
 assert(chooseFittingFont("12:34",220,fonts,5)==7);
 assert(chooseFittingFont("123",200,fonts,5,16)==2);
 assert(chooseFittingFont("123",200,fonts,5,8)==1);
 assert(progress(1500,1500)==0);
 assert(progress(1600,1500)==0);
 assert(progress(1499,1500)==1);
 assert(progress(0,1500)==1500);
 for(int height : {70,80,160}) {
   drawHomeAssistantCard(16,48,208,height,0,0,0);
   assert(tft.lastY+tft.fontHeight(tft.lastFont)<=48+height-14);
   assert(tft.fontHeight(tft.lastFont)<= (height==70 ? 16 : height==80 ? 26 : 48));
 }
 slot.hasData=false;slot.stale=true;strcpy(slot.lastError,"HTTP 404");
 drawHomeAssistantCard(16,48,208,70,0,0,0);
 assert(tft.lastText=="Error" && tft.lastColor==theme.warning);assert(lastAdaptiveText=="Error");
 slot.hasData=true;slot.lastSuccessMs=60000;
 drawHomeAssistantCard(16,48,208,70,0,0,0);
 assert(tft.lastText=="123" && lastAdaptiveText=="Stale 1m");
 slot.stale=false;slot.lastError[0]='\0';
 strcpy(displayState.imagePath,"/images/test.jpg");
 displayInvalidateImage("/images/other.jpg"); assert(invalidations==0);
 displayInvalidateImage(nullptr); assert(invalidations==0);
 displayInvalidateImage("/images/test.jpg"); assert(invalidations==1);
 displayServiceTemporaryMessage(); assert(updates==0);
 expired=true; displayServiceTemporaryMessage(); assert(updates==1);
 updateWorldDynamicArea();
 assert(tft.lastY==142);
 assert(tft.textWidth(tft.lastText,tft.lastFont)<=118);
 assert(tft.fontHeight(tft.lastFont)<=tft.clearHeight);
 assert(tft.lastY+tft.clearHeight<=220);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / "check.cpp").write_text(code)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(path / "check.cpp"), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
# Sprite color indexes must be paired with explicit RGB565 push colors.
assert "clockDynamicSprite.setColorDepth(1);" in source
assert "clockDynamicSprite.setBitmapColor(theme.text, dynamicBackground);" in source
assert "clockDynamicSprite.fillSprite(0);" in source
assert re.search(r"drawClockTime\(clockDynamicSprite,\s*timeText,\s*dashboardConfig.showSeconds,\s*1,\s*1,\s*0,", source)
print("Display regressions passed: numeric formatting, glyph coverage, height bounds, progress, image invalidation, overlay expiry, sprite mapping")
