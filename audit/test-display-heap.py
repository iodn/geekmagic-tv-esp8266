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
struct {uint32_t heap=20000;uint32_t getFreeHeap(){return heap;}} ESP;
void logPrintf(const char*,...){}
struct {
 bool allocationSucceeds=true;int allocations=0;
 void deleteSprite(){}void setColorDepth(int depth){assert(depth==1);}
 void *createSprite(int w,int h){++allocations;assert(w==216 && h==64);return allocationSucceeds?this:nullptr;}
} clockDynamicSprite;
'''
code+=re.search(r'constexpr uint32_t kClockSpriteMinFreeHeapBytes = [^;]+;',source).group(0)
code+=source[start:end]
code+=r'''
int main(){
 assert(ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 assert(ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 clockDynamicSpriteReady=false;clockDynamicSpriteAttempted=false;clockDynamicSpriteAllowed=false;
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 clockDynamicSpriteAllowed=true;ESP.heap=9000;
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==1);
 // Resource resumption resets the attempt flag before trying again.
 clockDynamicSpriteAttempted=false;ESP.heap=20000;clockDynamicSprite.allocationSucceeds=false;
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==2);
 assert(!ensureClockDynamicSprite());assert(clockDynamicSprite.allocations==2);
 assert(kClockSpriteMinFreeHeapBytes-1744>=8000);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Display heap checks passed: 20 KB allocation, low-heap skip, suspension, allocation failure')
