"""Exercise the firmware WiFi and mDNS state machines with mocked radio and time."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/main.cpp').read_text()
def extract(name, filename="main.cpp"):
    source=(root/"src"/filename).read_text()
    match=re.search(r'^(?:bool|void) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert match,name
    start=source.index('{',match.start());depth=1;end=start+1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
code=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
using uint32_t=unsigned int;
class String:public std::string {public:using std::string::string;String(const std::string &s):std::string(s){}bool isEmpty()const{return empty();}};
#define F(x) x
constexpr int WIFI_AP_STA=3,WIFI_STA=1,WL_CONNECTED=3;
constexpr unsigned long WIFI_CONNECTION_TIMEOUT=30000UL,WIFI_RECONNECT_INTERVAL=300000UL;
constexpr char WIFI_AP_NAME[]="SmartClock-Setup";
constexpr int SETTINGS_HOSTNAME_LENGTH=32,WEB_SERVER_PORT=80;

unsigned long now=100;
unsigned long millis(){return now;}
struct IPAddress {int value;IPAddress(int a,int b,int c,int d):value(a|b|c|d){}bool operator!=(const IPAddress&o)const{return value!=o.value;}String toString()const{return value?"192.168.1.5":"0.0.0.0";}};
struct {
 String ssid="saved-network",host="clock",password="saved-password",flashSsid="saved-network",flashPassword="saved-password";bool persistentValue=true;bool ap=false,apSuccess=true;int modeValue=0,stationStatus=0,beginCalls=0,disconnectCalls=0;IPAddress ip{0,0,0,0};
 void hostname(const char*value){host=value;}String hostname(){return host;}String SSID(){return ssid;}
 void mode(int value){modeValue=value;}bool softAP(const char*,const char*){ap=apSuccess;return ap;}
 void softAPdisconnect(bool){ap=false;}void setAutoReconnect(bool){}void begin(){++beginCalls;}
 String psk(){return password;}bool getPersistent(){return persistentValue;}void persistent(bool value){persistentValue=value;}
 void begin(const char*name,const char*pass){ssid=name;password=pass;++beginCalls;if(persistentValue){flashSsid=ssid;flashPassword=password;}stationStatus=0;ip=IPAddress(0,0,0,0);}
 void disconnect(bool,bool erase){++disconnectCalls;if(erase){ssid="";password="";if(persistentValue){flashSsid="";flashPassword="";}}stationStatus=0;ip=IPAddress(0,0,0,0);}
 int status(){return stationStatus;}IPAddress localIP(){return ip;}IPAddress softAPIP(){return IPAddress(192,168,4,1);}
} WiFi;
struct {int starts=0,stops=0;bool active=false;void start(int port,const char*domain,IPAddress){assert(port==53);assert(std::string(domain)=="*");++starts;active=true;}void stop(){++stops;active=false;}} setupDns;
struct {bool apMode=false,showImage=false;char ipInfo[24]={};} displayState;
struct {char deviceName[32]="clock";} appSettings;
void settingsBuildHostname(const char*name,char*buffer,size_t n){snprintf(buffer,n,"%s",name);}
void displayShowAPScreen(const char*,const char*,const char*){displayState.apMode=true;}
using __FlashStringHelper=char;
void displayShowMessage(const String&){}void logPrint(const String&){}
int restoredDashboard=0;void displayShowDashboard(){++restoredDashboard;displayState.apMode=false;assert(std::string(displayState.ipInfo)=="192.168.1.5");}void logPrintf(const char*,...){}void logHeapState(const char*){}void logOptionalServiceDelay(const char*,uint32_t){}
String generateRandomPassword(int){return "12345678";}
struct {bool success=false;int beginCalls=0,endCalls=0;bool begin(const char*){++beginCalls;return success;}void end(){++endCalls;}void addService(const char*,const char*,int){}void addServiceTxt(const char*,const char*,const char*,const char*){}} MDNS;
struct {void println(const char*){}void printf(const char*,...){}} Serial;
struct {int ends=0;void end(){++ends;}} ArduinoOTA;
struct {uint32_t heap=40000;uint32_t getFreeHeap(){return heap;}} ESP;
bool wifiFailsafeMode=false,recoveryBootMode=false,timeServicesStarted=true,mdnsStarted=false,otaStarted=false;
unsigned long bootCompletedAt=1,lastWiFiReconnectAttempt=0,lastMdnsStartAttempt=0;
String apPassword="12345678";
bool wifiReconnectActive=false,setupApStarted=false;
unsigned long wifiReconnectStartedAt=0,lastApStartAttempt=0;
void startTimeServices(){timeServicesStarted=true;}
void setupOTA(){otaStarted=true;}
'''
for constant in ['kOptionalServiceWarmupMs','kOptionalServiceMinFreeHeapBytes']:
    code+='\n'+re.search(r'constexpr uint32_t '+constant+r' = [^;]+;',source).group(0)
web=(root/'src/webserver.cpp').read_text()
code+='\n'+re.search(r'struct WiFiConnectTask \{[\s\S]*?\};',web).group(0)+'\nWiFiConnectTask wifiConnectTask{};\n'
for constant in ['kWiFiConnectTimeoutMs','kWiFiIpWaitTimeoutMs']:
    code+='\n'+re.search(r'constexpr unsigned long '+constant+r' = [^;]+;',web).group(0)
code+='\nint restarts=0;void queueRestart(){++restarts;}\n'
for name in ['copyConfiguredHostname','applyConfiguredHostname','ensureSetupAP','startWiFiReconnect','trackWiFiReconnectAttempt','setupWiFi','monitorWiFi','setupMDNS','startDeferredNetworkServices']:code+='\n'+extract(name)
for name in ["copyString","startQueuedWiFiConnect","failWiFiConnectTask","processWiFiConnectTask"]:
    code+="\n"+extract(name,"webserver.cpp")
code+=r'''
int main(){
 setupWiFi();assert(now==100);assert(WiFi.beginCalls==1);assert(WiFi.ap);assert(WiFi.modeValue==WIFI_AP_STA);assert(wifiFailsafeMode);assert(wifiReconnectActive);assert(setupDns.starts==1);assert(setupDns.active);
 int dnsStarts=setupDns.starts;monitorWiFi();monitorWiFi();assert(setupDns.starts==dnsStarts);
 assert(WiFi.ssid=="saved-network");assert(WiFi.disconnectCalls==0);
 now+=WIFI_CONNECTION_TIMEOUT-1;monitorWiFi();assert(wifiReconnectActive);assert(WiFi.ap);
 now++;monitorWiFi();assert(!wifiReconnectActive);assert(WiFi.ap);assert(WiFi.ssid=="saved-network");
 now=lastWiFiReconnectAttempt+WIFI_RECONNECT_INTERVAL-1;monitorWiFi();assert(WiFi.beginCalls==1);
 now++;monitorWiFi();assert(wifiReconnectActive);assert(WiFi.beginCalls==2);assert(WiFi.ap);
 WiFi.stationStatus=WL_CONNECTED;WiFi.ip=IPAddress(192,168,1,5);monitorWiFi();assert(!wifiFailsafeMode);assert(!wifiReconnectActive);assert(!WiFi.ap);assert(WiFi.modeValue==WIFI_STA);assert(!displayState.apMode);assert(setupDns.stops==1);assert(!setupDns.active);assert(restoredDashboard==1);
 ESP.heap=7000;int serviceStarts=MDNS.beginCalls;startDeferredNetworkServices();assert(MDNS.beginCalls==serviceStarts);
 ESP.heap=20000;
 MDNS.success=false;startDeferredNetworkServices();assert(!mdnsStarted);assert(MDNS.beginCalls==1);
 now+=4999;startDeferredNetworkServices();assert(MDNS.beginCalls==1);assert(!otaStarted);
 now++;MDNS.success=true;startDeferredNetworkServices();assert(mdnsStarted);assert(MDNS.beginCalls==2);
 startDeferredNetworkServices();assert(otaStarted);
 // Losing the station must stop optional services and open setup access again.
 WiFi.stationStatus=0;WiFi.ip=IPAddress(0,0,0,0);monitorWiFi();assert(wifiFailsafeMode);assert(WiFi.ap);assert(!mdnsStarted);assert(!otaStarted);assert(ArduinoOTA.ends==1);
 // No credentials: setup is immediate and AP-only station retries are omitted.
 WiFi.ssid="";wifiReconnectActive=false;setupApStarted=false;WiFi.ap=false;lastApStartAttempt=0;int begins=WiFi.beginCalls;setupWiFi();assert(WiFi.ap);assert(WiFi.beginCalls==begins);
 // An AP startup failure is retried, but bounded to one attempt every two seconds.
 setupApStarted=false;WiFi.ap=false;WiFi.apSuccess=false;lastApStartAttempt=0;dnsStarts=setupDns.starts;ensureSetupAP();assert(!setupApStarted);assert(setupDns.starts==dnsStarts);
 WiFi.apSuccess=true;now+=1999;ensureSetupAP();assert(!setupApStarted);now++;ensureSetupAP();assert(setupApStarted);assert(setupDns.starts==dnsStarts+1);assert(setupDns.active);monitorWiFi();assert(setupDns.starts==dnsStarts+1);
 // Trying a new network must preserve the previous flash credential until success.
 WiFi.ssid="saved-network";WiFi.password="saved-password";WiFi.persistentValue=true;
 strcpy(wifiConnectTask.ssid,"new-network");strcpy(wifiConnectTask.password,"new-password");wifiConnectTask.queued=true;
 wifiReconnectActive=true;wifiReconnectStartedAt=now-WIFI_CONNECTION_TIMEOUT;
 unsigned long before=now;processWiFiConnectTask();assert(now==before);assert(!wifiReconnectActive);assert(wifiConnectTask.active);assert(WiFi.ap);assert(WiFi.flashSsid=="saved-network");assert(!WiFi.persistentValue);
 now+=kWiFiConnectTimeoutMs+1;processWiFiConnectTask();assert(!wifiConnectTask.active);assert(WiFi.ssid=="saved-network");assert(WiFi.password=="saved-password");assert(WiFi.flashSsid=="saved-network");assert(WiFi.ap);assert(WiFi.persistentValue);assert(wifiConnectTask.password[0]==0);assert(wifiConnectTask.previousPassword[0]==0);
 // Restoring the previous station receives a fresh deadline, rather than the
 // startup attempt's expired deadline immediately disconnecting it again.
 assert(wifiReconnectActive);assert(wifiReconnectStartedAt==now);int disconnects=WiFi.disconnectCalls;
 monitorWiFi();assert(wifiReconnectActive);assert(WiFi.disconnectCalls==disconnects);
 now+=WIFI_CONNECTION_TIMEOUT-1;monitorWiFi();assert(wifiReconnectActive);assert(WiFi.disconnectCalls==disconnects);
 now++;monitorWiFi();assert(!wifiReconnectActive);assert(WiFi.disconnectCalls==disconnects+1);assert(WiFi.ap);
 // A successful association and IP acquisition persist only the requested network.
 strcpy(wifiConnectTask.password,"new-password");wifiConnectTask.queued=true;processWiFiConnectTask();WiFi.stationStatus=WL_CONNECTED;WiFi.ip=IPAddress(192,168,1,5);processWiFiConnectTask();assert(restarts==1);assert(WiFi.flashSsid=="new-network");assert(WiFi.flashPassword=="new-password");assert(wifiConnectTask.password[0]==0);assert(wifiConnectTask.previousPassword[0]==0);
 // With no prior network, failed candidate credentials are cleared only in RAM.
 WiFi.ssid="";WiFi.password="";WiFi.flashSsid="";WiFi.flashPassword="";WiFi.stationStatus=0;WiFi.ip=IPAddress(0,0,0,0);
 strcpy(wifiConnectTask.password,"bad-password");wifiConnectTask.queued=true;processWiFiConnectTask();
 assert(WiFi.ssid=="new-network");assert(WiFi.flashSsid.empty());
 now+=kWiFiConnectTimeoutMs+1;processWiFiConnectTask();assert(!wifiConnectTask.active);assert(!wifiReconnectActive);assert(WiFi.ssid.empty());assert(WiFi.password.empty());assert(WiFi.flashSsid.empty());assert(WiFi.flashPassword.empty());assert(WiFi.ap);assert(WiFi.persistentValue);
 begins=WiFi.beginCalls;now+=WIFI_RECONNECT_INTERVAL;monitorWiFi();assert(WiFi.beginCalls==begins);assert(WiFi.ap);
 std::cout<<"WiFi/mDNS regressions passed: credentials, AP retries, nonblocking start, service retry\n";
}
'''
with tempfile.TemporaryDirectory() as folder:
    d=Path(folder);(d/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
