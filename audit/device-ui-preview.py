"""Run actual screen layouts with TFT font bitmaps; verify bounds and save previews."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('/tmp/geekmagic-device-ui'))
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = (root / 'src/display.cpp').read_text()

def function(name):
    match = re.search(r'^(?:std::vector<String>|String|bool|void|int|char|uint8_t|uint32_t|const WeatherData\*|const MarketData\*)\s*' + name + r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    prefix = 'template <typename TCanvas>\n' if name in ['chooseCanvasFittingFont', 'drawClockTime'] else ''
    if name == 'hashValue':
        prefix = 'template <typename TValue>\n'
    return match.start(), prefix + source[match.start():end]

names = '''trimTrailingZeros groupThousands formatDuration focusProgressWidth
formatRelativeCountdown formatUtcOffset formatClockMetaLine updateClockMetaCache
fontSupportsText chooseCanvasFittingFont drawClockTime headerSubtitle wrapText
drawWrappedCenteredText chooseFittingFont drawAdaptiveText fitDisplayLabel
drawDisplayLabel drawHeroText drawItemPosition footerClockText updateFooterClock drawScreenChrome drawPlaceholder
effectiveWeatherData weatherWaitingForSync weatherUnitSymbol formatWeatherTemperature
effectiveMarketData marketCurrencyPrefix formatMarketPriceValue formatMarketPrice
marketChangeWindowLabel marketWaitingForSync anyMarketsWaitingForSync
dashboardPageHasRenderableContent dashboardPageAvailable firstAvailableDashboardPage
nextAvailableDashboardPage dashboardItemEnabled dashboardItemLimit dashboardItemCount
normalizeDashboardItem dashboardItemSlot hashBytes hashValue hashCString hashMarketData
hashHomeAssistantData hashWorldClockData dashboardDynamicHash updateClockDynamicArea drawDynamicText
updateFocusDynamicArea updateWorldDynamicArea updateEventDynamicArea renderClockPage
renderWeatherPage renderMarketsPage drawHomeAssistantValue renderHomePage renderFocusPage
renderWorldPage renderEventPage renderQuotePage renderStatusPage renderDashboardPage
displayCycleNextPage pageUsesDynamicRefresh updateDashboardDynamicArea renderDashboardPageCached'''.split()
functions = sorted(function(name) for name in names)
palette = source[source.index('constexpr uint16_t rgb565'):source.index('int savedBrightness')]
code = '#include "device-ui-fixture.h"\n' + palette
code += '\nconst ThemePalette &activeTheme(){return kThemes[dashboardConfig.theme];}\n'
code += '\n'.join(body for _, body in functions)
code += r'''
void displayUpdate(){++renders;normalizeDashboardItem();renderDashboardPage();}
bool hasDraw(const std::string&s){for(auto&d:textDraws)if(d.text==s)return true;return false;}
void checkBounds(){
 for(auto &d:textDraws){
  if(d.x<0||d.y<0||d.x+d.w>240||d.y+d.h>240){fprintf(stderr,"Overflow: %s at %d,%d %dx%d\n",d.text.c_str(),d.x,d.y,d.w,d.h);abort();}
  assert(d.h>=16);
  if(d.y<212&&d.y+d.h>212){fprintf(stderr,"Footer boundary: %s at %d,%d %dx%d\n",d.text.c_str(),d.x,d.y,d.w,d.h);abort();}
 }
 assert(tft.textSize==1&&clockDynamicSprite.textSize==1);
}
void fixture(){
 dashboardConfig.showIp=true;dashboardConfig.use24Hour=true;
 for(auto &enabled:dashboardConfig.enabledPages)enabled=true;
 feedConfig.weather.source=WEATHER_FEED_OPEN_METEO;
 feedRuntime.weather.hasData=true;
 auto &w=feedRuntime.weather.data;strcpy(w.location,"Tokyo");strcpy(w.condition,"Partly cloudy");w.temperature=22;w.high=26;w.low=18;w.rainChance=20;
 const char *symbols[]={"BTC","MSFT","NVDA"},*labels[]={"Bitcoin","Microsoft","Nvidia"};
 const float prices[]={83827,543.45f,228.52f},changes[]={2.54f,-1.2f,0};
 for(int i=0;i<3;++i){feedConfig.markets[i].source=i==0?MARKET_FEED_COINGECKO:MARKET_FEED_FINNHUB;strcpy(feedConfig.markets[i].currency,"usd");auto&m=feedRuntime.markets[i];m.hasData=true;m.data.enabled=true;strcpy(m.data.symbol,symbols[i]);strcpy(m.data.label,labels[i]);m.data.price=prices[i];m.data.changePercent=changes[i];}
 feedConfig.homeAssistant.enabled=true;feedRuntime.homeAssistant.hasData=true;
 for(int i=0;i<4;++i){auto&c=feedConfig.homeAssistant.slots[i];c.enabled=true;strcpy(c.entityId,"sensor.temperature");strcpy(c.label,"Living room");strcpy(c.unit,"C");auto&v=feedRuntime.homeAssistant.slots[i];v.hasData=true;v.numeric=true;strcpy(v.state,"23.4");}
 auto &f=dashboardData.focus;strcpy(f.label,"Deep work");f.running=true;f.durationMinutes=25;
 auto &a=dashboardData.worldClocks[0],&b=dashboardData.worldClocks[1];a.enabled=b.enabled=true;strcpy(a.label,"Tokyo");a.offsetSeconds=32400;strcpy(b.label,"New York");b.offsetSeconds=-14400;
 strcpy(dashboardData.event.title,"Launch day");strcpy(dashboardData.event.subtitle,"Next milestone");
 strcpy(dashboardData.quote.text,"Make room for what matters.");strcpy(dashboardData.quote.author,"Daily reminder");
 strcpy(dashboardData.status.line1,"All systems online");strcpy(dashboardData.status.line2,"Have a good day");
}
int main(int argc,char**argv){
 assert(argc==2);fixture();
 for(int theme=0;theme<DASHBOARD_THEME_COUNT;++theme){dashboardConfig.theme=theme;
  for(int page=0;page<9;++page){displayState.currentPage=page;normalizeDashboardItem();textDraws.clear();renderDashboardPage();checkBounds();
   assert(hasDraw("12:34"));
   if(page!=DASHBOARD_PAGE_CLOCK){bool footer=false;for(auto &d:textDraws)if(d.text=="12:34"&&d.y>=212)footer=true;assert(footer);}
   tft.save(std::string(argv[1])+"/theme-"+std::to_string(theme)+"-page-"+std::to_string(page)+".ppm");
   if(page==DASHBOARD_PAGE_MARKETS){assert(hasDraw("Bitcoin")&&!hasDraw("Microsoft"));for(auto&d:textDraws)if(d.text=="+2.54%")assert(d.h>=52);}
   if(page==DASHBOARD_PAGE_WEATHER)for(auto&d:textDraws)if(d.text=="22C")assert(d.h>=78);
  }
 }
 // The local clock updates independently of feed data and retains its placement.
 displayState.currentPage=DASHBOARD_PAGE_MARKETS;dashboardConfig.use24Hour=true;
 textDraws.clear();drawScreenChrome("Markets");assert(hasDraw("12:34"));
 renderCache.valid=false;renderDashboardPageCached();int fullScreens=tft.fullScreens;
 textDraws.clear();renderDashboardPageCached();assert(textDraws.empty());
 localClockText="12:35";renderDashboardPageCached();assert(textDraws.size()==1&&hasDraw("12:35"));assert(tft.fullScreens==fullScreens);
 assert(textDraws[0].x==8&&textDraws[0].y>=212&&textDraws[0].w<=84);checkBounds();
 dashboardConfig.use24Hour=false;localClockPm=true;textDraws.clear();updateFooterClock();assert(hasDraw("12:35 PM"));checkBounds();
 localClockPm=false;textDraws.clear();updateFooterClock();assert(hasDraw("12:35 AM"));checkBounds();
 timeSynced=false;textDraws.clear();updateFooterClock();assert(hasDraw("--:--"));checkBounds();
 timeSynced=true;localClockText="12:34";dashboardConfig.use24Hour=true;
 // Every available item is visited before advancing sections, including a single-section setup.
 for(auto &e:dashboardConfig.enabledPages)e=false;dashboardConfig.enabledPages[DASHBOARD_PAGE_MARKETS]=true;
 displayState.currentPage=DASHBOARD_PAGE_MARKETS;normalizeDashboardItem();selectedItemIndex=0;
 for(int expected:{1,2,0}){displayCycleNextPage(false);assert(displayState.currentPage==DASHBOARD_PAGE_MARKETS&&selectedItemIndex==expected);}
 uint32_t before=2166136261;hashMarketData(before);
 feedRuntime.markets[1].data.price+=100;uint32_t after=2166136261;hashMarketData(after);assert(before==after);
 feedRuntime.markets[0].data.price+=100;after=2166136261;hashMarketData(after);assert(before!=after);
 feedRuntime.markets[0].data.enabled=false;feedRuntime.markets[1].data.enabled=false;selectedItemIndex=2;normalizeDashboardItem();assert(selectedItemIndex==0&&dashboardItemSlot(DASHBOARD_PAGE_MARKETS)==2);
 displayState.currentPage=DASHBOARD_PAGE_HOME;normalizeDashboardItem();feedConfig.homeAssistant.slots[0].enabled=false;feedConfig.homeAssistant.slots[2].enabled=false;
 assert(dashboardItemCount(DASHBOARD_PAGE_HOME)==2);assert(dashboardItemSlot(DASHBOARD_PAGE_HOME)==1);selectedItemIndex=1;assert(dashboardItemSlot(DASHBOARD_PAGE_HOME)==3);
 displayState.currentPage=DASHBOARD_PAGE_WORLD;normalizeDashboardItem();
 uint32_t worldBefore=dashboardDynamicHash(DASHBOARD_PAGE_WORLD);
 dashboardData.worldClocks[1].offsetSeconds=32400;assert(dashboardDynamicHash(DASHBOARD_PAGE_WORLD)==worldBefore);
 dashboardData.worldClocks[0].offsetSeconds=0;assert(dashboardDynamicHash(DASHBOARD_PAGE_WORLD)!=worldBefore);
 // Empty, long, negative and stale values stay within the panel at readable font sizes.
 fixture();dashboardConfig.theme=0;strcpy(displayState.ipInfo,"255.255.255.255");
 strcpy(feedRuntime.weather.data.location,"A very long city name");feedRuntime.weather.data.temperature=-148;feedConfig.weather.useFahrenheit=true;
 strcpy(feedConfig.markets[0].currency,"nzd");feedRuntime.markets[0].data.price=999999999;feedRuntime.markets[0].data.changePercent=-123.45f;
 memset(dashboardData.quote.text,'W',sizeof(dashboardData.quote.text)-1);
 memset(dashboardData.event.title,'W',sizeof(dashboardData.event.title)-1);memset(dashboardData.event.subtitle,'W',sizeof(dashboardData.event.subtitle)-1);
 memset(dashboardData.status.line1,'W',sizeof(dashboardData.status.line1)-1);memset(dashboardData.status.line2,'W',sizeof(dashboardData.status.line2)-1);
 auto &slot=feedRuntime.homeAssistant.slots[0];slot.stale=true;slot.lastSuccessMs=60000;
 for(int page=0;page<9;++page){displayState.currentPage=page;normalizeDashboardItem();for(bool buffer:{true,false}){spriteAvailable=buffer;textDraws.clear();renderDashboardPage();checkBounds();}}
 displayState.currentPage=DASHBOARD_PAGE_HOME;normalizeDashboardItem();selectedItemIndex=0;
 textDraws.clear();renderHomePage();assert(hasDraw("Stale 1m"));
 slot.hasData=false;strcpy(slot.lastError,"HTTP 404");textDraws.clear();renderHomePage();assert(hasDraw("Error"));checkBounds();
 for(auto &d:textDraws)if(d.text=="Error"&&d.y<160)assert(d.h>=52);
 dashboardConfig.showSeconds=true;dashboardConfig.use24Hour=false;displayState.currentPage=DASHBOARD_PAGE_CLOCK;
 normalizeDashboardItem();textDraws.clear();renderClockPage();checkBounds();
 emptyData=true;for(int page=0;page<9;++page){displayState.currentPage=page;normalizeDashboardItem();textDraws.clear();renderDashboardPage();checkBounds();}
 puts("Device layouts passed: exact TFT fonts, bounds, readable sizes, item cycling, hidden-feed hashes, long/stale/empty values and buffer fallback");
}
'''
with tempfile.TemporaryDirectory() as folder:
    temp = Path(folder)
    (temp / 'Arduino.h').write_text('#pragma once\n#include <cstdint>\n')
    (temp / 'layout.cpp').write_text(code)
    subprocess.run(['g++', '-std=c++17', '-O1', '-I'+str(temp), '-I'+str(root/'audit'),
                    '-I'+str(root/'src'), '-I'+str(root/'.pio/libdeps/nodemcuv2/TFT_eSPI'), '-I'+str(root/'.pio/build/nodemcuv2/generated'),
                    str(temp/'layout.cpp'), '-o', str(temp/'layout')], check=True)
    subprocess.run([str(temp/'layout'), str(args.output)], check=True)
palettes = __import__("json").loads((root / "src/device-themes.json").read_text())["palettes"]
for theme in range(len(palettes)):
    board = Image.new('RGB', (752, 800), '#12151b')
    draw = ImageDraw.Draw(board)
    titles = ['Clock','Weather','Markets','Home','Focus','World','Event','Quote','Status']
    for page, title in enumerate(titles):
        x, y = 8 + (page % 3) * 248, 28 + (page // 3) * 260
        draw.text((x, y-16), title, fill='white')
        board.paste(Image.open(args.output/f'theme-{theme}-page-{page}.ppm'), (x,y))
    board.save(args.output/f'device-ui-theme-{theme}.png')
print('Exact-font preview boards:', args.output)

gallery = Image.new('RGB', (1008, 1088), '#12151b')
gallery_draw = ImageDraw.Draw(gallery)
for theme, palette in enumerate(palettes):
    x, y = 8 + (theme % 4) * 250, 28 + (theme // 4) * 270
    gallery_draw.text((x, y-18), palette['label'] + ' / ' + palette['mode'], fill='white')
    gallery.paste(Image.open(args.output / f'theme-{theme}-page-2.ppm'), (x, y))
gallery.save(args.output / 'device-theme-gallery.png')
