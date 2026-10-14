"""Check real TLS heap gating with and without the configured secondary heap."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/feeds.cpp').read_text()
start=source.index('bool tlsHeapReady()');end=source.index('\nbool httpGetJson(',start)
code=r'''
#include <cassert>
#define F(x) x
void logPrintf(const char*,...){}
#include <cstdint>
#include <algorithm>
#include <string>
using String=std::string;
using std::max;
int heap=0;
bool feedJobsCancelled=false;
uint32_t clockMs=0;
int delays=0,recoverAfter=0,cancelAfter=0;
uint32_t millis(){return clockMs;}
void delay(unsigned ms);
struct HeapSelectIram {int previous=heap;HeapSelectIram(){heap=1;}~HeapSelectIram(){heap=previous;}};
uint32_t freeBytes[2]={14728,18000},maxBytes[2]={14000,18000};
struct {void getHeapStats(uint32_t *free,uint32_t *largest){*free=freeBytes[heap];*largest=maxBytes[heap];}} ESP;
void delay(unsigned ms){clockMs+=ms;++delays;if(recoverAfter&&delays>=recoverAfter){freeBytes[0]=14728;maxBytes[0]=14000;}if(cancelAfter&&delays>=cancelAfter)feedJobsCancelled=true;}
'''
for constant in ['kHttpsMinFreeHeapBytes','kHttpsMinDramHeapBytes','kHttpsMinDramBlockBytes','kHttpsHeapWaitMs','kHttpsDefaultRecvBufferBytes']:
    code+=re.search(r'constexpr uint(?:32|16)_t '+constant+r' = [^;]+;',source).group(0)
code+=source[start:end]
code+=r'''
int main(){
#ifdef UMM_HEAP_IRAM
 assert(tlsHeapReady());assert(heap==0);
 // Captured Sync-all failure: enough combined RAM but insufficient DRAM
 // for the crypto stack, SSL engine and 1,496-byte certificate validator.
 freeBytes[0]=12808;maxBytes[0]=12808;freeBytes[1]=18824;maxBytes[1]=18824;
 assert(!tlsHeapReady());assert(heap==0);
 freeBytes[0]=14728;maxBytes[0]=14000;freeBytes[1]=18000;maxBytes[1]=18000;
 freeBytes[0]=7000;maxBytes[0]=7000;freeBytes[1]=20000;maxBytes[1]=20000;
 assert(!tlsHeapReady());assert(heap==0);
 freeBytes[0]=14728;maxBytes[0]=14000;freeBytes[1]=18000;maxBytes[1]=18000;
 maxBytes[1]=16000;assert(!tlsHeapReady());assert(heap==0);
 maxBytes[1]=18000;freeBytes[0]=4000;assert(!tlsHeapReady());assert(heap==0);
 freeBytes[0]=25000;maxBytes[0]=17000;freeBytes[1]=1000;maxBytes[1]=1000;assert(!tlsHeapReady());
 freeBytes[0]=33000;maxBytes[0]=20000;assert(tlsHeapReady());
 maxBytes[0]=11000;assert(!tlsHeapReady());
 String error;
 freeBytes[0]=12808;maxBytes[0]=12808;freeBytes[1]=18000;maxBytes[1]=18000;
 recoverAfter=3;assert(waitForTlsHeap(&error));assert(delays==3&&clockMs==150&&error.empty()&&heap==0);
 recoverAfter=0;delays=0;clockMs=0;freeBytes[0]=12808;maxBytes[0]=12808;
 assert(!waitForTlsHeap(&error));assert(clockMs==kHttpsHeapWaitMs&&error=="Low heap, retrying later"&&heap==0);
 delays=0;clockMs=0;cancelAfter=2;error.clear();
 assert(!waitForTlsHeap(&error));assert(clockMs==100&&error=="Feed job cancelled"&&heap==0);
 feedJobsCancelled=false;cancelAfter=0;clockMs=0xfffffff0U;
 uint32_t began=clockMs;assert(!waitForTlsHeap(nullptr));assert(clockMs-began==kHttpsHeapWaitMs&&heap==0);
#else
 assert(!tlsHeapReady());freeBytes[0]=25000;maxBytes[0]=17000;assert(!tlsHeapReady());
 freeBytes[0]=33000;maxBytes[0]=20000;assert(tlsHeapReady());
 maxBytes[0]=16000;assert(!tlsHeapReady());
#endif
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    for definitions in [[],['-DUMM_HEAP_IRAM']]:
        subprocess.run(['g++','-std=c++17',*definitions,str(path/'test.cpp'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
print('TLS heap checks passed: DRAM/IRAM budgets, fragmented/low heap, bounded cooperative recovery, cancellation, timer wrap')
