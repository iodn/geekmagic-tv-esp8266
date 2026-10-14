"""Print observed audit failures using source-extracted functions and host mocks.

Run pio run first to install ArduinoJson, then run with Python 3 and g++.
These are diagnostics, not hardware tests or a passing regression test suite.
"""
from pathlib import Path
import re, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
def function(file, name):
    source = (root / 'src' / file).read_text()
    match = re.search(r'^(?:String|bool|void|int32_t) ' + name + r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]
with tempfile.TemporaryDirectory() as folder:
    d = Path(folder)
    (d / 'Arduino.h').write_text(r'''
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <algorithm>
class String : public std::string {
public:
 using std::string::string;
 String(const std::string &s):std::string(s){}
 String(float value, uint8_t precision) {char b[128]; snprintf(b,sizeof b,"%.*f",precision,value); assign(b);}
 bool endsWith(const char *s) const {return size()>=strlen(s) && compare(size()-strlen(s),strlen(s),s)==0;}
 bool startsWith(const char *s) const {return rfind(s,0)==0;}
 void remove(size_t n){erase(n);}
 int indexOf(char c) const {auto n=find(c);return n==npos ? -1 : int(n);}
 char charAt(size_t n) const {return at(n);}
 bool isEmpty() const {return empty();}
};
#define F(x) x
''')
    code = r'''
#include "Arduino.h"
#include <ArduinoJson.h>
#include "feeds.h"
#include <iostream>
#include <memory>
#include <map>
#include <cmath>
using std::isfinite;
DashboardData dashboardData{};
FeedConfig feedConfig{};
FeedRuntimeState feedRuntime{};
uint32_t epoch=1700000000;
uint32_t dashboardCurrentEpoch(){return epoch;}
int automaticSaves=0;
void normalizeData(){}
bool dashboardSaveData(){++automaticSaves;return true;}
unsigned long millis(){return 1234;}
void yield(){}
void logPrint(const char*){}
struct {int status(){return 3;}} WiFi;
constexpr int WL_CONNECTED=3;
bool feedsHomeAssistantConfigured(){return true;}
bool networkFail=false;
bool syncHomeAssistantSlot(uint8_t i, String *error){if(i==0 && !networkFail){feedRuntime.homeAssistant.slots[i].hasData=true;return true;} *error="HTTP 404";return false;}
struct File {
 std::shared_ptr<std::string> data;
 operator bool() const {return bool(data);}
 size_t write(uint8_t b){if(data->size()>=12)return 0;data->push_back(char(b));return 1;}
 size_t write(const uint8_t *b, size_t n){size_t k=std::min(n,12-data->size());data->append((const char*)b,k);return k;}
 void flush(){} void close(){}
};
struct {
 std::map<std::string,std::shared_ptr<std::string>> files;
 void remove(const char*p){files.erase(p);}
 bool exists(const char*p){return files.count(p);}
 File open(const char*p,const char*){auto s=std::make_shared<std::string>();files[p]=s;return {s};}
 bool rename(const char*a,const char*b){files[b]=files[a];files.erase(a);return true;}
} LittleFS;
struct {uint16_t version; char passwordMd5[33]; char provisionedPassword[33];} authConfig{};
constexpr uint16_t AUTH_CONFIG_VERSION=2;
bool provisionedThisBoot=true;
bool authVerifyPassword(const String& p){return p==authConfig.passwordMd5;}
bool authValidatePasswordPolicy(const String&,String*){return true;}
String computePasswordMd5(const String &p){return p;}
bool writeAuthConfig(){return false;}
'''
    for file, names in {
        'feeds.cpp':['copyString','parseFloatStrict','trimTrailingZerosString','groupThousandsString','formatHomeAssistantNumericState','homeAssistantSlotConfigured','syncHomeAssistant','buildTempPath','writeJsonToFile'],
        'display.cpp':['trimTrailingZeros','groupThousands','formatMarketPriceValue'],
        'dashboard.cpp':['dashboardFocusRemainingSeconds','dashboardEventRemainingSeconds','applyFocusObject','applyEventObject','dashboardSyncRuntimeState'],
        'auth.cpp':['authUpdatePassword'],
    }.items():
        code += '\n\n'.join(function(file,name) for name in names)+'\n'
    code += r'''
int main(){
 std::cout << "HA 1000 -> " << formatHomeAssistantNumericState(1000) << "; market 1000 -> " << formatMarketPriceValue(1000) << "\n";
 float parsed=0;
 bool numeric=parseFloatStrict("1234567890123456789012345678901oops",parsed);
 std::cout << "Strict parser accepts long text after truncation=" << numeric << "\n";
 std::cout << "HA exact integer 16777217 -> " << formatHomeAssistantNumericState(16777217.0f) << "\n";
 dashboardData.focus.running=true; dashboardData.focus.remainingSeconds=1500; dashboardData.focus.updatedAtEpoch=epoch-600;
 dashboardData.event.remainingSeconds=3600; dashboardData.event.updatedAtEpoch=epoch-600;
 std::cout << "Before unrelated UI payload: focus=" << dashboardFocusRemainingSeconds() << ", event=" << dashboardEventRemainingSeconds() << "\n";
 JsonDocument ui; deserializeJson(ui,R"({"focus":{"running":true,"remainingSeconds":1500},"event":{"remainingSeconds":3600}})");
 applyFocusObject(ui["focus"].as<JsonObjectConst>()); applyEventObject(ui["event"].as<JsonObjectConst>());
 std::cout << "After identical stored durations resent: focus=" << dashboardFocusRemainingSeconds() << ", event=" << dashboardEventRemainingSeconds() << "\n";
 dashboardData.focus.updatedAtEpoch=0;
 strcpy(dashboardData.quote.text,"unsaved preview");
 dashboardSyncRuntimeState();
 std::cout << "Clock anchor initialization triggers whole-data save count=" << automaticSaves << ", live quote='" << dashboardData.quote.text << "'\n";
 feedConfig.homeAssistant.slots[0].enabled=true;strcpy(feedConfig.homeAssistant.slots[0].entityId,"person.a");
 feedConfig.homeAssistant.slots[1].enabled=true;strcpy(feedConfig.homeAssistant.slots[1].entityId,"person.b");
 String error; bool ok=syncHomeAssistant(&error);
 std::cout << "HA one slot OK, one 404: success=" << ok << ", lastError='" << feedRuntime.homeAssistant.lastError << "'\n";
 networkFail=true;ok=syncHomeAssistant(&error);
 std::cout << "HA all slots fail next poll: success=" << ok << ", old slot.hasData=" << feedRuntime.homeAssistant.slots[0].hasData << "\n";
 JsonDocument config;config["token"]="abcdefghijklmnopqrs";
 LittleFS.files["/config.json"]=std::make_shared<std::string>("old valid config");
 ok=writeJsonToFile("/config.json",config);
 std::cout << "Short filesystem write: save success=" << ok << ", saved='" << *LittleFS.files["/config.json"] << "'\n";
 strcpy(authConfig.passwordMd5,"old-password"); strcpy(authConfig.provisionedPassword,"old-password");
 ok=authUpdatePassword("old-password","new-password",&error);
 std::cout << "Password storage fails: success=" << ok << ", active credential='" << authConfig.passwordMd5 << "'\n";
 JsonDocument wrong;deserializeJson(wrong,R"({"line1":123})");
 std::cout << "Numeric API line1: isNull=" << wrong["line1"].isNull() << ", as<const char*> null=" << (wrong["line1"].as<const char*>()==nullptr) << "\n";
}
'''
    (d/'audit.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-I'+str(d),'-I'+str(root/'src'),'-I'+str(root/'.pio/libdeps/nodemcuv2/ArduinoJson/src'),str(d/'audit.cpp'),'-o',str(d/'audit')],check=True)
    subprocess.run([str(d/'audit')],check=True)
