"""Exercise real wrapping and page layout with maximal text and mock font metrics."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/display.cpp').read_text()
def extract(name):
    match=re.search(r'^(?:std::vector<String>|String|void) '+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert match,name
    start=source.index('{',match.start());end=start+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
code=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
using std::min;
class String:public std::string {
 public:using std::string::string;String(const std::string &s):std::string(s){}
 bool endsWith(const char *suffix)const{size_t n=strlen(suffix);return size()>=n && compare(size()-n,n,suffix)==0;}
 bool isEmpty()const{return empty();}char charAt(size_t i)const{return at(i);}void remove(size_t i){erase(i);}
};
constexpr int FONT_INFO=1,FONT_LABEL=2,FONT_BODY=4,FONT_TITLE=6,TC_DATUM=1,MC_DATUM=2,DISPLAY_HEIGHT=240;
struct Draw {String text;int y,height,width;};
struct {
 int font=1;std::vector<Draw> draws;
 void setTextFont(int f){font=f;}void setTextDatum(int){}void setTextColor(uint16_t,uint16_t){}
 int fontHeight(){return font==4?26:font==2?16:8;}
 int textWidth(const String &s,int f=0){if(!f)f=font;return int(s.length())*(f==4?14:f==2?8:6);}
 void drawString(const String &s,int,int y,int f){int old=font;font=f;draws.push_back({s,y,fontHeight(),textWidth(s)});font=old;}
 void fillRoundRect(int,int,int,int,int,uint16_t){}
} tft;
struct ThemePalette {uint16_t text=1,muted=2,surface=3,surfaceAlt=4,accentSoft=5,accent=6,background=7;} theme;
const auto &activeTheme(){return theme;}
struct {struct {char title[64],subtitle[96];} event;struct {char text[256],author[64];} quote;struct {char line1[96],line2[96];} status;} dashboardData{};
bool hasEventContent(){return true;}bool hasQuoteContent(){return true;}bool hasStatusContent(){return true;}
void drawPlaceholder(const char*,const char*){}void drawScreenChrome(const char*){}
void drawRoundedPanel(int,int,int,int,uint16_t,uint16_t){}void drawDividerLine(int,int,int,uint16_t){}
void updateEventDynamicArea(){}
String safeCString(const char *s){return s;}
void drawAdaptiveText(const String &,int,int,int,int,const int*,size_t,uint16_t,uint16_t){}
'''
for name in ['wrapText','drawWrappedCenteredText','fitDisplayLabel','drawDisplayLabel','renderEventPage','renderQuotePage','renderStatusPage']:
    code+='\n'+extract(name)
code+=r'''
int main(){
 auto words=wrapText("one two three",FONT_INFO,42);assert(words.size()==2 && words[0]=="one two" && words[1]=="three");
 auto explicitLines=wrapText("first\n\nlast",FONT_INFO,186);assert(explicitLines.size()==3 && explicitLines[1].empty());
 auto longWord=wrapText(String(100,'W'),FONT_LABEL,186);
 size_t characters=0;for(const auto &line:longWord){assert(tft.textWidth(line,FONT_LABEL)<=186);characters+=line.length();}assert(characters==100);
 drawWrappedCenteredText(String(256,'W'),120,104,186,FONT_LABEL,1,3,4,76);
 assert(tft.draws.size()==4);for(const auto &draw:tft.draws){assert(draw.y>=104 && draw.y+draw.height<=180 && draw.width<=186);}
 assert(tft.draws.back().text.endsWith("..."));
 tft.draws.clear();drawWrappedCenteredText("too tall",120,238,186,FONT_LABEL,1,3);assert(tft.draws.empty());
 memset(dashboardData.event.title,'W',sizeof(dashboardData.event.title)-1);
 memset(dashboardData.event.subtitle,'W',sizeof(dashboardData.event.subtitle)-1);
 renderEventPage();for(const auto &draw:tft.draws){assert(draw.width<=224);if(draw.y<98)assert(draw.y>=16 && draw.y+draw.height<=76);else assert(draw.y>=174 && draw.y+draw.height<=210);}
 assert(!tft.draws.empty());
 tft.draws.clear();memset(dashboardData.quote.text,'W',sizeof(dashboardData.quote.text)-1);strcpy(dashboardData.quote.author,"Author");
 renderQuotePage();for(const auto &draw:tft.draws){assert(draw.width<=224 && draw.y+draw.height<=216);}
 assert(tft.draws[tft.draws.size()-2].text.endsWith("..."));
 tft.draws.clear();memset(dashboardData.status.line1,'W',sizeof(dashboardData.status.line1)-1);memset(dashboardData.status.line2,'W',sizeof(dashboardData.status.line2)-1);
 renderStatusPage();for(const auto &draw:tft.draws){assert(draw.width<=224);if(draw.y<130)assert(draw.y>=30 && draw.y+draw.height<=106);else assert(draw.y>=130 && draw.y+draw.height<=206);}
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Wrapped display checks passed: word splits, line breaks, ellipsis, event/quote/status bounds')
