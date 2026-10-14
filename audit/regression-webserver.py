"""Exercise real webserver handlers with ArduinoJson and mocked service/storage boundaries."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/webserver.cpp').read_text()
def extract(name):
    match=re.search(r'^(?:bool|void) '+name+r'\([^;{]*\)\s*\{',SOURCE,re.M);assert match,name
    start=SOURCE.index('{',match.start());depth=1;end=start+1
    while depth:depth+=(SOURCE[end]=='{')-(SOURCE[end]=='}');end+=1
    return SOURCE[match.start():end]
code=r'''
#include <ArduinoJson.h>
#include <cassert>
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <map>
#include <iostream>
class String:public std::string {public:using std::string::string;String(const std::string&s):std::string(s){}String(int v):std::string(std::to_string(v)){}String(long v):std::string(std::to_string(v)){}String(unsigned long v):std::string(std::to_string(v)){}bool isEmpty()const{return empty();}size_t write(uint8_t c){push_back(c);return 1;}size_t write(const uint8_t*data,size_t n){append(reinterpret_cast<const char*>(data),n);return n;} };
#define F(x) x
constexpr int SETTINGS_DEVICE_NAME_LENGTH=32,SETTINGS_HOSTNAME_LENGTH=32;
constexpr char NTP_SERVER[]="pool.ntp.org",FIRMWARE_VERSION_STRING[]="test",kAuthSessionCookieName[]="SCSESSID";
std::vector<std::string> events;
constexpr int UPLOAD_FILE_START=0,UPLOAD_FILE_WRITE=1,UPLOAD_FILE_END=2,UPLOAD_FILE_ABORTED=3,WIFI_SCAN_RUNNING=-1,WIFI_SCAN_FAILED=-2;
struct HTTPUpload {int status=UPLOAD_FILE_START;uint8_t*buf=nullptr;size_t currentSize=0;} uploadState;
struct {std::string body;bool hasBody=true;std::map<std::string,String> args;bool hasArg(const char*name){return std::string(name)=="plain"?hasBody:args.count(name);}String arg(const char*name){return std::string(name)=="plain"?String(body):args[name];}HTTPUpload&upload(){return uploadState;}void sendHeader(const char*,const String&v){events.push_back("cookie");assert(v.find("Max-Age=0")!=std::string::npos);}} server;
struct Settings {int brightness=70,theme=0;long gmtOffset=0;char deviceName[32]="configured-clock";} appSettings,savedAppSettings;
bool savedSettingsReady=false;
int currentBrightness=70,currentTheme=0,saves=0,ntpCalls=0,brightnessCalls=0;
struct {int scanResult=WIFI_SCAN_RUNNING,startResult=WIFI_SCAN_RUNNING,scanCalls=0,deletes=0;String hostname(){return "active-clock";}void scanDelete(){++deletes;}int scanNetworks(bool asynchronous,bool hidden){assert(asynchronous&&hidden);++scanCalls;return startResult;}int scanComplete(){return scanResult;}const char*SSID(int index){return index==0?"network-one":"network-two";}int RSSI(int index){return -50-index;}int encryptionType(int){return 4;}} WiFi;
struct {bool running=false,error=false,beginSuccess=true,writeSuccess=true,endSuccess=true;int begins=0,ends=0;bool begin(uint32_t){++begins;running=beginSuccess;error=!beginSuccess;return beginSuccess;}size_t write(uint8_t*,size_t n){return writeSuccess?n:n-1;}bool end(bool finalize=false){++ends;running=false;if(!finalize){error=true;return false;}error=!endSuccess;return endSuccess;}bool isRunning(){return running;}bool hasError(){return error;}void printError(int){}} Update;
int Serial=0;
struct {uint32_t getFreeSketchSpace(){return 0x100000;}} ESP;
bool wifiScanStarted=false,factoryResetPending=false,wifiReconfigurePending=false,otaUploadComplete=false;
String otaUploadError;
struct {bool queued=false,active=false;}wifiConnectTask;
bool feedBusy=false,syncAccept=true;int syncCalls=0;String syncScope;
bool feedsBusy(){return feedBusy;}bool feedsSyncNow(const char*scope,String*error){++syncCalls;syncScope=scope;if(!syncAccept)*error="Invalid scope";return syncAccept;}
void feedsBuildStateJson(String&json){json=R"({"meta":{"syncPending":false}})";}
void displayShowMessage(const String&){}
struct {void end(){events.push_back("ota-end");}} ArduinoOTA;
bool otaStarted=true,rebootPending=false;
unsigned long rebootRequestedAtMs=0;
unsigned long millis(){return 1234;}
String authSessionToken="session-token";unsigned long authSessionLastSeenMs=123;
int status=0;String response;
void sendTextResponse(int code,const String&text){status=code;response=text;events.push_back("response");}
void sendJsonResponse(int code,const String&text){status=code;response=text;}
void settingsBuildHostname(const char*name,char*buffer,size_t n){snprintf(buffer,n,"%s",name);}
void settingsDefaultDeviceName(char*buffer,size_t n){snprintf(buffer,n,"default-clock");}
void settingsSanitizeDeviceName(const char*name,char*buffer,size_t n){snprintf(buffer,n,"%s",name?name:"");}
bool settingsSaveSuccess=true;bool settingsSave(const Settings&){++saves;return settingsSaveSuccess;}
void configTime(long,int,const char*){++ntpCalls;}
void applyEffectiveBrightness(bool){++brightnessCalls;}
bool displayHasDraftChanges(){return false;}
bool webserverHasPendingNetworkAction();
template<class T>T constrain(T value,T low,T high){return std::max(low,std::min(value,high));}
bool passwordSaveSuccess=false;
bool authUpdatePassword(const String&oldPassword,const String&newPassword,String*error){assert(oldPassword=="old-password");assert(newPassword=="new-password");events.push_back("persist-password");if(!passwordSaveSuccess)*error="Failed to save";return passwordSaveSuccess;}
'''
for name in ['webserverHasPendingNetworkAction','copyString','copyConfiguredHostname','copyActiveHostname','hostnameRestartRequired','copyDefaultDeviceName','queueRestart','clearSessionState','clearSessionCookie','parseJsonBody','handleAuthPasswordChange','captureSavedSettings','applyRuntimeSettingsObject','handleAppJson','handleVersionJson','handleApiUpdate','handleWiFiScan','handleFeedsSync','handleFeedJob','handleOTAUpload','handleOTADone']:
    if name=='handleApiUpdate':
        code+='\nstruct {char line2[32]="original";} displayState;\nint updates=0;void displayUpdate(){++updates;}\n'
    code+='\n'+extract(name)
code+=r'''
int main(){
 server.body=R"({"currentPassword":"old-password","newPassword":"new-password"})";
 handleAuthPasswordChange();assert(status==400);assert(otaStarted);assert(!rebootPending);assert(authSessionToken=="session-token");assert(authSessionLastSeenMs==123);assert(events==std::vector<std::string>({"persist-password","response"}));
 events.clear();passwordSaveSuccess=true;handleAuthPasswordChange();assert(status==200);assert(!otaStarted);assert(rebootPending);assert(rebootRequestedAtMs==1234);assert(authSessionToken.empty());assert(authSessionLastSeenMs==0);assert(events==std::vector<std::string>({"persist-password","cookie","ota-end","response"}));
 assert(response.find("restarting")!=std::string::npos);
 events.clear();server.body="invalid json";handleAuthPasswordChange();assert(status==400);assert(events==std::vector<std::string>({"response"}));
 JsonDocument settings;deserializeJson(settings,R"({"brightness":30,"gmtOffset":0})");String error;
 assert(applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),false,&error));assert(ntpCalls==0);assert(currentBrightness==30);
 assert(applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),false,&error));assert(ntpCalls==0);
 settings["gmtOffset"]=3600;applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),false,&error);assert(ntpCalls==1);assert(appSettings.gmtOffset==3600);
 applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),true,&error);assert(ntpCalls==1);assert(saves==1);assert(savedSettingsReady);
 // Clamped timezone inputs must also avoid repeated NTP restarts.
 settings["gmtOffset"]=999999;applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),false,&error);assert(ntpCalls==2);assert(appSettings.gmtOffset==14*3600);
 applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),false,&error);assert(ntpCalls==2);
 settingsSaveSuccess=false;settings["brightness"]=40;int savedBrightness=savedAppSettings.brightness;
 assert(!applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),true,&error));
 assert(error=="Failed to save settings" && savedAppSettings.brightness==savedBrightness);
 settingsSaveSuccess=true;assert(applyRuntimeSettingsObject(settings.as<JsonObjectConst>(),true,&error));
 assert(savedAppSettings.brightness==40);
 handleAppJson();assert(status==200);JsonDocument app;assert(!deserializeJson(app,response.c_str()));assert(app["hostName"]=="active-clock");assert(app["configuredHostName"]=="configured-clock");assert(app["rebootRequired"]==true);
 handleVersionJson();assert(!deserializeJson(app,response.c_str()));assert(app["hostName"]=="active-clock");
 strcpy(appSettings.deviceName,"active-clock");handleAppJson();assert(!deserializeJson(app,response.c_str()));assert(app["rebootRequired"]==false);
 for(const char*body:{"{\"line1\":123}","{\"line1\":true}","{\"line1\":[]}","{\"line1\":{}}"}){server.body=body;handleApiUpdate();assert(status==400);assert(updates==0);assert(std::string(displayState.line2)=="original");}
 server.body=R"({"line1":"valid"})";handleApiUpdate();assert(status==200);assert(updates==1);assert(std::string(displayState.line2)=="valid");
 // Fresh scans queue asynchronously; polls report pending and then actual results.
 rebootPending=false;handleWiFiScan();assert(status==409);assert(WiFi.scanCalls==0);
 server.args["start"]="1";feedBusy=true;handleWiFiScan();assert(status==409);assert(WiFi.scanCalls==0);
 feedBusy=false;wifiConnectTask.queued=true;handleWiFiScan();assert(status==409);wifiConnectTask.queued=false;
 handleWiFiScan();assert(status==202);assert(WiFi.scanCalls==1);assert(wifiScanStarted);assert(webserverHasPendingNetworkAction());
 server.args.clear();handleWiFiScan();assert(status==202);assert(WiFi.scanCalls==1);
 WiFi.scanResult=2;handleWiFiScan();assert(status==200);assert(!wifiScanStarted);assert(!deserializeJson(app,response.c_str()));assert(app.is<JsonArray>());assert(app.size()==2);assert(app[0]["ssid"]=="network-one");assert(app[1]["rssi"]==-51);
 server.args["start"]="1";WiFi.startResult=WIFI_SCAN_FAILED;handleWiFiScan();assert(status==502);assert(!wifiScanStarted);
 WiFi.startResult=WIFI_SCAN_RUNNING;WiFi.scanResult=WIFI_SCAN_RUNNING;handleWiFiScan();assert(status==202);server.args.clear();WiFi.scanResult=WIFI_SCAN_FAILED;handleWiFiScan();assert(status==502);assert(!wifiScanStarted);
 feedBusy=true;handleFeedJob();assert(status==202);handleFeedsSync();assert(status==409);assert(syncCalls==0);
 feedBusy=false;rebootPending=true;handleFeedsSync();assert(status==409);assert(syncCalls==0);rebootPending=false;
 handleFeedJob();assert(status==200);assert(!deserializeJson(app,response.c_str()));assert(app["meta"]["syncPending"]==false);
 server.args["scope"]="home";handleFeedsSync();assert(status==202);assert(syncCalls==1);assert(syncScope=="home");
 syncAccept=false;handleFeedsSync();assert(status==400);syncAccept=true;
 // OTA starts are rejected before flash mutation while network/feed work is active.
 feedBusy=true;uploadState.status=UPLOAD_FILE_START;handleOTAUpload();assert(Update.begins==0);assert(!otaUploadComplete);handleOTADone();assert(status==409);assert(!rebootPending);
 feedBusy=false;wifiConnectTask.active=true;handleOTAUpload();assert(Update.begins==0);wifiConnectTask.active=false;
 handleOTAUpload();assert(Update.begins==1);assert(Update.running);assert(!otaUploadComplete);
 uploadState.status=UPLOAD_FILE_ABORTED;handleOTAUpload();assert(!Update.running);handleOTADone();assert(status==409);assert(!rebootPending);
 uploadState.status=UPLOAD_FILE_START;handleOTAUpload();Update.writeSuccess=false;uint8_t bytes[4]={0};uploadState.buf=bytes;uploadState.currentSize=4;uploadState.status=UPLOAD_FILE_WRITE;handleOTAUpload();uploadState.status=UPLOAD_FILE_END;handleOTAUpload();assert(!otaUploadComplete);handleOTADone();assert(status==409);assert(!Update.running);assert(!rebootPending);
 Update.writeSuccess=true;uploadState.status=UPLOAD_FILE_START;handleOTAUpload();uploadState.status=UPLOAD_FILE_WRITE;handleOTAUpload();uploadState.status=UPLOAD_FILE_END;handleOTAUpload();assert(otaUploadComplete);assert(!Update.running);handleOTADone();assert(status==200);assert(rebootPending);
 std::cout<<"Webserver regressions passed: OTA rotation, API validation, NTP updates, active hostname\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder);(path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-I'+str(ROOT/'.pio/libdeps/nodemcuv2/ArduinoJson/src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
