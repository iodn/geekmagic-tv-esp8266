"""Parse thousands of CoinGecko matches with bounded memory and the original ranking."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/feeds.cpp').read_text()
def extract(name):
    m=re.search(r'^(?:String|void|int|DeserializationError) '+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert m,name
    i=source.index('{',m.start())+1;depth=1
    masked=re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',lambda m:' '*len(m.group(0)),source)
    while depth:
        depth+=(masked[i]=='{')-(masked[i]=='}');i+=1
    return source[m.start():i]
code=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <climits>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>
#include <map>
static std::map<void*,size_t> sizes;
static size_t allocated=0,peak=0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void*,size_t);
extern "C" void __real_free(void*);
void track(void *p,size_t n){if(p){sizes[p]=n;allocated+=n;if(allocated>peak)peak=allocated;}}
extern "C" void *__wrap_malloc(size_t n){void *p=__real_malloc(n);track(p,n);return p;}
extern "C" void __wrap_free(void *p){auto i=sizes.find(p);if(i!=sizes.end()){allocated-=i->second;sizes.erase(i);}__real_free(p);}
extern "C" void *__wrap_realloc(void *p,size_t n){auto i=sizes.find(p);size_t old=i==sizes.end()?0:i->second;void *q=__real_realloc(p,n);if(q){allocated-=old;sizes.erase(p);track(q,n);}return q;}
#include <ArduinoJson.h>
struct String:std::string {
 using std::string::string;String(const std::string&s):std::string(s){}
 void trim(){auto a=find_first_not_of(" \r\n\t");if(a==npos){clear();return;}erase(0,a);erase(find_last_not_of(" \r\n\t")+1);}
 void toLowerCase(){for(char &c:*this)c=std::tolower(static_cast<unsigned char>(c));}
 void replace(const char *a,const char *b){size_t pos=0;while((pos=find(a,pos))!=npos){std::string::replace(pos,strlen(a),b);pos+=strlen(b);}}
 bool startsWith(const String&s)const{return rfind(s,0)==0;}
 int indexOf(const String&s)const{auto p=find(s);return p==npos?-1:int(p);}
};
struct Stream {
 virtual ~Stream()=default;
 virtual int available()=0;virtual int peek()=0;virtual int read()=0;
 virtual size_t write(uint8_t)=0;
 virtual size_t readBytes(char *b,size_t n){size_t got=0;int c;while(got<n&&(c=read())>=0)b[got++]=c;return got;}
 bool find(const char *s){size_t n=0;int c;while((c=read())>=0){n=c==s[n]?n+1:0;if(!s[n])return true;}return false;}
};
struct Input:Stream {
 std::string text;size_t pos=0;Input(std::string s):text(s){}
 int available()override{return text.size()-pos;}
 int peek()override{return pos<text.size()?static_cast<unsigned char>(text[pos]):-1;}
 int read()override{int c=peek();if(c>=0)++pos;return c;}
 size_t write(uint8_t)override{return 0;}
};
'''
for name in ('trimmedLowercaseString','normalizeCoinGeckoLookupValue','buildCoinGeckoSearchFilter','coinGeckoCandidateScore','readCoinGeckoSearch'):
    code+=extract(name)+'\n'
code+=r'''
int main(){
 std::string coins;
 for(int i=0;i<5000;i++){
  if(i)coins+=",";
  coins+="{\"id\":\"btc-other-"+std::to_string(i)+"\",\"name\":\"Other asset\",\"symbol\":\"btc\",\"market_cap_rank\":"+std::to_string(5001-i)+",\"ignored\":\"not retained\"}";
 }
 coins+=",{\"id\":\"bitcoin\",\"name\":\"Bitcoin\",\"symbol\":\"btc\",\"market_cap_rank\":1}";
 // Reproduce the old full-array allocation, then check the same results streamed.
 std::string payload="{\"coins\" : ["+coins+"],\"exchanges\":[]}";
 {JsonDocument all,filter;buildCoinGeckoSearchFilter(filter);Input input(payload);assert(!deserializeJson(all,input,DeserializationOption::Filter(filter.as<JsonVariantConst>())));assert(peak>100000);std::printf("Full-array JSON peak: %zu bytes\n",peak);}
 assert(allocated==0);peak=0;
 {JsonDocument best;Input input(payload);assert(!readCoinGeckoSearch(input,best,"btc"));assert(best["id"]=="bitcoin");assert(best["ignored"].isNull());assert(peak<10000);std::printf("Streamed JSON peak: %zu bytes\n",peak);}
 assert(allocated==0);peak=0;
 {JsonDocument best;Input input("{\"coins\":[{\"id\":\"btc\",\"symbol\":\"other\",\"market_cap_rank\":999},{\"id\":\"bitcoin\",\"symbol\":\"btc\",\"market_cap_rank\":1}]}");assert(!readCoinGeckoSearch(input,best,"btc"));assert(best["id"]=="btc");}
 {JsonDocument best;Input input("{\"coins\": []}");assert(!readCoinGeckoSearch(input,best,"btc"));assert(best.isNull());}
 for(const char *s:{"{}","{\"coins\":{}}","{\"coins\":[{\"id\":","{\"coins\":[{} {}]}","{\"coins\":[{},]}"}){JsonDocument best;Input input(s);assert(readCoinGeckoSearch(input,best,"btc"));}
}
'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-DARDUINOJSON_POOL_CAPACITY=128','-I'+str(root/'.pio/libdeps/nodemcuv2/ArduinoJson/src'),str(p/'test.cpp'),'-Wl,--wrap=malloc,--wrap=realloc,--wrap=free','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('CoinGecko stream checks passed: 5,001 candidates, bounded allocations, ranking, empty/malformed/truncated results')
