"""Check API responses release HTTP/1.1 keep-alive memory between feed polls."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/webserver.cpp').read_text()
def extract(name):
    m=re.search(r'^void '+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
code='''#include <cassert>
#include <string>
using String=std::string;
struct {bool alive=true;int code=0;void keepAlive(bool v){alive=v;}void sendHeader(const char*,const char*){}void send(int c,const char*,const String&){code=c;assert(!alive);}}server;
'''
for name in ('prepareNoStoreHeaders','sendTextResponse','sendJsonResponse'):
    code+=extract(name)+'\n'
code+='''int main(){sendJsonResponse(202,"{\\"pending\\":true}");assert(server.code==202);server.alive=true;sendJsonResponse(200,"{}");server.alive=true;sendTextResponse(409,"Busy");}'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('HTTP heap checks passed: pending, success and error API responses disable keep-alive')
