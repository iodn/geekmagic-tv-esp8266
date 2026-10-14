#ifndef FEEDS_H
#define FEEDS_H

#include <Arduino.h>
#include "dashboard.h"

#define FEEDS_CONFIG_PATH "/feeds-config.json"
#define FEEDS_CONFIG_VERSION 2
#define HOME_ASSISTANT_SLOT_COUNT 4

enum WeatherFeedSource : uint8_t {
    WEATHER_FEED_DISABLED = 0,
    WEATHER_FEED_MANUAL,
    WEATHER_FEED_OPEN_METEO
};

enum MarketFeedSource : uint8_t {
    MARKET_FEED_DISABLED = 0,
    MARKET_FEED_MANUAL,
    MARKET_FEED_FINNHUB,
    MARKET_FEED_COINGECKO
};

enum HomeAssistantTlsMode : uint8_t {
    HOME_ASSISTANT_TLS_INSECURE = 0,
    HOME_ASSISTANT_TLS_FINGERPRINT
};

struct WeatherFeedConfig {
    uint8_t source;
    char query[40];
    char label[24];
    float latitude;
    float longitude;
    uint16_t refreshMinutes;
    bool useFahrenheit;
};

struct MarketFeedConfig {
    uint8_t source;
    char symbol[24];
    char label[16];
    char currency[8];
    uint16_t refreshMinutes;
};

struct HomeAssistantSlotConfig {
    bool enabled;
    char entityId[72];
    char label[24];
    char unit[12];
};

struct HomeAssistantConfig {
    bool enabled;
    char baseUrl[128];
    char token[224];
    uint8_t tlsMode;
    char fingerprint[41];
    uint16_t refreshMinutes;
    HomeAssistantSlotConfig slots[HOME_ASSISTANT_SLOT_COUNT];
};

struct FeedConfig {
    uint16_t version;
    char finnhubApiKey[48];
    WeatherFeedConfig weather;
    MarketFeedConfig markets[DASHBOARD_MARKET_COUNT];
    HomeAssistantConfig homeAssistant;
};

struct WeatherFeedRuntime {
    bool hasData;
    bool syncing;
    uint32_t lastAttemptMs;
    uint32_t lastSuccessMs;
    char lastError[64];
    WeatherData data;
};

struct MarketFeedRuntime {
    bool hasData;
    bool syncing;
    uint32_t lastAttemptMs;
    uint32_t lastSuccessMs;
    char lastError[64];
    MarketData data;
};

struct HomeAssistantSlotData {
    bool enabled;
    bool hasData;
    bool numeric;
    bool stale;
    uint32_t lastAttemptMs;
    uint32_t lastSuccessMs;
    char lastError[64];
    char entityId[72];
    char label[24];
    char state[24];
    char unit[12];
};

struct HomeAssistantRuntime {
    bool hasData;
    bool syncing;
    uint32_t lastAttemptMs;
    uint32_t lastSuccessMs;
    char lastError[64];
    HomeAssistantSlotData slots[HOME_ASSISTANT_SLOT_COUNT];
};

struct FeedRuntimeState {
    WeatherFeedRuntime weather;
    MarketFeedRuntime markets[DASHBOARD_MARKET_COUNT];
    HomeAssistantRuntime homeAssistant;
};

extern FeedConfig feedConfig;
extern FeedRuntimeState feedRuntime;

void feedsInit();
void feedsLoop();
void feedsResetToDefaults();
bool feedsLoadConfig();
bool feedsSaveConfig();
bool feedsPreviewConfigJson(const String &json, String *error = nullptr);
bool feedsApplyConfigJson(const String &json, String *error = nullptr);
void feedsDiscardDraftChanges();
bool feedsHasDraftChanges();
void feedsBuildStateJson(String &json);
bool feedsSyncNow(const char *scope = "all", String *error = nullptr);
bool feedsBusy();
void feedsCancelJobs();
enum FeedSearchStatus : uint8_t { FEED_SEARCH_IDLE, FEED_SEARCH_PENDING, FEED_SEARCH_READY, FEED_SEARCH_FAILED };
bool feedsQueueWeatherSearch(const String &query, String *error = nullptr);
FeedSearchStatus feedsWeatherSearchResult(String &json, String *error = nullptr);
bool feedsSearchWeatherLocations(const String &query, String &json, String *error = nullptr);

uint8_t feedsWeatherSource();
uint8_t feedsMarketSource(uint8_t index);
bool feedsHomeAssistantConfigured();
bool feedsHasHomeAssistantData();
const HomeAssistantSlotData* feedsHomeAssistantSlotData(uint8_t index);
bool feedsWeatherConfigured();
bool feedsMarketConfigured(uint8_t index);
bool feedsHasWeatherData();
bool feedsHasMarketData(uint8_t index);
bool feedsWeatherUsesFahrenheit();
const WeatherData* feedsWeatherData();
const MarketData* feedsMarketData(uint8_t index);

#endif
