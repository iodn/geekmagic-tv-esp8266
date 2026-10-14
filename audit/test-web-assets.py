"""Verify generated compressed pages preserve every byte and are reproducible."""
from pathlib import Path
import gzip, re, runpy, tempfile, subprocess
root=Path(__file__).resolve().parents[1]
source=(root/'src/webui.h').read_text()
with tempfile.TemporaryDirectory() as folder:
    class BuildEnv:
        def subst(self, value):
            return str(root) if value=='$PROJECT_DIR' else folder
        def Append(self, **kwargs):
            assert kwargs['CPPPATH']==[str(Path(folder)/'generated')]
    args={'Import':lambda name:None,'env':BuildEnv()}
    runpy.run_path(str(root/'scripts/compress-webui.py'),init_globals=args)
    output=Path(folder)/'generated/webui_gzip.h'
    original=output.read_bytes()
    original_mtime=output.stat().st_mtime_ns
    runpy.run_path(str(root/'scripts/compress-webui.py'),init_globals=args)
    assert output.read_bytes()==original and output.stat().st_mtime_ns==original_mtime
    (Path(folder)/'Arduino.h').write_text('#pragma once\n#include <cstdint>\n#define PROGMEM\n#define PSTR(x) x\nusing PGM_P=const char*;\n')
    handlers=(root/'src/webserver.cpp').read_text()
    code=r'''
#include <cassert>
#include <string>
#include <fstream>
#include "generated/webui_gzip.h"
constexpr uint32_t kAuthSessionMaxAgeSec=43200;
struct {
 std::string encoding,body;
 bool hasHeader(const char*){return false;}
 void sendHeader(const char *name,const char *value){assert(std::string(name)=="Content-Encoding");encoding=value;}
 void send_P(int status,PGM_P type,PGM_P bytes,size_t length){assert(status==200 && std::string(type)=="text/html");assert(encoding=="gzip");body.assign(bytes,length);}
} server;
bool requestHasValidSession(){return false;}
void attachSessionCookie(uint32_t){}void clearSessionCookie(){}
void prepareNoStoreHeaders(){server.encoding.clear();}
'''
    for name in ['handleRoot','handleOTAForm']:
        start=handlers.index('void '+name+'(');brace=handlers.index('{',start);depth=1;end=brace+1
        while depth:
            depth+=(handlers[end]=='{')-(handlers[end]=='}');end+=1
        code+=handlers[start:end]+'\n'
    code+=r'''
int main(){handleRoot();std::ofstream("index_html.bin",std::ios::binary).write(server.body.data(),server.body.size());
handleOTAForm();std::ofstream("ota_html.bin",std::ios::binary).write(server.body.data(),server.body.size());}
'''
    (Path(folder)/'handlers.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-I'+folder,str(Path(folder)/'handlers.cpp'),'-o',str(Path(folder)/'handlers')],check=True)
    subprocess.run([str(Path(folder)/'handlers')],cwd=folder,check=True)
    for name in ['index_html','ota_html']:
        html=re.search(r'const char '+name+r'\[\] PROGMEM = R"rawliteral\(([\s\S]*?)\)rawliteral";',source).group(1).encode()
        array=re.search(name+r'_gzip\[\] PROGMEM = \{([\s\S]*?)\};',original.decode()).group(1)
        data=bytes(int(value,16) for value in re.findall(r'0x([0-9a-f]{2})',array))
        assert (Path(folder)/(name+'.bin')).read_bytes()==data
        assert gzip.decompress(data)==html
        assert len(data)<len(html)//3
        print(f'{name}: {len(html)} -> {len(data)} bytes; exact round trip verified')
print('Compressed web asset checks passed: both pages, deterministic bytes, unchanged-file reuse')
