"""Host regressions of actual countdown, NTP anchoring and filesystem functions."""
from pathlib import Path
import re, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/dashboard.cpp').read_text()
def extract(name):
    match = re.search(r'^(?:bool|void|int32_t) '+name+r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    start=source.index('{', match.start()); depth=1; end=start+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[match.start():end]
with tempfile.TemporaryDirectory() as folder:
    d=Path(folder)
    (d/'Arduino.h').write_text('#pragma once\n#include <string>\n#include <cstdint>\nusing String=std::string;\n')
    code=r'''
#include <ArduinoJson.h>
#include "dashboard.h"
#include <cassert>
#include <map>
#include <memory>
#include <cstring>
#include <cstdio>
#include <iostream>
DashboardData dashboardData{}, savedDashboardData{};
uint32_t epoch=1700000000;
uint32_t dashboardCurrentEpoch(){return epoch;}
void normalizeData(){}
int saves=0;
bool dashboardSaveData(){++saves;return true;}
size_t writeLimit=10000;
bool failRename=false;
struct File {
 std::shared_ptr<std::string> data; size_t cursor=0;
 explicit operator bool()const{return bool(data);}
 size_t write(uint8_t b){return write(&b,1);}
 size_t write(const uint8_t*b,size_t n){size_t k=std::min(n, writeLimit-data->size());data->append(reinterpret_cast<const char*>(b),k);return k;}
 int read(){return cursor<data->size()?static_cast<unsigned char>((*data)[cursor++]):-1;}
 size_t readBytes(char*b,size_t n){size_t k=std::min(n,data->size()-cursor);memcpy(b,data->data()+cursor,k);cursor+=k;return k;}
 void flush(){} void close(){}
};
struct {
 std::map<std::string,std::shared_ptr<std::string>> files;
 void remove(const char*p){files.erase(p);}
 bool exists(const char*p){return files.count(p);}
 File open(const char*p,const char*mode){if(*mode=='w'){auto value=std::make_shared<std::string>();files[p]=value;return {value};}auto f=files.find(p);return f==files.end()?File{}:File{f->second};}
 bool rename(const char*a,const char*b){if(failRename)return false;files[b]=files.at(a);files.erase(a);return true;}
} LittleFS;
'''
    for name in ['copyString','buildTempPath','dashboardFocusRemainingSeconds','dashboardEventRemainingSeconds','applyFocusObject','applyEventObject','dashboardSyncRuntimeState','writeJsonToFile']:
        code+='\n'+extract(name)
    code+='\ntemplate<typename TObject>\n'+extract('loadJsonFile')
    code+=r'''
int main(){
 dashboardData.focus.running=true; dashboardData.focus.remainingSeconds=1500; dashboardData.focus.updatedAtEpoch=epoch-600;
 dashboardData.event.remainingSeconds=3600; dashboardData.event.updatedAtEpoch=epoch-600;
 JsonDocument ui; deserializeJson(ui,R"({"focus":{"running":true,"remainingSeconds":1500},"event":{"remainingSeconds":3600}})");
 applyFocusObject(ui["focus"]);applyEventObject(ui["event"]);
 assert(dashboardFocusRemainingSeconds()==900);assert(dashboardEventRemainingSeconds()==3000);
 deserializeJson(ui,R"({"running":false,"label":"Pause","remainingSeconds":1500})"); applyFocusObject(ui.as<JsonObjectConst>());
 assert(!dashboardData.focus.running);assert(dashboardData.focus.remainingSeconds==900);
 epoch+=60;assert(dashboardFocusRemainingSeconds()==900);
 deserializeJson(ui,R"({"running":true})");applyFocusObject(ui.as<JsonObjectConst>());
 epoch+=30;assert(dashboardFocusRemainingSeconds()==870);
 dashboardData.focus.updatedAtEpoch=0;savedDashboardData=dashboardData;
 strcpy(dashboardData.quote.text,"unsaved preview");strcpy(savedDashboardData.quote.text,"saved quote");
 assert(dashboardSyncRuntimeState());assert(saves==0);assert(dashboardData.focus.updatedAtEpoch==epoch);
 assert(savedDashboardData.focus.updatedAtEpoch==epoch);assert(std::string(savedDashboardData.quote.text)=="saved quote");
 JsonDocument settings;settings["token"]="abcdefghijklmnop";
 LittleFS.files["/config.json"]=std::make_shared<std::string>("{\"old\":true}");writeLimit=12;
 assert(!writeJsonToFile("/config.json",settings));assert(*LittleFS.files.at("/config.json")=="{\"old\":true}");assert(!LittleFS.exists("/config.json.tmp"));
 writeLimit=10000;failRename=true;
 assert(!writeJsonToFile("/config.json",settings));assert(*LittleFS.files.at("/config.json")=="{\"old\":true}");assert(LittleFS.exists("/config.json.tmp"));
 failRename=false;assert(writeJsonToFile("/config.json",settings));assert(!LittleFS.exists("/config.json.tmp"));
 LittleFS.files.erase("/config.json");LittleFS.files["/config.json.tmp"]=std::make_shared<std::string>("[]");
 bool called=false;auto load=[&](JsonObjectConst){called=true;};assert(!loadJsonFile("/config.json",load));assert(!called);
 LittleFS.files["/config.json.tmp"]=std::make_shared<std::string>("{\"token\":\"saved\"}");
 assert(loadJsonFile("/config.json",load));assert(called);assert(LittleFS.exists("/config.json"));
 std::cout<<"Dashboard regressions passed\n";
}
'''
    (d/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-I'+str(d),'-I'+str(root/'src'),'-I'+str(root/'.pio/libdeps/nodemcuv2/ArduinoJson/src'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
