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
using std::max;
int heap=0;
struct HeapSelectIram {int previous=heap;HeapSelectIram(){heap=1;}~HeapSelectIram(){heap=previous;}};
uint32_t freeBytes[2]={14728,18000},maxBytes[2]={14000,18000};
struct {void getHeapStats(uint32_t *free,uint32_t *largest){*free=freeBytes[heap];*largest=maxBytes[heap];}} ESP;
'''
for constant in ['kHttpsMinFreeHeapBytes','kHttpsDefaultRecvBufferBytes']:
    code+=re.search(r'constexpr uint(?:32|16)_t '+constant+r' = [^;]+;',source).group(0)
code+=source[start:end]
code+=r'''
int main(){
#ifdef UMM_HEAP_IRAM
 assert(tlsHeapReady());assert(heap==0);
 maxBytes[1]=16000;assert(!tlsHeapReady());assert(heap==0);
 maxBytes[1]=18000;freeBytes[0]=4000;assert(!tlsHeapReady());assert(heap==0);
 freeBytes[0]=25000;maxBytes[0]=17000;freeBytes[1]=1000;maxBytes[1]=1000;assert(tlsHeapReady());
#else
 assert(!tlsHeapReady());freeBytes[0]=25000;maxBytes[0]=17000;assert(tlsHeapReady());
 maxBytes[0]=16000;assert(!tlsHeapReady());
#endif
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    for definitions in [[],['-DUMM_HEAP_IRAM']]:
        subprocess.run(['g++','-std=c++17',*definitions,str(path/'test.cpp'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
print('TLS heap checks passed: secondary heap, largest allocation, low capacity, single heap, restored selection')
