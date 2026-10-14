"""Run the actual display validator against the installed Tiny JPEG C decoder.

Pillow generates baseline JPEGs. File and TJpg_Decoder adapters preserve the real
jd_prepare/jd_decomp implementation; only filesystem, callback and yield boundaries
are host doubles. Native pointer widths require a larger host workspace than MCU.
"""
from pathlib import Path
import re
import subprocess
import tempfile
from PIL import Image

root = Path(__file__).resolve().parents[1]
decoder = root / ".pio/libdeps/nodemcuv2/TJpg_Decoder/src"
source = (root / "src/display.cpp").read_text()
match = re.search(r"^bool displayValidateImage\([^;{]*\)\s*\{", source, re.M)
assert match
start = source.index("{", match.start())
depth, end = 1, start + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
validator = source[match.start():end]

with tempfile.TemporaryDirectory() as folder:
    tmp = Path(folder)
    for name, dimensions in (("small.jpg", (32, 24)), ("large.jpg", (320, 256))):
        image = Image.new("RGB", dimensions)
        image.putdata([((x * 23 + y * 5) % 256, (y * 19 + x * 7) % 256, (x * y * 3) % 256)
                       for y in range(dimensions[1]) for x in range(dimensions[0])])
        image.save(tmp / name, "JPEG", quality=85, progressive=False)
    valid = (tmp / "small.jpg").read_bytes()
    sos = valid.index(b"\xff\xda")
    entropy_start = sos + 2 + int.from_bytes(valid[sos + 2:sos + 4], "big")
    assert entropy_start < len(valid) - 2
    (tmp / "truncated-entropy.jpg").write_bytes(valid[:entropy_start + 3] + b"\xff\xd9")
    (tmp / "missing-eoi.jpg").write_bytes(valid[:-2])
    (tmp / "partial-eoi.jpg").write_bytes(valid[:-1])
    (tmp / "invalid-header.jpg").write_bytes(b"\xff\xd8\xff\xdb\x00\x00\xff\xd9")
    (tmp / "bad-soi.jpg").write_bytes(b"\x00\x00" + valid[2:])
    (tmp / "too-short.jpg").write_bytes(b"\xff\xd8")
    code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <iterator>
#include <algorithm>
#include <iostream>
#include "tjpgd.h"
struct File {
 std::vector<uint8_t> data;size_t position=0;bool valid=false;
 explicit operator bool()const{return valid;}
 size_t size(){return data.size();}
 int read(){return position<data.size() ? data[position++] : -1;}
 bool seek(size_t offset){if(offset>data.size())return false;position=offset;return true;}
 void close(){valid=false;}
};
struct FS {
 File open(const char *path,const char *){std::ifstream stream(path,std::ios::binary);File result;
  if(stream){result.valid=true;result.data.assign(std::istreambuf_iterator<char>(stream),{});}return result;}
 bool exists(const char *path){std::ifstream stream(path);return bool(stream);}
} LittleFS;
int yields=0,drawnBlocks=0,decodedBlocks=0;
void yield(){++yields;}
bool tftOutput(int16_t,int16_t,uint16_t,uint16_t,uint16_t *){++drawnBlocks;return true;}
using Callback=bool (*)(int16_t,int16_t,uint16_t,uint16_t,uint16_t *);
struct Input {
 File file;Callback callback;
};
static size_t input(JDEC *decoder,uint8_t *buffer,size_t requested){
 auto &state=*static_cast<Input *>(decoder->device);
 size_t count=std::min(requested,state.file.data.size()-state.file.position);
 if(buffer)memcpy(buffer,state.file.data.data()+state.file.position,count);
 state.file.position+=count;return count;
}
static int output(JDEC *decoder,void *bitmap,JRECT *rectangle){
 auto &state=*static_cast<Input *>(decoder->device);++decodedBlocks;
 return state.callback(rectangle->left,rectangle->top,rectangle->right-rectangle->left+1,
                       rectangle->bottom-rectangle->top+1,static_cast<uint16_t *>(bitmap));
}
struct Decoder {
 Callback callback=tftOutput;uint16_t lastWidth=0,lastHeight=0;int calls=0;
 void setCallback(Callback value){callback=value;}
 JRESULT process(const char *path,FS &fs,bool decode){
  ++calls;Input state{fs.open(path,"r"),callback};if(!state.file)return JDR_INP;
  // 64-bit host pointers enlarge table metadata relative to the 32-bit MCU.
  alignas(void *) uint8_t workspace[TJPGD_WORKSPACE_SIZE * 2];
  JDEC decoder{};JRESULT result=jd_prepare(&decoder,input,workspace,sizeof workspace,&state);
  if(result!=JDR_OK)return result;
  lastWidth=decoder.width;lastHeight=decoder.height;
  return decode ? jd_decomp(&decoder,output,0) : result;
 }
 JRESULT getFsJpgSize(uint16_t *width,uint16_t *height,const char *path,FS &fs){
  JRESULT result=process(path,fs,false);*width=lastWidth;*height=lastHeight;return result;}
 JRESULT drawFsJpg(int,int,const char *path,FS &fs){return process(path,fs,true);}
} TJpgDec;
'''
    code += validator + "\n"
    code += r'''
int main(int argc,char **argv){
 assert(argc==2);std::string directory=argv[1];
 auto path=[&](const char *name){return directory+"/"+name;};
 for(const char *name : {"small.jpg","large.jpg"}) {
  int previousYields=yields,previousBlocks=decodedBlocks;
  assert(displayValidateImage(path(name).c_str()));assert(TJpgDec.callback==tftOutput);
  assert(decodedBlocks>previousBlocks && yields>previousYields);assert(drawnBlocks==0);
  assert(TJpgDec.lastWidth==(std::string(name)=="small.jpg" ? 32 : 320));
  assert(TJpgDec.lastHeight==(std::string(name)=="small.jpg" ? 24 : 256));
 }
 for(const char *name : {"truncated-entropy.jpg","missing-eoi.jpg","partial-eoi.jpg","invalid-header.jpg","bad-soi.jpg","too-short.jpg","missing.jpg"}) {
  int previousCalls=TJpgDec.calls;
  assert(!displayValidateImage(path(name).c_str()));assert(TJpgDec.callback==tftOutput);assert(drawnBlocks==0);
  if(std::string(name)!="truncated-entropy.jpg" && std::string(name)!="invalid-header.jpg")assert(TJpgDec.calls==previousCalls);
 }
 assert(!displayValidateImage(nullptr));
 // Verify entropy failure comes from the real decoder, not only boundary markers.
 assert(TJpgDec.process(path("truncated-entropy.jpg").c_str(),LittleFS,true)!=JDR_OK);
 // Decoder acceptance without EOI depends on entropy padding/read-ahead. The
 // validator rejects the missing marker independently of that implementation detail.
 TJpgDec.setCallback([](int16_t,int16_t,uint16_t,uint16_t,uint16_t *){return true;});
 (void)TJpgDec.process(path("missing-eoi.jpg").c_str(),LittleFS,true);
 TJpgDec.setCallback(tftOutput);
 std::cout<<"Real JPEG regressions passed: baseline/dimensions, complete decode, truncated entropy, SOI/EOI/header rejection, no drawing, callback restoration\n";
}
'''
    (tmp / "check.cpp").write_text(code)
    subprocess.run(["gcc", "-std=c99", "-I" + str(decoder), "-c", str(decoder / "tjpgd.c"), "-o", str(tmp / "decoder.o")], check=True)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I" + str(decoder), str(tmp / "check.cpp"), str(tmp / "decoder.o"), "-o", str(tmp / "check")], check=True)
    subprocess.run([str(tmp / "check"), str(tmp)], check=True)
