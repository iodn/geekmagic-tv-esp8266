"""Host regressions for feed formatting, parsing, HA errors and cooperative jobs.

The worker uses the real source-extracted pump with a ucontext continuation shim.
Radio/TLS/Xtensa assembly still require device verification.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(name):
    source = (ROOT / "src/feeds.cpp").read_text()
    match = re.search(r"^(?:String|bool|void|FeedSearchStatus|const HomeAssistantSlotData\*) " + name + r"\([^;{]*\)\s*\{", source, re.M)
    assert match, name
    begin = source.index("{", match.start())
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    mock_header = r'''
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
'''
    (path / "Arduino.h").write_text(mock_header)
    code = r'''
#include "Arduino.h"
#include "feeds.h"
#include <cassert>
#include <cmath>
#include <memory>
#include <new>
#include <ucontext.h>
#include <iostream>
#include <vector>
using std::isfinite;
FeedConfig feedConfig{};
FeedRuntimeState feedRuntime{};
unsigned long clockMs = 1;
unsigned long millis() { return clockMs; }
struct cont_t { ucontext_t context, caller; char stack[262144]; bool initialized = false; };
cont_t mainCont;
cont_t *g_pcont = &mainCont;
int workerStarts=0;
void cont_init(cont_t*) {++workerStarts;}
void cont_suspend(cont_t *c) { swapcontext(&c->context, &c->caller); }
void yield() { cont_suspend(g_pcont); }
void cont_run(cont_t *c, void (*worker)()) {
    if (!c->initialized) {
        getcontext(&c->context);
        c->context.uc_stack.ss_sp = c->stack;
        c->context.uc_stack.ss_size = sizeof(c->stack);
        c->context.uc_link = &c->caller;
        makecontext(&c->context, worker, 0);
        c->initialized = true;
    }
    swapcontext(&c->caller, &c->context);
}
void cont_check(cont_t*) {}
struct WiFiClient { bool aborted = false; void abort() { aborted = true; } };
struct FeedHttpClient : WiFiClient { void abortTransport() { aborted = true; } };
struct FeedTlsClient : WiFiClient { void abortTransport() { aborted = true; } };
FeedTlsClient *activeTlsClient = nullptr;
struct { int status() { return 3; } } WiFi;
constexpr int WL_CONNECTED = 3;
void logPrint(const char*) {}
void logPrintf(const char*,...) {}
bool feedsHomeAssistantConfigured() { return true; }
bool failAll = false;
bool syncHomeAssistantSlot(uint8_t index, String *error) {
    auto &slot = feedRuntime.homeAssistant.slots[index];
    if (index == 0 && !failAll) {
        slot.hasData = true; slot.lastSuccessMs = millis();
        slot.stale = false; slot.lastError[0] = 0;
        return true;
    }
    *error = "HTTP 404";
    return false;
}
std::unique_ptr<cont_t> feedContinuation;
uint16_t pendingSyncMask = 0, activeSyncMask = 0;
bool feedJobRunning = false, feedJobComplete = false, lastSyncSuccess = false;
bool feedJobsCancelled = false;
bool feedBatchActive = false;
String lastSyncError;
FeedHttpClient *activeHttpClient = nullptr;
uint32_t activeRequestStartedMs = 0;
bool activeRequestExpired = false;
constexpr uint32_t kRequestDeadlineMs = 24000;
FeedSearchStatus searchStatus = FEED_SEARCH_IDLE;
String searchQuery, searchJson, searchError;
constexpr uint16_t kWeatherJob=1, kHomeAssistantJob=2, kMarketJobBase=4;
bool syncWeather(String *error) {
    FeedHttpClient client;
    activeHttpClient = &client;
    activeRequestStartedMs = millis();
    while (!client.aborted) yield();
    activeHttpClient = nullptr;
    *error = "Request deadline exceeded";
    return false;
}
std::vector<int> marketCalls;
int failedMarket=-1;
bool syncMarket(uint8_t index, String*error) {marketCalls.push_back(index);yield();if(index==failedMarket){*error="HTTP 404";return false;}return true;}
bool feedsSearchWeatherLocations(const String&, String &json, String*) {
    yield(); yield(); json = "search result"; return true;
}
'''
    source = (ROOT / "src/feeds.cpp").read_text()
    begin = source.index("class FeedJsonStream")
    end = source.index("\n};", begin) + 4
    code += r'''
constexpr unsigned kHttpTimeoutMs = 12000;
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int peek() = 0;
    virtual int read() = 0;
    virtual size_t readBytes(char*, size_t) = 0;
    virtual size_t write(uint8_t) = 0;
    void setTimeout(unsigned) {}
};
struct MockBody : Stream {
    bool ready;
    explicit MockBody(bool value) : ready(value) {}
    int available() override { return ready; }
    int peek() override { return ready ? 'x' : -1; }
    int read() override { return peek(); }
    size_t readBytes(char*, size_t) override { return 0; }
    size_t write(uint8_t) override { return 0; }
};
''' + source[begin:end] + r'''
bool parserDone = false;
size_t parserReceived = 0;
void parserBulkTest() {
    MockBody source(true);
    FeedJsonStream stream(source);
    char buffer[131073];
    parserReceived = stream.readBytes(buffer, sizeof buffer);
    assert(stream.exhausted());
    parserDone = true;
}
void parserWaitingTest() {
    MockBody source(false);
    FeedJsonStream stream(source);
    char byte;
    parserReceived = stream.readBytes(&byte, 1);
    parserDone = true;
}
'''
    for name in ["copyString", "parseFloatStrict", "trimTrailingZerosString", "groupThousandsString", "formatHomeAssistantNumericState", "homeAssistantSlotConfigured", "syncHomeAssistant", "feedsHomeAssistantSlotData", "runFeedJob", "pumpFeedJob", "feedsBusy", "feedsCancelJobs", "feedsWeatherSearchResult"]:
        code += function(name) + "\n"
    code += r'''
void partialTest() {
    feedConfig.homeAssistant.slots[0].enabled = true;
    feedConfig.homeAssistant.slots[1].enabled = true;
    strcpy(feedConfig.homeAssistant.slots[0].entityId, "sensor.one");
    strcpy(feedConfig.homeAssistant.slots[1].entityId, "sensor.two");
    String error;
    syncHomeAssistant(&error);
}
int main() {
    assert(trimTrailingZerosString("1000") == "1000");
    assert(trimTrailingZerosString("1500") == "1500");
    assert(trimTrailingZerosString("10000") == "10000");
    assert(trimTrailingZerosString("-1000") == "-1000");
    assert(trimTrailingZerosString("1.5000") == "1.5");
    assert(trimTrailingZerosString("0") == "0");
    assert(formatHomeAssistantNumericState(1000) == "1,000");
    float value = 0;
    assert(!parseFloatStrict("1234567890123456789012345678901oops", value));
    assert(parseFloatStrict("-1500.50", value));
    assert(!parseFloatStrict("nan", value));
    assert(!parseFloatStrict("12abc", value));
    cont_t partial;
    cont_init(&partial);
    do { g_pcont = &partial; cont_run(&partial, partialTest); g_pcont = &mainCont; }
    while (partial.context.uc_link && feedRuntime.homeAssistant.syncing);
    assert(feedRuntime.homeAssistant.slots[0].hasData);
    assert(feedRuntime.homeAssistant.slots[1].stale);
    assert(String(feedRuntime.homeAssistant.slots[1].lastError) == "HTTP 404");
    assert(String(feedRuntime.homeAssistant.lastError) == "HTTP 404");
    assert(feedRuntime.homeAssistant.lastSuccessMs == 0);
    failAll = true;
    cont_t failed;
    cont_init(&failed);
    do { g_pcont = &failed; cont_run(&failed, partialTest); g_pcont = &mainCont; }
    while (feedRuntime.homeAssistant.syncing);
    assert(feedRuntime.homeAssistant.slots[0].hasData);
    assert(feedRuntime.homeAssistant.slots[0].stale);
    // First-poll failures must remain observable without a cached state.
    assert(feedsHomeAssistantSlotData(1) == &feedRuntime.homeAssistant.slots[1]);
    assert(!feedRuntime.homeAssistant.slots[1].hasData);
    assert(feedsHomeAssistantSlotData(0) == &feedRuntime.homeAssistant.slots[0]);
    assert(feedsHomeAssistantSlotData(2) == nullptr);
    assert(feedsHomeAssistantSlotData(HOME_ASSISTANT_SLOT_COUNT) == nullptr);
    assert(feedsHomeAssistantSlotData(255) == nullptr);
    auto &unused = feedRuntime.homeAssistant.slots[2];
    unused.enabled=true;
    assert(feedsHomeAssistantSlotData(2) == nullptr);
    strcpy(unused.lastError, "HTTP 401");
    assert(feedsHomeAssistantSlotData(2) == &unused);
    unused.enabled=false;
    assert(feedsHomeAssistantSlotData(2) == nullptr);
    pendingSyncMask = kWeatherJob;
    unsigned heartbeat = 0;
    while (feedsBusy()) {
        pumpFeedJob();
        assert(g_pcont == &mainCont);
        ++heartbeat; clockMs += 1000;
        assert(heartbeat < 40);
    }
    assert(heartbeat >= 24);
    assert(activeRequestExpired);
    assert(!lastSyncSuccess && !lastSyncError.isEmpty());
    assert(!feedContinuation);
    pendingSyncMask = kWeatherJob;
    activeRequestExpired = false;
    pumpFeedJob();
    assert(feedsBusy());
    feedsCancelJobs();
    heartbeat = 0;
    while (feedsBusy()) { pumpFeedJob(); ++heartbeat; assert(heartbeat < 5); }
    assert(!lastSyncSuccess && lastSyncError == "Feed job cancelled");
    assert(g_pcont == &mainCont && !feedContinuation);
    feedJobsCancelled = false;
    int starts=workerStarts;
    pendingSyncMask=kMarketJobBase|(kMarketJobBase<<1)|(kMarketJobBase<<2);
    failedMarket=1;marketCalls.clear();heartbeat=0;
    while(feedsBusy()) {
        pumpFeedJob();++heartbeat;assert(heartbeat<20);
        if(feedJobRunning)assert((activeSyncMask&(activeSyncMask-1))==0);
        assert(g_pcont==&mainCont);
    }
    assert(workerStarts-starts==3&&marketCalls==std::vector<int>({0,1,2}));
    assert(!lastSyncSuccess&&lastSyncError=="HTTP 404"&&!feedContinuation);
    starts=workerStarts;failedMarket=-1;marketCalls.clear();
    pendingSyncMask=kMarketJobBase|(kMarketJobBase<<1);heartbeat=0;
    while(feedsBusy()){pumpFeedJob();assert(++heartbeat<12);}
    assert(workerStarts-starts==2&&lastSyncSuccess&&lastSyncError.isEmpty());
    pendingSyncMask=kHomeAssistantJob|kMarketJobBase;marketCalls.clear();pumpFeedJob();
    assert(activeSyncMask==kMarketJobBase&&marketCalls==std::vector<int>({0}));
    feedsCancelJobs();heartbeat=0;
    while(feedsBusy()){pumpFeedJob();assert(++heartbeat<5);}
    feedJobsCancelled=false;
    pendingSyncMask=kMarketJobBase|(kMarketJobBase<<1);marketCalls.clear();
    do{pumpFeedJob();}while(feedJobRunning);
    assert(feedsBusy()&&feedBatchActive&&!feedContinuation);
    feedsCancelJobs();assert(!feedsBusy()&&!feedBatchActive&&!lastSyncSuccess&&lastSyncError=="Feed job cancelled");
    feedJobsCancelled=false;
    pendingSyncMask=kWeatherJob|kMarketJobBase;marketCalls.clear();pumpFeedJob();
    feedsCancelJobs();heartbeat=0;
    while(feedsBusy()){pumpFeedJob();assert(++heartbeat<5);}
    assert(marketCalls.empty()&&!lastSyncSuccess&&lastSyncError=="Feed job cancelled");
    feedJobsCancelled = false;
    searchStatus = FEED_SEARCH_PENDING;
    heartbeat = 0;
    while (feedsBusy()) { pumpFeedJob(); ++heartbeat; assert(heartbeat < 8); }
    String result;
    assert(feedsWeatherSearchResult(result) == FEED_SEARCH_READY);
    assert(result == "search result" && heartbeat == 3);
    assert(g_pcont == &mainCont);
    cont_t parser;
    activeRequestExpired = false;
    heartbeat = 0;
    do {
        g_pcont = &parser; cont_run(&parser, parserBulkTest); g_pcont = &mainCont;
        ++heartbeat;
        assert(heartbeat < 600);
    } while (!parserDone);
    assert(parserReceived == 131072 && heartbeat == 513);
    cont_t waiting;
    parserDone = false; heartbeat = 0;
    do {
        g_pcont = &waiting; cont_run(&waiting, parserWaitingTest); g_pcont = &mainCont;
        if (++heartbeat == 3) activeRequestExpired = true;
        assert(heartbeat < 6);
    } while (!parserDone);
    assert(parserReceived == 0 && heartbeat == 4);
    std::cout << "Feed regressions passed: numeric boundaries, strict parsing, partial/stale HA, yielding/resumed jobs, deadline abort and search.\n";
}
'''
    (path / "test.cpp").write_text(code)
    subprocess.run(["g++", "-std=c++17", "-I" + str(path), "-I" + str(ROOT / "src"), str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
