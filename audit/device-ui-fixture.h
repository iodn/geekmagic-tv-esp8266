// Host canvas for the actual firmware layout functions and TFT font bitmaps.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>
using std::min;
using std::max;
class String : public std::string {
public:
    using std::string::string;
    String(const std::string &s) : std::string(s) {}
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    String(T n) : std::string(std::to_string(n)) {}
    String(float n, uint8_t precision) { char b[80]; snprintf(b, sizeof b, "%.*f", precision, n); assign(b); }
    bool isEmpty() const { return empty(); }
    int indexOf(char c) const { auto n=find(c); return n==npos ? -1 : int(n); }
    bool startsWith(const char *s) const { return rfind(s,0)==0; }
    bool endsWith(const char *s) const { return size()>=strlen(s) && compare(size()-strlen(s),strlen(s),s)==0; }
    char charAt(size_t i) const { return at(i); }
    void remove(size_t i) { erase(i); }
    void trim() { auto a=find_first_not_of(" \t\n"); if(a==npos){clear();return;} assign(substr(a,find_last_not_of(" \t\n")-a+1)); }
    void toLowerCase() { for(auto &c:*this)c=std::tolower(c); }
    void toUpperCase() { for(auto &c:*this)c=std::toupper(c); }
};
#define PROGMEM
#define F(x) x
#include "Fonts/glcdfont.c"
#include "Fonts/Font16.c"
#include "Fonts/Font32rle.c"
#include "Fonts/Font64rle.c"
#include "Fonts/Font7srle.c"
#include "dashboard.h"
#include "feeds.h"
constexpr int FONT_INFO=1, FONT_LABEL=2, FONT_BODY=4, FONT_TITLE=6, FONT_HUGE=7;
constexpr int TL_DATUM=0, TC_DATUM=1, TR_DATUM=2, MC_DATUM=4;
constexpr int DISPLAY_WIDTH=240, DISPLAY_HEIGHT=240;
constexpr uint32_t kFnvPrime=16777619UL,kFnvOffset=2166136261UL;
struct TextDraw {String text; int x,y,w,h,font,size;};
std::vector<TextDraw> textDraws;
bool collectDraws=true;
struct Canvas {
    int width=240,height=240,currentFont=1,textSize=1,datum=TL_DATUM;
    uint16_t foreground=0xffff,background=0;
    std::vector<uint16_t> pixels=std::vector<uint16_t>(240*240);
    void setTextSize(int n) {textSize=n;}
    void setTextFont(int n) {currentFont=n;}
    void setTextDatum(int n) {datum=n;}
    void setTextColor(uint16_t f,uint16_t b) {foreground=f;background=b;}
    void setTextPadding(int) {}
    int fontHeight(int f=0) const {if(!f)f=currentFont;return (f==1?8:f==2?16:f==4?26:48)*textSize;}
    int charWidth(unsigned char c,int f) const {
        if(f==1)return 6;
        if(c<32 || c>127)c=32;
        return (f==2?widtbl_f16:f==4?widtbl_f32:f==6?widtbl_f64:widtbl_f7s)[c-32];
    }
    int textWidth(const String&s,int f=0) const {if(!f)f=currentFont;int w=0;for(unsigned char c:s)w+=charWidth(c,f)*textSize;return w;}
    void pixel(int x,int y,uint16_t c) {if(x>=0&&x<width&&y>=0&&y<height)pixels[y*width+x]=c;}
    void fillRect(int x,int y,int w,int h,uint16_t c) {for(int yy=max(y,0);yy<min(y+h,height);++yy)for(int xx=max(x,0);xx<min(x+w,width);++xx)pixel(xx,yy,c);}
    int fullScreens=0;
    void fillScreen(uint16_t c) {++fullScreens;std::fill(pixels.begin(),pixels.end(),c);}
    void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c) {
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
            int dx=xx<r?r-xx-1:xx>=w-r?xx-(w-r):0;
            int dy=yy<r?r-yy-1:yy>=h-r?yy-(h-r):0;
            if(dx*dx+dy*dy<r*r)pixel(x+xx,y+yy,c);
        }
    }
    void drawRoundRect(int,int,int,int,int,uint16_t) {}
    void drawFastHLine(int x,int y,int w,uint16_t c){fillRect(x,y,w,1,c);}
    void drawFastVLine(int x,int y,int h,uint16_t c){fillRect(x,y,1,h,c);}
    void fillCircle(int x,int y,int r,uint16_t c){for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)if(xx*xx+yy*yy<=r*r)pixel(x+xx,y+yy,c);}
    void glyph(unsigned char c,int x,int y,int f) {
        int w=charWidth(c,f),h=fontHeight(f)/textSize;
        if(c<32 || c>127)c=32;
        if(f==1) {
            for(int yy=0;yy<8;++yy)for(int xx=0;xx<6;++xx)
                fillRect(x+xx*textSize,y+yy*textSize,textSize,textSize,xx<5 && (::font[c*5+xx]&(1<<yy))?foreground:background);
            return;
        }
        const unsigned char *bits=(f==2?chrtbl_f16:f==4?chrtbl_f32:f==6?chrtbl_f64:chrtbl_f7s)[c-32];
        if(f==2) {
            int stride=(w+6)/8;
            for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)
                fillRect(x+xx*textSize,y+yy*textSize,textSize,textSize,xx<stride*8 && (bits[yy*stride+xx/8]&(0x80>>(xx%8)))?foreground:background);
        } else {
            int position=0;
            while(position<w*h) {
                unsigned char run=*bits++;int n=(run&127)+1;
                for(int j=0;j<n && position<w*h;++j,++position)
                    fillRect(x+(position%w)*textSize,y+(position/w)*textSize,textSize,textSize,(run&128)?foreground:background);
            }
        }
    }
    void drawString(const String&s,int x,int y,int f) {
        int w=textWidth(s,f),h=fontHeight(f);
        if(datum==TC_DATUM||datum==MC_DATUM)x-=w/2;
        else if(datum==TR_DATUM)x-=w;
        if(datum==MC_DATUM)y-=h/2;
        if(collectDraws)textDraws.push_back({s,x,y,w,h,f,textSize});
        for(unsigned char c:s){glyph(c,x,y,f);x+=charWidth(c,f)*textSize;}
    }
    void save(const std::string&path) {
        std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(uint16_t c:pixels){char rgb[]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};out.write(rgb,3);}
    }
} tft;
struct Sprite : Canvas {
    uint16_t mappedForeground=0xffff,mappedBackground=0;
    std::vector<TextDraw> spriteDraws;
    Sprite(){width=216;height=64;pixels.resize(width*height);}
    void setBitmapColor(uint16_t f,uint16_t b){mappedForeground=f;mappedBackground=b;}
    void fillSprite(int c){fillScreen(c);spriteDraws.clear();}
    void drawString(const String&s,int x,int y,int f) {
        size_t before=textDraws.size();Canvas::drawString(s,x,y,f);
        if(collectDraws){spriteDraws.push_back(textDraws.back());textDraws.resize(before);}
    }
    bool pushSprite(int x,int y,int sx=0,int sy=0,int w=216,int h=64) {
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)tft.pixel(x+xx,y+yy,pixels[(sy+yy)*width+sx+xx]?mappedForeground:mappedBackground);
        for(auto draw:spriteDraws){draw.x+=x-sx;draw.y+=y-sy;textDraws.push_back(draw);}return true;
    }
} clockDynamicSprite;
DashboardConfig dashboardConfig{};
DashboardData dashboardData{};
FeedConfig feedConfig{};
FeedRuntimeState feedRuntime{};
struct {char ipInfo[24]="192.168.16.218";bool apMode=false;uint8_t currentPage=0;} displayState;
uint8_t selectedItemPage=DASHBOARD_PAGE_COUNT,selectedItemIndex=0;
bool spriteAvailable=true,emptyData=false;
uint32_t millis(){return 120000;}
bool feedsHomeAssistantConfigured(){return feedConfig.homeAssistant.enabled;}
bool feedsHasHomeAssistantData(){return feedRuntime.homeAssistant.hasData;}
bool feedsWeatherConfigured(){return true;}
bool feedsMarketConfigured(uint8_t i){return feedConfig.markets[i].source!=MARKET_FEED_DISABLED;}
uint8_t feedsWeatherSource(){return feedConfig.weather.source;}
uint8_t feedsMarketSource(uint8_t i){return feedConfig.markets[i].source;}
bool feedsWeatherUsesFahrenheit(){return feedConfig.weather.useFahrenheit;}
bool feedsHasWeatherData(){return feedRuntime.weather.hasData;}
bool feedsHasMarketData(uint8_t i){return feedRuntime.markets[i].hasData;}
const WeatherData *feedsWeatherData(){return feedRuntime.weather.hasData?&feedRuntime.weather.data:nullptr;}
const MarketData *feedsMarketData(uint8_t i){return feedRuntime.markets[i].hasData?&feedRuntime.markets[i].data:nullptr;}
const HomeAssistantSlotData *feedsHomeAssistantSlotData(uint8_t i){return &feedRuntime.homeAssistant.slots[i];}
bool homeAssistantSlotConfigured(uint8_t i){return feedConfig.homeAssistant.slots[i].enabled && feedConfig.homeAssistant.slots[i].entityId[0];}
String homeAssistantSlotLabel(uint8_t i){return feedConfig.homeAssistant.slots[i].label;}
String homeAssistantSlotUnit(uint8_t i){return feedConfig.homeAssistant.slots[i].unit;}
bool hasWeatherContent(){return !emptyData&&feedsHasWeatherData();}
bool hasMarketContent(){return !emptyData&&std::any_of(std::begin(feedRuntime.markets),std::end(feedRuntime.markets),[](auto&m){return m.hasData&&m.data.enabled;});}
bool hasHomeAssistantContent(){return !emptyData&&feedsHasHomeAssistantData();}
bool hasWorldClockContent(){return !emptyData;}
bool hasFocusContent(){return !emptyData;}
bool hasEventContent(){return !emptyData;}
bool hasQuoteContent(){return !emptyData;}
bool hasStatusContent(){return !emptyData;}
bool homeAssistantWaitingForSync(){return false;}
const char *activeClockMessage(){return nullptr;}
String safeCString(const char*s){return s?s:"";}
String localClockText="12:34";
bool localClockPm=false,timeSynced=true;
char footerClockCache[16]={};
String formatLocalTime(bool seconds,bool,bool *pm){*pm=localClockPm;return timeSynced ? localClockText+(seconds?":56":"") : "--:--";}
String formatLocalDate(){return "Wed 14 Oct 2026";}
String formatWorldTime(long offset){return offset==32400?"12:34":"08:34";}
bool hasTimeSync(){return timeSynced;}
int32_t dashboardFocusRemainingSeconds(){return 24*60+38;}
int32_t dashboardEventRemainingSeconds(){return 2*3600+3*60;}
bool ensureClockDynamicSprite(){return spriteAvailable;}
struct {bool valid=false;char metaLine[40]={};} clockMetaCache;
bool temporaryMessageVisible(){return false;}
constexpr int DISPLAY_MODE_DASHBOARD=0;
struct {bool valid=false;uint8_t mode=0,page=0,theme=0;uint32_t staticHash=0,dynamicHash=0;} renderCache;
uint8_t activeThemeIdValue(){return dashboardConfig.theme;}
uint32_t dashboardStaticHash(uint8_t){return 1;}
int appliedBrightness=70;
int beginPageTransition(int n){return n;}
void endPageTransition(int,int){}
void logPrint(const char*){}
int renders=0;
void invalidateRenderCache(){}
void displayUpdate();
uint8_t dashboardItemCount(uint8_t);
uint8_t dashboardItemSlot(uint8_t);
bool dashboardPageEnabled(uint8_t p){return dashboardConfig.enabledPages[p];}
uint8_t dashboardFirstEnabledPage(){return 0;}
