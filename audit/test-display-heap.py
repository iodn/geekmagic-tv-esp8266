"""Check the real sprite allocation gate at realistic and constrained heap sizes."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/display.cpp').read_text()
start=source.index('bool ensureClockDynamicSprite()')
end=source.index('\nvoid updateClockDynamicArea()',start)
code=r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#define F(x) x
constexpr int kClockDynamicRegionWidth=216,kClockDynamicRegionHeight=64;
bool clockDynamicSpriteAllowed=true,clockDynamicSpriteReady=false,clockDynamicSpriteAttempted=false;
uint32_t clockDynamicSpriteLastAttemptMs=0,now=100;
uint32_t millis(){return now;}
struct {uint32_t heap=20000;uint32_t getFreeHeap(){return heap;}} ESP;
void logPrintf(const char*,...){}
struct {
 bool allocationSucceeds=true;int allocations=0;
 void deleteSprite(){}void setColorDepth(int depth){assert(depth==1);}
 void *createSprite(int w,int h){++allocations;assert(w==216 && h==64);return allocationSucceeds?this:nullptr;}
} clockDynamicSprite;
'''
for constant in ['kClockSpriteMinFreeHeapBytes','kClockSpriteRetryIntervalMs']:
    code+=re.search(r'constexpr uint32_t '+constant+r' = [^;]+;',source).group(0)
code+=source[start:end]
code+=r'''
int main(){
 assert(ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 assert(ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 clockDynamicSpriteReady=false;clockDynamicSpriteAttempted=false;clockDynamicSpriteAllowed=false;
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 clockDynamicSpriteAllowed=true;ESP.heap=9000;
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 ESP.heap=20000;clockDynamicSprite.allocationSucceeds=false;
 now+=4999;assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 now++;
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==2);
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==2);
 now+=5000;clockDynamicSprite.allocationSucceeds=true;
 assert(ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==3);
 // Retry timing must also work across the 32-bit millis wrap.
 clockDynamicSpriteReady=false;clockDynamicSpriteLastAttemptMs=0xfffff800U;
 now=clockDynamicSpriteLastAttemptMs+4999U;assert(!ensureClockDynamicSprite());
 now++;assert(ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==4);
 assert(kClockSpriteMinFreeHeapBytes-1744>=8000);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Display heap checks passed: 20 KB allocation, low-heap skip, suspension, allocation failure')
