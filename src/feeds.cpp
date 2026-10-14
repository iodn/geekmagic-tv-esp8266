#include "feeds.h"

#include "display.h"
#include "logger.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiClientSecureBearSSL.h>
#include <ctype.h>
#include <cont.h>
#include <lwip/tcp.h>
#include <include/ClientContext.h>
#include <limits.h>
#include <math.h>

FeedConfig feedConfig;
FeedRuntimeState feedRuntime;

namespace {

constexpr uint16_t kWeatherMinRefreshMinutes = 5;
constexpr uint16_t kWeatherMaxRefreshMinutes = 240;
constexpr uint16_t kMarketMinRefreshMinutes = 1;
constexpr uint16_t kMarketMaxRefreshMinutes = 240;
constexpr uint16_t kHomeAssistantMinRefreshMinutes = 1;
constexpr uint16_t kHomeAssistantMaxRefreshMinutes = 240;
constexpr uint32_t kHttpTimeoutMs = 12000;
constexpr uint32_t kFeedStartupGracePeriodMs = 15000UL;
constexpr uint32_t kHttpsMinFreeHeapBytes = 24000UL;
constexpr uint16_t kHttpsDefaultRecvBufferBytes = 16384;
constexpr uint16_t kHttpsCompactXmitBufferBytes = 512;
constexpr uint32_t kRequestDeadlineMs = 24000UL;

FeedConfig savedFeedConfig;
bool feedDraftActive = false;
uint32_t feedsStartupReadyAtMs = 0;

struct HttpJsonRequestOptions {
    const char *authorizationBearer;
    uint8_t tlsMode;
    const char *fingerprint;
};

// Use the actual BearSSL socket context: the public WiFiClientSecure wrapper
// has no base socket, and abort() would otherwise flush TLS while suspended.
class FeedTlsClient : public BearSSL::WiFiClientSecureCtx {
public:
    void abortTransport() { if (_client) _client->abort(); }
};
FeedTlsClient *activeTlsClient = nullptr;
std::unique_ptr<cont_t> feedContinuation;
uint16_t pendingSyncMask = 0;
uint16_t activeSyncMask = 0;
bool feedJobRunning = false;
bool feedJobComplete = false;
bool feedJobsCancelled = false;
bool lastSyncSuccess = false;
String lastSyncError;
WiFiClient *activeHttpClient = nullptr;
uint32_t activeRequestStartedMs = 0;
bool activeRequestExpired = false;
FeedSearchStatus searchStatus = FEED_SEARCH_IDLE;
String searchQuery, searchJson, searchError;


bool feedConfigEquals(const FeedConfig &left, const FeedConfig &right);
void updateDraftState();
void copyString(char *destination, size_t destinationSize, const char *source);

bool buildTempPath(const char *path, char *tempPath, size_t tempPathSize) {
    int tempPathLength = snprintf(tempPath, tempPathSize, "%s.tmp", path);
    return tempPathLength > 0 && static_cast<size_t>(tempPathLength) < tempPathSize;
}

void copyString(char *destination, size_t destinationSize, const char *source) {
    if (destinationSize == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

void lowercaseCString(char *value) {
    if (value == nullptr) {
        return;
    }

    for (size_t index = 0; value[index] != '\0'; ++index) {
        value[index] = static_cast<char>(tolower(value[index]));
    }
}

uint16_t clampWeatherRefresh(uint16_t minutes) {
    return constrain(minutes, kWeatherMinRefreshMinutes, kWeatherMaxRefreshMinutes);
}

uint16_t clampMarketRefresh(uint16_t minutes) {
    return constrain(minutes, kMarketMinRefreshMinutes, kMarketMaxRefreshMinutes);
}

uint16_t clampHomeAssistantRefresh(uint16_t minutes) {
    return constrain(minutes, kHomeAssistantMinRefreshMinutes, kHomeAssistantMaxRefreshMinutes);
}

bool weatherCoordinatesValid(float latitude, float longitude) {
    return isfinite(latitude) &&
           isfinite(longitude) &&
           latitude >= -90.0f &&
           latitude <= 90.0f &&
           longitude >= -180.0f &&
           longitude <= 180.0f &&
           (fabsf(latitude) > 0.0001f || fabsf(longitude) > 0.0001f);
}

const char* weatherSourceToString(uint8_t source) {
    switch (source) {
        case WEATHER_FEED_OPEN_METEO:
            return "open-meteo";
        case WEATHER_FEED_DISABLED:
        default:
            return "disabled";
    }
}

const char* marketSourceToString(uint8_t source) {
    switch (source) {
        case MARKET_FEED_FINNHUB:
            return "finnhub";
        case MARKET_FEED_COINGECKO:
            return "coingecko";
        case MARKET_FEED_DISABLED:
        default:
            return "disabled";
    }
}

const char* homeAssistantTlsModeToString(uint8_t mode) {
    switch (mode) {
        case HOME_ASSISTANT_TLS_FINGERPRINT:
            return "fingerprint";
        case HOME_ASSISTANT_TLS_INSECURE:
        default:
            return "insecure";
    }
}

uint8_t weatherSourceFromVariant(JsonVariantConst value) {
    if (value.isNull()) {
        return feedConfig.weather.source;
    }

    if (value.is<const char*>()) {
        String source = value.as<const char*>();
        source.toLowerCase();

        if (source == "open-meteo" || source == "openmeteo" || source == "meteo") {
            return WEATHER_FEED_OPEN_METEO;
        }
        return WEATHER_FEED_DISABLED;
    }

    return static_cast<uint8_t>(value.as<int>());
}

uint8_t marketSourceFromVariant(JsonVariantConst value) {
    if (value.isNull()) {
        return MARKET_FEED_DISABLED;
    }

    if (value.is<const char*>()) {
        String source = value.as<const char*>();
        source.toLowerCase();

        if (source == "finnhub") {
            return MARKET_FEED_FINNHUB;
        }
        if (source == "coingecko" || source == "coin-gecko") {
            return MARKET_FEED_COINGECKO;
        }
        return MARKET_FEED_DISABLED;
    }

    return static_cast<uint8_t>(value.as<int>());
}

uint8_t homeAssistantTlsModeFromVariant(JsonVariantConst value) {
    if (value.isNull()) {
        return HOME_ASSISTANT_TLS_INSECURE;
    }

    if (value.is<const char*>()) {
        String mode = value.as<const char*>();
        mode.toLowerCase();

        if (mode == "fingerprint" || mode == "pinned" || mode == "pin") {
            return HOME_ASSISTANT_TLS_FINGERPRINT;
        }
        return HOME_ASSISTANT_TLS_INSECURE;
    }

    return static_cast<uint8_t>(value.as<int>());
}

String urlEncode(const String &value) {
    static const char hex[] = "0123456789ABCDEF";
    String encoded;
    encoded.reserve(value.length() * 3);

    for (size_t index = 0; index < value.length(); ++index) {
        uint8_t character = static_cast<uint8_t>(value.charAt(index));
        if (isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
            encoded += static_cast<char>(character);
            continue;
        }

        encoded += '%';
        encoded += hex[(character >> 4) & 0x0F];
        encoded += hex[character & 0x0F];
    }

    return encoded;
}

String trimmedLowercaseString(const String &value) {
    String normalized = value;
    normalized.trim();
    normalized.toLowerCase();
    return normalized;
}

String normalizeCoinGeckoLookupValue(const String &value) {
    String normalized = trimmedLowercaseString(value);
    normalized.replace(" ", "-");
    normalized.replace("_", "-");
    return normalized;
}

String trimStringCopy(const char *value) {
    String normalized = value != nullptr ? String(value) : String("");
    normalized.trim();
    return normalized;
}

void trimAndCopyString(char *destination, size_t destinationSize, const char *source) {
    String normalized = trimStringCopy(source);
    copyString(destination, destinationSize, normalized.c_str());
}

bool isHexDigitChar(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'A' && value <= 'F') ||
           (value >= 'a' && value <= 'f');
}

void normalizeFingerprint(char *value, size_t valueSize) {
    if (value == nullptr || valueSize == 0) {
        return;
    }

    String normalized;
    normalized.reserve(40);
    for (size_t index = 0; value[index] != '\0'; ++index) {
        char character = value[index];
        if (!isHexDigitChar(character)) {
            continue;
        }

        normalized += static_cast<char>(toupper(character));
        if (normalized.length() == 40) {
            break;
        }
    }

    if (normalized.length() != 40) {
        value[0] = '\0';
        return;
    }

    copyString(value, valueSize, normalized.c_str());
}

bool baseUrlValid(const String &baseUrl) {
    return baseUrl.startsWith("http://") || baseUrl.startsWith("https://");
}

void normalizeBaseUrl(char *value, size_t valueSize) {
    if (value == nullptr || valueSize == 0) {
        return;
    }

    String normalized = trimStringCopy(value);
    while (normalized.endsWith("/")) {
        normalized.remove(normalized.length() - 1);
    }
    copyString(value, valueSize, normalized.c_str());
}

bool parseFloatStrict(const String &value, float &parsedValue) {
    if (value.length() == 0) {
        return false;
    }

    const char *buffer = value.c_str();
    char *end = nullptr;
    parsedValue = strtof(buffer, &end);
    if (end == nullptr || end == buffer) {
        return false;
    }

    while (*end == ' ') {
        ++end;
    }
    return *end == '\0' && isfinite(parsedValue);
}

String humanizeIdentifier(const String &value) {
    String label = value;
    int entitySeparator = label.lastIndexOf('.');
    if (entitySeparator >= 0 && entitySeparator < static_cast<int>(label.length()) - 1) {
        label = label.substring(entitySeparator + 1);
    }

    label.replace("_", " ");
    bool capitalizeNext = true;
    for (size_t index = 0; index < label.length(); ++index) {
        char character = label.charAt(index);
        if (capitalizeNext && character >= 'a' && character <= 'z') {
            label.setCharAt(index, static_cast<char>(toupper(character)));
            capitalizeNext = false;
        } else if (character == ' ') {
            capitalizeNext = true;
        } else {
            capitalizeNext = false;
        }
    }

    label.trim();
    return label;
}

String humanizeStateText(const String &value) {
    String normalized = trimStringCopy(value.c_str());
    if (normalized.length() == 0) {
        return "No data";
    }

    normalized.replace("_", " ");
    bool capitalizeNext = true;
    for (size_t index = 0; index < normalized.length(); ++index) {
        char character = normalized.charAt(index);
        if (capitalizeNext && character >= 'a' && character <= 'z') {
            normalized.setCharAt(index, static_cast<char>(toupper(character)));
            capitalizeNext = false;
        } else if (character == ' ') {
            capitalizeNext = true;
        } else {
            capitalizeNext = false;
        }
    }

    return normalized;
}

String buildWeatherLocationLabel(JsonObjectConst item) {
    String label = item["name"].isNull() ? String("") : String(item["name"].as<const char*>());
    if (!item["admin1"].isNull() && String(item["admin1"].as<const char*>()).length() > 0) {
        label += ", " + String(item["admin1"].as<const char*>());
    }
    if (!item["country"].isNull() && String(item["country"].as<const char*>()).length() > 0) {
        label += ", " + String(item["country"].as<const char*>());
    }
    return label;
}

void buildWeatherSearchFilter(JsonDocument &filter) {
    JsonArray results = filter["results"].to<JsonArray>();
    JsonObject item = results.add<JsonObject>();
    item["name"] = true;
    item["country"] = true;
    item["admin1"] = true;
    item["latitude"] = true;
    item["longitude"] = true;
    item["timezone"] = true;
}

void buildWeatherForecastFilter(JsonDocument &filter) {
    JsonObject current = filter["current"].to<JsonObject>();
    current["temperature_2m"] = true;
    current["weather_code"] = true;

    JsonObject daily = filter["daily"].to<JsonObject>();
    daily["temperature_2m_max"] = true;
    daily["temperature_2m_min"] = true;
    daily["precipitation_probability_max"] = true;
}

void buildCoinGeckoSearchFilter(JsonDocument &filter) {
    JsonArray coins = filter["coins"].to<JsonArray>();
    JsonObject coin = coins.add<JsonObject>();
    coin["id"] = true;
    coin["name"] = true;
    coin["symbol"] = true;
    coin["market_cap_rank"] = true;
}

void buildCoinGeckoMarketFilter(JsonDocument &filter) {
    JsonArray markets = filter.to<JsonArray>();
    JsonObject market = markets.add<JsonObject>();
    market["symbol"] = true;
    market["name"] = true;
    market["current_price"] = true;
    market["price_change_24h"] = true;
    market["price_change_percentage_24h"] = true;
}

void buildFinnhubQuoteFilter(JsonDocument &filter) {
    filter["c"] = true;
    filter["d"] = true;
    filter["dp"] = true;
}

void buildHomeAssistantStateFilter(JsonDocument &filter) {
    filter["entity_id"] = true;
    filter["state"] = true;

    JsonObject attributes = filter["attributes"].to<JsonObject>();
    attributes["friendly_name"] = true;
    attributes["unit_of_measurement"] = true;
}

void setConfigDefaults() {
    memset(&feedConfig, 0, sizeof(feedConfig));
    feedConfig.version = FEEDS_CONFIG_VERSION;

    feedConfig.weather.source = WEATHER_FEED_DISABLED;
    feedConfig.weather.refreshMinutes = 30;
    feedConfig.weather.useFahrenheit = false;

    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        feedConfig.markets[index].source = MARKET_FEED_DISABLED;
        feedConfig.markets[index].refreshMinutes = 10;
        copyString(feedConfig.markets[index].currency, sizeof(feedConfig.markets[index].currency), "usd");
    }

    feedConfig.homeAssistant.enabled = false;
    feedConfig.homeAssistant.tlsMode = HOME_ASSISTANT_TLS_INSECURE;
    feedConfig.homeAssistant.refreshMinutes = 2;
}

void clearWeatherRuntime() {
    memset(&feedRuntime.weather, 0, sizeof(feedRuntime.weather));
}

void clearMarketRuntime(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return;
    }

    memset(&feedRuntime.markets[index], 0, sizeof(feedRuntime.markets[index]));
}

void clearHomeAssistantRuntime() {
    memset(&feedRuntime.homeAssistant, 0, sizeof(feedRuntime.homeAssistant));
}

void clearAllRuntime() {
    clearWeatherRuntime();
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        clearMarketRuntime(index);
    }
    clearHomeAssistantRuntime();
}

void normalizeConfig() {
    feedConfig.version = FEEDS_CONFIG_VERSION;
    feedConfig.finnhubApiKey[sizeof(feedConfig.finnhubApiKey) - 1] = '\0';

    feedConfig.weather.source = constrain(feedConfig.weather.source, WEATHER_FEED_DISABLED, WEATHER_FEED_OPEN_METEO);
    if (feedConfig.weather.source == WEATHER_FEED_MANUAL) {
        feedConfig.weather.source = WEATHER_FEED_DISABLED;
    }
    feedConfig.weather.query[sizeof(feedConfig.weather.query) - 1] = '\0';
    feedConfig.weather.label[sizeof(feedConfig.weather.label) - 1] = '\0';
    feedConfig.weather.refreshMinutes = clampWeatherRefresh(feedConfig.weather.refreshMinutes);
    if (!isfinite(feedConfig.weather.latitude) || feedConfig.weather.latitude < -90.0f || feedConfig.weather.latitude > 90.0f) {
        feedConfig.weather.latitude = 0.0f;
    }
    if (!isfinite(feedConfig.weather.longitude) || feedConfig.weather.longitude < -180.0f || feedConfig.weather.longitude > 180.0f) {
        feedConfig.weather.longitude = 0.0f;
    }

    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        MarketFeedConfig &market = feedConfig.markets[index];
        market.source = constrain(market.source, MARKET_FEED_DISABLED, MARKET_FEED_COINGECKO);
        if (market.source == MARKET_FEED_MANUAL) {
            market.source = MARKET_FEED_DISABLED;
        }
        market.symbol[sizeof(market.symbol) - 1] = '\0';
        market.label[sizeof(market.label) - 1] = '\0';
        market.currency[sizeof(market.currency) - 1] = '\0';
        if (market.currency[0] == '\0') {
            copyString(market.currency, sizeof(market.currency), "usd");
        }
        lowercaseCString(market.currency);
        market.refreshMinutes = clampMarketRefresh(market.refreshMinutes);
    }

    HomeAssistantConfig &homeAssistant = feedConfig.homeAssistant;
    normalizeBaseUrl(homeAssistant.baseUrl, sizeof(homeAssistant.baseUrl));
    trimAndCopyString(homeAssistant.token, sizeof(homeAssistant.token), homeAssistant.token);
    homeAssistant.tlsMode = constrain(homeAssistant.tlsMode, HOME_ASSISTANT_TLS_INSECURE, HOME_ASSISTANT_TLS_FINGERPRINT);
    normalizeFingerprint(homeAssistant.fingerprint, sizeof(homeAssistant.fingerprint));
    homeAssistant.refreshMinutes = clampHomeAssistantRefresh(homeAssistant.refreshMinutes);

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        HomeAssistantSlotConfig &slot = homeAssistant.slots[index];
        trimAndCopyString(slot.entityId, sizeof(slot.entityId), slot.entityId);
        trimAndCopyString(slot.label, sizeof(slot.label), slot.label);
        trimAndCopyString(slot.unit, sizeof(slot.unit), slot.unit);
    }
}

void clearRuntimeForInactiveSources() {
    if (feedConfig.weather.source != WEATHER_FEED_OPEN_METEO || !feedsWeatherConfigured()) {
        clearWeatherRuntime();
    }

    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (feedConfig.markets[index].source != MARKET_FEED_FINNHUB &&
            feedConfig.markets[index].source != MARKET_FEED_COINGECKO) {
            clearMarketRuntime(index);
            continue;
        }

        if (!feedsMarketConfigured(index)) {
            clearMarketRuntime(index);
        }
    }

    if (!feedsHomeAssistantConfigured()) {
        clearHomeAssistantRuntime();
    }
}

bool weatherIdentityChanged(const FeedConfig &before, const FeedConfig &after) {
    return before.weather.source != after.weather.source ||
           strcmp(before.weather.query, after.weather.query) != 0 ||
           strcmp(before.weather.label, after.weather.label) != 0 ||
           fabsf(before.weather.latitude - after.weather.latitude) > 0.0001f ||
           fabsf(before.weather.longitude - after.weather.longitude) > 0.0001f ||
           before.weather.useFahrenheit != after.weather.useFahrenheit;
}

bool marketIdentityChanged(const MarketFeedConfig &before, const MarketFeedConfig &after) {
    return before.source != after.source ||
           strcmp(before.symbol, after.symbol) != 0 ||
           strcmp(before.label, after.label) != 0 ||
           strcmp(before.currency, after.currency) != 0;
}

bool homeAssistantSlotIdentityChanged(const HomeAssistantSlotConfig &before, const HomeAssistantSlotConfig &after) {
    return before.enabled != after.enabled ||
           strcmp(before.entityId, after.entityId) != 0;
}

bool homeAssistantIdentityChanged(const HomeAssistantConfig &before, const HomeAssistantConfig &after) {
    if (before.enabled != after.enabled ||
        strcmp(before.baseUrl, after.baseUrl) != 0 ||
        strcmp(before.token, after.token) != 0 ||
        before.tlsMode != after.tlsMode ||
        strcmp(before.fingerprint, after.fingerprint) != 0) {
        return true;
    }

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (homeAssistantSlotIdentityChanged(before.slots[index], after.slots[index])) {
            return true;
        }
    }

    return false;
}

bool weatherConfigEquals(const WeatherFeedConfig &left, const WeatherFeedConfig &right) {
    return left.source == right.source &&
           strcmp(left.query, right.query) == 0 &&
           strcmp(left.label, right.label) == 0 &&
           fabsf(left.latitude - right.latitude) <= 0.0001f &&
           fabsf(left.longitude - right.longitude) <= 0.0001f &&
           left.refreshMinutes == right.refreshMinutes &&
           left.useFahrenheit == right.useFahrenheit;
}

bool marketConfigEquals(const MarketFeedConfig &left, const MarketFeedConfig &right) {
    return left.source == right.source &&
           strcmp(left.symbol, right.symbol) == 0 &&
           strcmp(left.label, right.label) == 0 &&
           strcmp(left.currency, right.currency) == 0 &&
           left.refreshMinutes == right.refreshMinutes;
}

bool homeAssistantSlotConfigEquals(const HomeAssistantSlotConfig &left, const HomeAssistantSlotConfig &right) {
    return left.enabled == right.enabled &&
           strcmp(left.entityId, right.entityId) == 0 &&
           strcmp(left.label, right.label) == 0 &&
           strcmp(left.unit, right.unit) == 0;
}

bool homeAssistantConfigEquals(const HomeAssistantConfig &left, const HomeAssistantConfig &right) {
    if (left.enabled != right.enabled ||
        strcmp(left.baseUrl, right.baseUrl) != 0 ||
        strcmp(left.token, right.token) != 0 ||
        left.tlsMode != right.tlsMode ||
        strcmp(left.fingerprint, right.fingerprint) != 0 ||
        left.refreshMinutes != right.refreshMinutes) {
        return false;
    }

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (!homeAssistantSlotConfigEquals(left.slots[index], right.slots[index])) {
            return false;
        }
    }

    return true;
}

bool feedConfigEquals(const FeedConfig &left, const FeedConfig &right) {
    if (left.version != right.version ||
        strcmp(left.finnhubApiKey, right.finnhubApiKey) != 0 ||
        !weatherConfigEquals(left.weather, right.weather) ||
        !homeAssistantConfigEquals(left.homeAssistant, right.homeAssistant)) {
        return false;
    }

    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (!marketConfigEquals(left.markets[index], right.markets[index])) {
            return false;
        }
    }

    return true;
}

void updateDraftState() {
    feedDraftActive = !feedConfigEquals(feedConfig, savedFeedConfig);
}

void captureSavedConfig() {
    memcpy(&savedFeedConfig, &feedConfig, sizeof(savedFeedConfig));
    feedDraftActive = false;
}

void fillWeatherDataJson(JsonObject root, const WeatherData &weather) {
    root["location"] = weather.location;
    root["condition"] = weather.condition;
    root["temperature"] = weather.temperature;
    root["high"] = weather.high;
    root["low"] = weather.low;
    root["rainChance"] = weather.rainChance;
}

void fillMarketDataJson(JsonObject root, const MarketData &market) {
    root["enabled"] = market.enabled;
    root["symbol"] = market.symbol;
    root["label"] = market.label;
    root["price"] = market.price;
    root["change"] = market.change;
    root["changePercent"] = market.changePercent;
}

void fillHomeAssistantSlotDataJson(JsonObject root, const HomeAssistantSlotData &slot) {
    root["enabled"] = slot.enabled;
    root["hasData"] = slot.hasData;
    root["numeric"] = slot.numeric;
    root["stale"] = slot.stale;
    root["lastError"] = slot.lastError;
    root["lastAttemptAgeSec"] = slot.lastAttemptMs ? static_cast<long>((millis() - slot.lastAttemptMs) / 1000UL) : -1;
    root["lastSuccessAgeSec"] = slot.lastSuccessMs ? static_cast<long>((millis() - slot.lastSuccessMs) / 1000UL) : -1;
    root["entityId"] = slot.entityId;
    root["label"] = slot.label;
    root["state"] = slot.state;
    root["unit"] = slot.unit;
}

void fillWeatherStatusJson(JsonObject root, const WeatherFeedRuntime &runtime) {
    root["syncing"] = runtime.syncing;
    root["hasData"] = runtime.hasData;
    root["lastError"] = runtime.lastError;
    root["lastAttemptAgeSec"] = runtime.lastAttemptMs > 0 ? static_cast<long>((millis() - runtime.lastAttemptMs) / 1000UL) : -1;
    root["lastSuccessAgeSec"] = runtime.lastSuccessMs > 0 ? static_cast<long>((millis() - runtime.lastSuccessMs) / 1000UL) : -1;

    JsonObject data = root["data"].to<JsonObject>();
    if (runtime.hasData) {
        fillWeatherDataJson(data, runtime.data);
    }
}

void fillMarketStatusJson(JsonObject root, const MarketFeedRuntime &runtime) {
    root["syncing"] = runtime.syncing;
    root["hasData"] = runtime.hasData;
    root["lastError"] = runtime.lastError;
    root["lastAttemptAgeSec"] = runtime.lastAttemptMs > 0 ? static_cast<long>((millis() - runtime.lastAttemptMs) / 1000UL) : -1;
    root["lastSuccessAgeSec"] = runtime.lastSuccessMs > 0 ? static_cast<long>((millis() - runtime.lastSuccessMs) / 1000UL) : -1;

    JsonObject data = root["data"].to<JsonObject>();
    if (runtime.hasData) {
        fillMarketDataJson(data, runtime.data);
    }
}

void fillHomeAssistantStatusJson(JsonObject root, const HomeAssistantRuntime &runtime) {
    root["syncing"] = runtime.syncing;
    root["hasData"] = runtime.hasData;
    root["lastError"] = runtime.lastError;
    root["lastAttemptAgeSec"] = runtime.lastAttemptMs > 0 ? static_cast<long>((millis() - runtime.lastAttemptMs) / 1000UL) : -1;
    root["lastSuccessAgeSec"] = runtime.lastSuccessMs > 0 ? static_cast<long>((millis() - runtime.lastSuccessMs) / 1000UL) : -1;

    JsonArray slots = root["slots"].to<JsonArray>();
    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        JsonObject slot = slots.add<JsonObject>();
        fillHomeAssistantSlotDataJson(slot, runtime.slots[index]);
    }
}

void fillConfigJson(JsonObject root) {
    root["version"] = feedConfig.version;

    JsonObject providers = root["providers"].to<JsonObject>();
    providers["finnhubApiKey"] = feedConfig.finnhubApiKey;

    JsonObject weather = root["weather"].to<JsonObject>();
    weather["source"] = weatherSourceToString(feedConfig.weather.source);
    weather["query"] = feedConfig.weather.query;
    weather["label"] = feedConfig.weather.label;
    weather["latitude"] = feedConfig.weather.latitude;
    weather["longitude"] = feedConfig.weather.longitude;
    weather["refreshMinutes"] = feedConfig.weather.refreshMinutes;
    weather["useFahrenheit"] = feedConfig.weather.useFahrenheit;

    JsonArray markets = root["markets"].to<JsonArray>();
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        JsonObject market = markets.add<JsonObject>();
        market["source"] = marketSourceToString(feedConfig.markets[index].source);
        market["symbol"] = feedConfig.markets[index].symbol;
        market["label"] = feedConfig.markets[index].label;
        market["currency"] = feedConfig.markets[index].currency;
        market["refreshMinutes"] = feedConfig.markets[index].refreshMinutes;
    }

    JsonObject homeAssistant = root["homeAssistant"].to<JsonObject>();
    homeAssistant["enabled"] = feedConfig.homeAssistant.enabled;
    homeAssistant["baseUrl"] = feedConfig.homeAssistant.baseUrl;
    homeAssistant["token"] = feedConfig.homeAssistant.token;
    homeAssistant["tlsMode"] = homeAssistantTlsModeToString(feedConfig.homeAssistant.tlsMode);
    homeAssistant["fingerprint"] = feedConfig.homeAssistant.fingerprint;
    homeAssistant["refreshMinutes"] = feedConfig.homeAssistant.refreshMinutes;

    JsonArray homeSlots = homeAssistant["slots"].to<JsonArray>();
    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        JsonObject slot = homeSlots.add<JsonObject>();
        slot["enabled"] = feedConfig.homeAssistant.slots[index].enabled;
        slot["entityId"] = feedConfig.homeAssistant.slots[index].entityId;
        slot["label"] = feedConfig.homeAssistant.slots[index].label;
        slot["unit"] = feedConfig.homeAssistant.slots[index].unit;
    }
}

void fillStatusJson(JsonObject root) {
    JsonObject weather = root["weather"].to<JsonObject>();
    fillWeatherStatusJson(weather, feedRuntime.weather);

    JsonArray markets = root["markets"].to<JsonArray>();
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        JsonObject market = markets.add<JsonObject>();
        fillMarketStatusJson(market, feedRuntime.markets[index]);
    }

    JsonObject homeAssistant = root["homeAssistant"].to<JsonObject>();
    fillHomeAssistantStatusJson(homeAssistant, feedRuntime.homeAssistant);
}

bool writeJsonToFile(const char *path, JsonDocument &doc) {
    char tempPath[64];
    if (!buildTempPath(path, tempPath, sizeof(tempPath))) {
        return false;
    }

    LittleFS.remove(tempPath);

    File file = LittleFS.open(tempPath, "w");
    if (!file) {
        return false;
    }

    size_t expectedBytes = measureJson(doc);
    bool success = !doc.overflowed() && serializeJson(doc, file) == expectedBytes;
    file.flush();
    file.close();
    if (!success) {
        LittleFS.remove(tempPath);
        return false;
    }

    if (!LittleFS.rename(tempPath, path)) {
        return false;
    }

    return true;
}

template <typename TObject>
bool loadJsonFile(const char *path, TObject object) {
    auto tryLoad = [&](const char *candidatePath) {
        File file = LittleFS.open(candidatePath, "r");
        if (!file) {
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error) {
            return false;
        }

        object(doc.as<JsonObjectConst>());
        return true;
    };

    if (tryLoad(path)) {
        return true;
    }

    char tempPath[64];
    if (!buildTempPath(path, tempPath, sizeof(tempPath))) {
        return false;
    }

    if (!LittleFS.exists(tempPath) || !tryLoad(tempPath)) {
        return false;
    }

    LittleFS.remove(path);
    LittleFS.rename(tempPath, path);
    return true;
}

const char* weatherCodeToText(int code) {
    switch (code) {
        case 0:
            return "Clear";
        case 1:
            return "Mostly clear";
        case 2:
            return "Partly cloudy";
        case 3:
            return "Overcast";
        case 45:
        case 48:
            return "Fog";
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
            return "Drizzle";
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
            return "Rain";
        case 71:
        case 73:
        case 75:
        case 77:
            return "Snow";
        case 80:
        case 81:
        case 82:
            return "Showers";
        case 85:
        case 86:
            return "Snow showers";
        case 95:
            return "Thunderstorm";
        case 96:
        case 99:
            return "Storm";
        default:
            return "Weather";
    }
}

// Bound parser work as well as network waits; filtered JSON still consumes the
// entire response. Yield every 256 bytes and reject unexpectedly large bodies.
class FeedJsonStream : public Stream {
public:
    explicit FeedJsonStream(Stream &source) : source_(source) { setTimeout(kHttpTimeoutMs); }
    int available() override { return exhausted() ? 0 : source_.available(); }
    int peek() override { return exhausted() ? -1 : source_.peek(); }
    int read() override {
        if (exhausted()) return -1;
        int value = source_.read();
        if (value >= 0 && (++consumed_ % 256) == 0) yield();
        return value;
    }
    size_t readBytes(char *buffer, size_t length) override {
        size_t received = 0;
        uint32_t lastProgress = millis();
        while (received < length && !exhausted()) {
            int value = read();
            if (value >= 0) {
                buffer[received++] = static_cast<char>(value);
                lastProgress = millis();
            } else {
                if (millis() - lastProgress >= kHttpTimeoutMs) break;
                yield();
            }
        }
        return received;
    }
    size_t write(uint8_t) override { return 0; }
    bool exhausted() const { return consumed_ >= 131072UL || activeRequestExpired; }
private:
    Stream &source_;
    uint32_t consumed_ = 0;
};

bool httpGetJson(const String &url,
                 JsonDocument &doc,
                 String *error,
                 JsonDocument *filter = nullptr,
                 const HttpJsonRequestOptions *options = nullptr) {
    if (feedJobsCancelled) {
        if (error) *error = F("Feed job cancelled");
        return false;
    }
    struct RequestGuard {
        RequestGuard() { activeRequestStartedMs = millis(); activeRequestExpired = false; }
        ~RequestGuard() { activeHttpClient = nullptr; }
    } requestGuard;
    struct ActiveClientGuard {
        explicit ActiveClientGuard(WiFiClient *client) { activeHttpClient = client; }
        ~ActiveClientGuard() { activeHttpClient = nullptr; activeTlsClient = nullptr; }
    };
    auto finishJsonRequest = [&](HTTPClient &http, const char *transportLabel) {
        http.addHeader("Accept", "application/json");
        if (options != nullptr &&
            options->authorizationBearer != nullptr &&
            options->authorizationBearer[0] != '\0') {
            http.addHeader("Authorization", String("Bearer ") + options->authorizationBearer);
        }
        int responseCode = http.GET();
        if (responseCode != HTTP_CODE_OK) {
            String httpError = responseCode < 0 ? HTTPClient::errorToString(responseCode) : "";
            if (error != nullptr) {
                *error = responseCode < 0
                             ? ("HTTP " + String(responseCode) + " " + httpError)
                             : ("HTTP " + String(responseCode));
            }
            if (responseCode < 0 && httpError.length() > 0) {
                logPrintf(F("%s GET failed: %d %s (%s)"),
                          transportLabel,
                          responseCode,
                          httpError.c_str(),
                          "feed request");
            } else {
                logPrintf(F("%s GET failed: %d (%s)"), transportLabel, responseCode, "feed request");
            }
            http.end();
            return false;
        }

        if (http.getSize() > 131072) {
            if (error) *error = F("Response too large");
            http.end();
            return false;
        }
        doc.clear();
        FeedJsonStream body(http.getStream());
        DeserializationError jsonError =
            filter == nullptr
                ? deserializeJson(doc, body)
                : deserializeJson(doc,
                                  body,
                                  DeserializationOption::Filter(filter->as<JsonVariantConst>()));
        http.end();

        if (jsonError || body.exhausted()) {
            if (error != nullptr) {
                *error = body.exhausted() ? (activeRequestExpired ? "Request deadline exceeded" : "Response too large") : jsonError == DeserializationError::NoMemory
                             ? "JSON memory exhausted"
                             : String(jsonError.c_str());
            }
            logPrintf(F("%s JSON parse failed: %s (%s)"), transportLabel, jsonError.c_str(), "feed request");
            return false;
        }

        if (activeRequestExpired) {
            if (error) *error = F("Request deadline exceeded");
            return false;
        }
        return true;
    };

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    http.useHTTP10(true);
    http.setUserAgent(F("SmartClock/1.0"));

    if (url.startsWith("http://")) {
        WiFiClient client;
        ActiveClientGuard activeGuard(&client);
        if (!http.begin(client, url)) {
            if (error != nullptr) {
                *error = F("HTTP begin failed");
            }
            return false;
        }

        return finishJsonRequest(http, "HTTP");
    }

    if (!url.startsWith("https://")) {
        if (error != nullptr) {
            *error = F("Unsupported URL scheme");
        }
        return false;
    }

    struct DisplayHeapGuard {
        DisplayHeapGuard() {
            displaySuspendDynamicResources();
        }

        ~DisplayHeapGuard() {
            displayResumeDynamicResources();
        }
    } displayHeapGuard;

    if (ESP.getFreeHeap() < kHttpsMinFreeHeapBytes) {
        if (error) *error = F("Low heap, retrying later");
        return false;
    }
    std::unique_ptr<FeedTlsClient> client(new (std::nothrow) FeedTlsClient());
    if (!client) {
        if (error) *error = F("TLS client allocation failed");
        return false;
    }
    if (options != nullptr &&
        options->tlsMode == HOME_ASSISTANT_TLS_FINGERPRINT) {
        if (options->fingerprint == nullptr || options->fingerprint[0] == '\0') {
            if (error != nullptr) {
                *error = F("Fingerprint missing");
            }
            return false;
        }

        if (!client->setFingerprint(options->fingerprint)) {
            if (error != nullptr) {
                *error = F("Fingerprint invalid");
            }
            return false;
        }
    } else {
        client->setInsecure();
    }
    client->setBufferSizes(kHttpsDefaultRecvBufferBytes, kHttpsCompactXmitBufferBytes);
    ActiveClientGuard activeGuard(client.get());
    activeTlsClient = client.get();
    if (!http.begin(*client, url)) {
        if (error != nullptr) {
            *error = F("HTTP begin failed");
        }
        return false;
    }

    return finishJsonRequest(http, "HTTPS");
}

bool fetchWeatherSearchJson(const String &query, JsonDocument &providerDoc, String *error) {
    String trimmedQuery = query;
    trimmedQuery.trim();

    if (trimmedQuery.length() < 2) {
        if (error != nullptr) {
            *error = F("Enter at least 2 characters");
        }
        return false;
    }

    String url = "http://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(trimmedQuery) +
                 "&count=6&language=en&format=json";
    JsonDocument filter;
    buildWeatherSearchFilter(filter);
    return httpGetJson(url, providerDoc, error, &filter);
}

bool resolveWeatherLocationFromQuery(String *error) {
    JsonDocument providerDoc;
    String requestError;
    if (!fetchWeatherSearchJson(feedConfig.weather.query, providerDoc, &requestError)) {
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    JsonArrayConst providerResults = providerDoc["results"].as<JsonArrayConst>();
    if (providerResults.isNull() || providerResults.size() == 0) {
        if (error != nullptr) {
            *error = F("City not found");
        }
        return false;
    }

    JsonObjectConst item = providerResults[0].as<JsonObjectConst>();
    if (item.isNull() || item["latitude"].isNull() || item["longitude"].isNull()) {
        if (error != nullptr) {
            *error = F("Location payload incomplete");
        }
        return false;
    }

    feedConfig.weather.latitude = item["latitude"].as<float>();
    feedConfig.weather.longitude = item["longitude"].as<float>();

    String resolvedLabel = buildWeatherLocationLabel(item);
    if (!resolvedLabel.isEmpty()) {
        copyString(feedConfig.weather.label, sizeof(feedConfig.weather.label), resolvedLabel.c_str());
    }

    updateDraftState();
    return true;
}

bool fetchCoinGeckoMarketJson(const String &marketId, const String &currency, JsonDocument &doc, String *error) {
    String url = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=" + urlEncode(currency) +
                 "&ids=" + urlEncode(marketId) +
                 "&sparkline=false&price_change_percentage=24h";
    JsonDocument filter;
    buildCoinGeckoMarketFilter(filter);
    return httpGetJson(url, doc, error, &filter);
}

int coinGeckoCandidateScore(const String &input, JsonObjectConst coin) {
    String candidateId = coin["id"].isNull() ? String("") : normalizeCoinGeckoLookupValue(coin["id"].as<const char*>());
    String candidateName = coin["name"].isNull() ? String("") : trimmedLowercaseString(coin["name"].as<const char*>());
    String candidateSymbol = coin["symbol"].isNull() ? String("") : trimmedLowercaseString(coin["symbol"].as<const char*>());

    if (candidateId == input) {
        return 600;
    }
    if (candidateSymbol == input) {
        return 560;
    }
    if (candidateName == input) {
        return 520;
    }
    if (candidateId.startsWith(input)) {
        return 420;
    }
    if (candidateName.startsWith(input)) {
        return 360;
    }
    if (candidateSymbol.startsWith(input)) {
        return 320;
    }
    if (candidateId.indexOf(input) >= 0) {
        return 240;
    }
    if (candidateName.indexOf(input) >= 0) {
        return 180;
    }
    if (candidateSymbol.indexOf(input) >= 0) {
        return 140;
    }
    return 0;
}

bool resolveCoinGeckoAsset(const String &query,
                           String &resolvedId,
                           String &resolvedName,
                           String *error) {
    String normalizedQuery = normalizeCoinGeckoLookupValue(query);
    if (normalizedQuery.length() == 0) {
        if (error != nullptr) {
            *error = F("CoinGecko symbol missing");
        }
        return false;
    }

    JsonDocument doc;
    String requestError;
    String url = "https://api.coingecko.com/api/v3/search?query=" + urlEncode(normalizedQuery);
    JsonDocument filter;
    buildCoinGeckoSearchFilter(filter);
    if (!httpGetJson(url, doc, &requestError, &filter)) {
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    JsonArrayConst coins = doc["coins"].as<JsonArrayConst>();
    if (coins.isNull() || coins.size() == 0) {
        if (error != nullptr) {
            *error = F("CoinGecko returned no match");
        }
        return false;
    }

    JsonObjectConst bestCoin;
    int bestScore = -1;
    int bestRank = INT_MAX;

    for (JsonObjectConst coin : coins) {
        int score = coinGeckoCandidateScore(normalizedQuery, coin);
        int rank = coin["market_cap_rank"].isNull() ? INT_MAX : coin["market_cap_rank"].as<int>();
        if (score > bestScore || (score == bestScore && rank < bestRank)) {
            bestCoin = coin;
            bestScore = score;
            bestRank = rank;
        }
    }

    if (bestCoin.isNull()) {
        bestCoin = coins[0].as<JsonObjectConst>();
    }

    resolvedId = bestCoin["id"].isNull() ? String("") : normalizeCoinGeckoLookupValue(bestCoin["id"].as<const char*>());
    resolvedName = bestCoin["name"].isNull() ? String("") : String(bestCoin["name"].as<const char*>());

    if (resolvedId.length() == 0) {
        if (error != nullptr) {
            *error = F("CoinGecko returned no match");
        }
        return false;
    }

    return true;
}

bool syncWeather(String *error) {
    if (WiFi.status() != WL_CONNECTED) {
        if (error != nullptr) {
            *error = F("WiFi disconnected");
        }
        return false;
    }

    if (feedConfig.weather.source != WEATHER_FEED_OPEN_METEO || !feedsWeatherConfigured()) {
        if (error != nullptr) {
            *error = F("Weather feed not configured");
        }
        return false;
    }

    WeatherFeedRuntime &runtime = feedRuntime.weather;
    runtime.syncing = true;
    runtime.lastAttemptMs = millis();
    runtime.lastError[0] = '\0';

    if (!weatherCoordinatesValid(feedConfig.weather.latitude, feedConfig.weather.longitude)) {
        String locationError;
        if (!resolveWeatherLocationFromQuery(&locationError)) {
            runtime.syncing = false;
            copyString(runtime.lastError, sizeof(runtime.lastError), locationError.c_str());
            if (error != nullptr) {
                *error = locationError;
            }
            return false;
        }
    }

    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(feedConfig.weather.latitude, 4) +
                 "&longitude=" + String(feedConfig.weather.longitude, 4) +
                 "&current=temperature_2m,weather_code" +
                 "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
                 "&forecast_days=1&timezone=auto";
    if (feedConfig.weather.useFahrenheit) {
        url += "&temperature_unit=fahrenheit";
    } else {
        url += "&temperature_unit=celsius";
    }

    JsonDocument doc;
    String requestError;
    JsonDocument filter;
    buildWeatherForecastFilter(filter);
    if (!httpGetJson(url, doc, &requestError, &filter)) {
        runtime.syncing = false;
        copyString(runtime.lastError, sizeof(runtime.lastError), requestError.c_str());
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    JsonObjectConst current = doc["current"].as<JsonObjectConst>();
    JsonObjectConst daily = doc["daily"].as<JsonObjectConst>();
    JsonArrayConst highs = daily["temperature_2m_max"].as<JsonArrayConst>();
    JsonArrayConst lows = daily["temperature_2m_min"].as<JsonArrayConst>();
    JsonArrayConst rain = daily["precipitation_probability_max"].as<JsonArrayConst>();

    if (current.isNull() || current["temperature_2m"].isNull()) {
        runtime.syncing = false;
        copyString(runtime.lastError, sizeof(runtime.lastError), "Weather payload incomplete");
        if (error != nullptr) {
            *error = runtime.lastError;
        }
        return false;
    }

    memset(&runtime.data, 0, sizeof(runtime.data));
    const char *locationLabel = feedConfig.weather.label[0] != '\0' ? feedConfig.weather.label : feedConfig.weather.query;
    copyString(runtime.data.location, sizeof(runtime.data.location), locationLabel);
    copyString(runtime.data.condition, sizeof(runtime.data.condition), weatherCodeToText(current["weather_code"] | 0));
    runtime.data.temperature = static_cast<int>(roundf(current["temperature_2m"].as<float>()));
    runtime.data.high = highs.isNull() || highs.size() == 0
                            ? runtime.data.temperature
                            : static_cast<int>(roundf(highs[0].as<float>()));
    runtime.data.low = lows.isNull() || lows.size() == 0
                           ? runtime.data.temperature
                           : static_cast<int>(roundf(lows[0].as<float>()));
    runtime.data.rainChance = rain.isNull() || rain.size() == 0
                                  ? 0
                                  : constrain(static_cast<int>(roundf(rain[0].as<float>())), 0, 100);

    runtime.hasData = true;
    runtime.syncing = false;
    runtime.lastSuccessMs = millis();
    runtime.lastError[0] = '\0';
    logPrintf(F("Weather sync OK: %s %d"), runtime.data.location, runtime.data.temperature);
    return true;
}

String uppercaseString(const String &value) {
    String upper = value;
    upper.toUpperCase();
    return upper;
}

String trimTrailingZerosString(const String &value) {
    String result = value;
    while (result.indexOf('.') >= 0 && result.endsWith("0")) {
        result.remove(result.length() - 1);
    }
    if (result.endsWith(".")) {
        result.remove(result.length() - 1);
    }
    return result;
}

String groupThousandsString(const String &value) {
    int decimalIndex = value.indexOf('.');
    if (decimalIndex < 0) {
        decimalIndex = value.length();
    }

    int startIndex = value.startsWith("-") ? 1 : 0;
    if (decimalIndex - startIndex <= 3) {
        return value;
    }

    String grouped;
    grouped.reserve(value.length() + ((decimalIndex - startIndex - 1) / 3));
    if (startIndex == 1) {
        grouped += '-';
    }

    for (int index = startIndex; index < static_cast<int>(value.length()); ++index) {
        grouped += value.charAt(index);

        if (index < decimalIndex - 1) {
            int remainingDigits = decimalIndex - index - 1;
            if (remainingDigits > 0 && (remainingDigits % 3) == 0) {
                grouped += ',';
            }
        }
    }

    return grouped;
}

String formatHomeAssistantNumericState(float value) {
    float absoluteValue = value >= 0.0f ? value : -value;
    uint8_t precision = 2;

    if (absoluteValue >= 1000.0f) {
        precision = 0;
    } else if (absoluteValue >= 100.0f) {
        precision = 1;
    } else if (absoluteValue >= 10.0f) {
        precision = 1;
    } else if (absoluteValue >= 1.0f) {
        precision = 2;
    } else if (absoluteValue >= 0.1f) {
        precision = 3;
    } else {
        precision = 4;
    }

    return groupThousandsString(trimTrailingZerosString(String(value, precision)));
}

bool homeAssistantSlotConfigured(const HomeAssistantSlotConfig &slot) {
    return slot.enabled && slot.entityId[0] != '\0';
}

String buildHomeAssistantSlotLabel(const HomeAssistantSlotConfig &slot, JsonObjectConst attributes) {
    if (slot.label[0] != '\0') {
        return String(slot.label);
    }

    if (!attributes.isNull() && !attributes["friendly_name"].isNull()) {
        String friendlyName = trimStringCopy(attributes["friendly_name"].as<const char*>());
        if (friendlyName.length() > 0) {
            return friendlyName;
        }
    }

    return humanizeIdentifier(String(slot.entityId));
}

String buildHomeAssistantSlotUnit(const HomeAssistantSlotConfig &slot, JsonObjectConst attributes) {
    if (slot.unit[0] != '\0') {
        return String(slot.unit);
    }

    if (!attributes.isNull() && !attributes["unit_of_measurement"].isNull()) {
        return trimStringCopy(attributes["unit_of_measurement"].as<const char*>());
    }

    return "";
}

String normalizeHomeAssistantStateForDisplay(const String &rawState, bool &numeric) {
    numeric = false;

    String trimmedState = trimStringCopy(rawState.c_str());
    if (trimmedState.length() == 0) {
        return "No data";
    }

    String lowered = trimmedLowercaseString(trimmedState);
    if (lowered == "unknown" || lowered == "unavailable" || lowered == "none") {
        return humanizeStateText(trimmedState);
    }

    float numericValue = 0.0f;
    if (parseFloatStrict(trimmedState, numericValue)) {
        numeric = true;
        if (trimmedState.indexOf('.') < 0 && trimmedState.indexOf('e') < 0 && trimmedState.indexOf('E') < 0) return groupThousandsString(trimmedState);
        return formatHomeAssistantNumericState(numericValue);
    }

    return humanizeStateText(trimmedState);
}

bool syncHomeAssistantSlot(uint8_t index, String *error) {
    if (index >= HOME_ASSISTANT_SLOT_COUNT) {
        if (error != nullptr) {
            *error = F("Home slot out of range");
        }
        return false;
    }

    const HomeAssistantSlotConfig &slot = feedConfig.homeAssistant.slots[index];
    if (!homeAssistantSlotConfigured(slot)) {
        if (error != nullptr) {
            *error = F("Home slot not configured");
        }
        return false;
    }

    String url = String(feedConfig.homeAssistant.baseUrl) + "/api/states/" + urlEncode(slot.entityId);
    JsonDocument doc;
    JsonDocument filter;
    buildHomeAssistantStateFilter(filter);
    HttpJsonRequestOptions requestOptions = {
        feedConfig.homeAssistant.token,
        feedConfig.homeAssistant.tlsMode,
        feedConfig.homeAssistant.fingerprint
    };

    String requestError;
    if (!httpGetJson(url, doc, &requestError, &filter, &requestOptions)) {
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    JsonObjectConst attributes = doc["attributes"].as<JsonObjectConst>();
    String entityId = trimStringCopy(doc["entity_id"] | slot.entityId);
    String rawState = trimStringCopy(doc["state"] | "");
    if (entityId.length() == 0 || !doc["state"].is<const char*>()) {
        if (error != nullptr) {
            *error = F("Home Assistant payload incomplete");
        }
        return false;
    }

    HomeAssistantSlotData &runtimeSlot = feedRuntime.homeAssistant.slots[index];
    bool numeric = false;
    String displayState = normalizeHomeAssistantStateForDisplay(rawState, numeric);
    String label = buildHomeAssistantSlotLabel(slot, attributes);
    String unit = buildHomeAssistantSlotUnit(slot, attributes);

    memset(&runtimeSlot, 0, sizeof(runtimeSlot));
    runtimeSlot.enabled = true;
    runtimeSlot.hasData = true;
    runtimeSlot.numeric = numeric;
    runtimeSlot.lastAttemptMs = millis();
    runtimeSlot.lastSuccessMs = millis();
    copyString(runtimeSlot.entityId, sizeof(runtimeSlot.entityId), entityId.c_str());
    copyString(runtimeSlot.label, sizeof(runtimeSlot.label), label.c_str());
    copyString(runtimeSlot.state, sizeof(runtimeSlot.state), displayState.c_str());
    copyString(runtimeSlot.unit, sizeof(runtimeSlot.unit), unit.c_str());
    return true;
}

bool syncHomeAssistant(String *error) {
    if (WiFi.status() != WL_CONNECTED) {
        if (error != nullptr) {
            *error = F("WiFi disconnected");
        }
        return false;
    }

    if (!feedsHomeAssistantConfigured()) {
        if (error != nullptr) {
            *error = F("Home Assistant not configured");
        }
        return false;
    }

    HomeAssistantRuntime &runtime = feedRuntime.homeAssistant;
    runtime.syncing = true;
    runtime.lastAttemptMs = millis();
    runtime.lastError[0] = '\0';

    bool anyConfigured = false;
    bool anySuccess = false;
    String firstError;

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        const HomeAssistantSlotConfig &slot = feedConfig.homeAssistant.slots[index];
        if (!homeAssistantSlotConfigured(slot)) {
            memset(&runtime.slots[index], 0, sizeof(runtime.slots[index]));
            continue;
        }

        anyConfigured = true;
        String slotError;
        runtime.slots[index].enabled = true;
        runtime.slots[index].lastAttemptMs = millis();
        bool success = syncHomeAssistantSlot(index, &slotError);
        if (!success) {
            runtime.slots[index].stale = true;
            copyString(runtime.slots[index].lastError, sizeof(runtime.slots[index].lastError), slotError.c_str());
        }
        anySuccess = anySuccess || success;
        if (!success && firstError.length() == 0) {
            firstError = slotError;
        }
        yield();
    }

    runtime.syncing = false;
    runtime.hasData = false;
    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        runtime.hasData = runtime.hasData || runtime.slots[index].hasData;
    }

    if (!anyConfigured) {
        if (error != nullptr) {
            *error = F("No Home Assistant slots configured");
        }
        copyString(runtime.lastError, sizeof(runtime.lastError), "No slots configured");
        return false;
    }

    if (!anySuccess) {
        copyString(runtime.lastError,
                   sizeof(runtime.lastError),
                   firstError.length() > 0 ? firstError.c_str() : "Home Assistant sync failed");
        if (error != nullptr) {
            *error = runtime.lastError;
        }
        return false;
    }

    if (firstError.length() == 0) runtime.lastSuccessMs = millis();
    copyString(runtime.lastError, sizeof(runtime.lastError), firstError.c_str());
    logPrint(F("Home Assistant sync completed"));
    return true;
}

bool syncFinnhubMarket(uint8_t index, String *error) {
    if (feedConfig.finnhubApiKey[0] == '\0') {
        if (error != nullptr) {
            *error = F("Finnhub API key missing");
        }
        return false;
    }

    String url = "https://finnhub.io/api/v1/quote?symbol=" + urlEncode(feedConfig.markets[index].symbol) +
                 "&token=" + urlEncode(feedConfig.finnhubApiKey);

    JsonDocument doc;
    String requestError;
    JsonDocument filter;
    buildFinnhubQuoteFilter(filter);
    if (!httpGetJson(url, doc, &requestError, &filter)) {
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    if (doc["c"].isNull()) {
        if (error != nullptr) {
            *error = F("Finnhub payload incomplete");
        }
        return false;
    }

    MarketFeedRuntime &runtime = feedRuntime.markets[index];
    memset(&runtime.data, 0, sizeof(runtime.data));
    runtime.data.enabled = true;
    copyString(runtime.data.symbol, sizeof(runtime.data.symbol), uppercaseString(feedConfig.markets[index].symbol).c_str());
    copyString(runtime.data.label, sizeof(runtime.data.label), feedConfig.markets[index].label);
    runtime.data.price = doc["c"].as<float>();
    runtime.data.change = doc["d"] | 0.0f;
    runtime.data.changePercent = doc["dp"] | 0.0f;
    return true;
}

bool syncCoinGeckoMarket(uint8_t index, String *error) {
    String currency = feedConfig.markets[index].currency;
    if (currency.length() == 0) {
        currency = "usd";
    }

    JsonDocument doc;
    String requestError;
    String marketId = normalizeCoinGeckoLookupValue(feedConfig.markets[index].symbol);
    if (marketId.length() == 0) {
        if (error != nullptr) {
            *error = F("CoinGecko symbol missing");
        }
        return false;
    }

    if (!fetchCoinGeckoMarketJson(marketId, currency, doc, &requestError)) {
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    JsonArrayConst markets = doc.as<JsonArrayConst>();
    if (markets.isNull() || markets.size() == 0) {
        String resolvedId;
        String resolvedName;
        if (!resolveCoinGeckoAsset(feedConfig.markets[index].symbol, resolvedId, resolvedName, &requestError)) {
            if (error != nullptr) {
                *error = requestError;
            }
            return false;
        }

        doc.clear();
        if (!fetchCoinGeckoMarketJson(resolvedId, currency, doc, &requestError)) {
            if (error != nullptr) {
                *error = requestError;
            }
            return false;
        }

        markets = doc.as<JsonArrayConst>();
        if (markets.isNull() || markets.size() == 0) {
            if (error != nullptr) {
                *error = F("CoinGecko returned no match");
            }
            return false;
        }

        copyString(feedConfig.markets[index].symbol, sizeof(feedConfig.markets[index].symbol), resolvedId.c_str());
        if (feedConfig.markets[index].label[0] == '\0' && resolvedName.length() > 0) {
            copyString(feedConfig.markets[index].label, sizeof(feedConfig.markets[index].label), resolvedName.c_str());
        }
        updateDraftState();
    }

    JsonObjectConst source = markets[0].as<JsonObjectConst>();
    if (source.isNull() || source["current_price"].isNull()) {
        if (error != nullptr) {
            *error = F("CoinGecko payload incomplete");
        }
        return false;
    }

    String providerSymbol = source["symbol"].isNull() ? String(feedConfig.markets[index].symbol) : String(source["symbol"].as<const char*>());
    String providerName = source["name"].isNull() ? String("") : String(source["name"].as<const char*>());

    MarketFeedRuntime &runtime = feedRuntime.markets[index];
    memset(&runtime.data, 0, sizeof(runtime.data));
    runtime.data.enabled = true;
    copyString(runtime.data.symbol,
               sizeof(runtime.data.symbol),
               uppercaseString(providerSymbol).c_str());
    copyString(runtime.data.label,
               sizeof(runtime.data.label),
               feedConfig.markets[index].label[0] != '\0' ? feedConfig.markets[index].label : providerName.c_str());
    runtime.data.price = source["current_price"].as<float>();
    runtime.data.change = source["price_change_24h"] | 0.0f;
    runtime.data.changePercent = source["price_change_percentage_24h"] | 0.0f;
    return true;
}

bool syncMarket(uint8_t index, String *error) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        if (error != nullptr) {
            *error = F("Market slot out of range");
        }
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (error != nullptr) {
            *error = F("WiFi disconnected");
        }
        return false;
    }

    if (!feedsMarketConfigured(index)) {
        if (error != nullptr) {
            *error = F("Market feed not configured");
        }
        return false;
    }

    MarketFeedRuntime &runtime = feedRuntime.markets[index];
    runtime.syncing = true;
    runtime.lastAttemptMs = millis();
    runtime.lastError[0] = '\0';

    bool success = false;
    String requestError;
    if (feedConfig.markets[index].source == MARKET_FEED_FINNHUB) {
        success = syncFinnhubMarket(index, &requestError);
    } else if (feedConfig.markets[index].source == MARKET_FEED_COINGECKO) {
        success = syncCoinGeckoMarket(index, &requestError);
    } else {
        requestError = "Market feed not configured";
    }

    runtime.syncing = false;
    if (!success) {
        copyString(runtime.lastError, sizeof(runtime.lastError), requestError.c_str());
        if (error != nullptr) {
            *error = requestError;
        }
        return false;
    }

    runtime.hasData = true;
    runtime.lastSuccessMs = millis();
    runtime.lastError[0] = '\0';
    logPrintf(F("Market sync OK: slot=%u symbol=%s price=%.2f"),
              index,
              runtime.data.symbol,
              runtime.data.price);
    return true;
}

void applyProvidersObject(JsonObjectConst providers) {
    if (providers.isNull()) {
        return;
    }

    if (!providers["finnhubApiKey"].isNull()) {
        copyString(feedConfig.finnhubApiKey, sizeof(feedConfig.finnhubApiKey), providers["finnhubApiKey"]);
    }
}

void applyWeatherObject(JsonObjectConst weather) {
    if (weather.isNull()) {
        return;
    }

    if (!weather["source"].isNull()) {
        feedConfig.weather.source = weatherSourceFromVariant(weather["source"]);
    }
    if (!weather["query"].isNull()) {
        copyString(feedConfig.weather.query, sizeof(feedConfig.weather.query), weather["query"]);
    }
    if (!weather["label"].isNull()) {
        copyString(feedConfig.weather.label, sizeof(feedConfig.weather.label), weather["label"]);
    }
    if (!weather["latitude"].isNull()) {
        feedConfig.weather.latitude = weather["latitude"].as<float>();
    }
    if (!weather["longitude"].isNull()) {
        feedConfig.weather.longitude = weather["longitude"].as<float>();
    }
    if (!weather["refreshMinutes"].isNull()) {
        feedConfig.weather.refreshMinutes = weather["refreshMinutes"].as<uint16_t>();
    }
    if (!weather["useFahrenheit"].isNull()) {
        feedConfig.weather.useFahrenheit = weather["useFahrenheit"].as<bool>();
    }
}

void applyMarketsArray(JsonArrayConst markets) {
    if (markets.isNull()) {
        return;
    }

    uint8_t index = 0;
    for (JsonObjectConst market : markets) {
        if (index >= DASHBOARD_MARKET_COUNT) {
            break;
        }

        if (!market["source"].isNull()) {
            feedConfig.markets[index].source = marketSourceFromVariant(market["source"]);
        }
        if (!market["symbol"].isNull()) {
            copyString(feedConfig.markets[index].symbol, sizeof(feedConfig.markets[index].symbol), market["symbol"]);
        }
        if (!market["label"].isNull()) {
            copyString(feedConfig.markets[index].label, sizeof(feedConfig.markets[index].label), market["label"]);
        }
        if (!market["currency"].isNull()) {
            copyString(feedConfig.markets[index].currency, sizeof(feedConfig.markets[index].currency), market["currency"]);
        }
        if (!market["refreshMinutes"].isNull()) {
            feedConfig.markets[index].refreshMinutes = market["refreshMinutes"].as<uint16_t>();
        }

        ++index;
    }
}

void applyHomeAssistantObject(JsonObjectConst homeAssistant) {
    if (homeAssistant.isNull()) {
        return;
    }

    if (!homeAssistant["enabled"].isNull()) {
        feedConfig.homeAssistant.enabled = homeAssistant["enabled"].as<bool>();
    }
    if (!homeAssistant["baseUrl"].isNull()) {
        copyString(feedConfig.homeAssistant.baseUrl,
                   sizeof(feedConfig.homeAssistant.baseUrl),
                   homeAssistant["baseUrl"]);
    }
    if (!homeAssistant["token"].isNull()) {
        copyString(feedConfig.homeAssistant.token,
                   sizeof(feedConfig.homeAssistant.token),
                   homeAssistant["token"]);
    }
    if (!homeAssistant["tlsMode"].isNull()) {
        feedConfig.homeAssistant.tlsMode = homeAssistantTlsModeFromVariant(homeAssistant["tlsMode"]);
    }
    if (!homeAssistant["fingerprint"].isNull()) {
        copyString(feedConfig.homeAssistant.fingerprint,
                   sizeof(feedConfig.homeAssistant.fingerprint),
                   homeAssistant["fingerprint"]);
    }
    if (!homeAssistant["refreshMinutes"].isNull()) {
        feedConfig.homeAssistant.refreshMinutes = homeAssistant["refreshMinutes"].as<uint16_t>();
    }

    JsonArrayConst slots = homeAssistant["slots"].as<JsonArrayConst>();
    if (slots.isNull()) {
        return;
    }

    uint8_t index = 0;
    for (JsonObjectConst slot : slots) {
        if (index >= HOME_ASSISTANT_SLOT_COUNT) {
            break;
        }

        if (!slot["enabled"].isNull()) {
            feedConfig.homeAssistant.slots[index].enabled = slot["enabled"].as<bool>();
        }
        if (!slot["entityId"].isNull()) {
            copyString(feedConfig.homeAssistant.slots[index].entityId,
                       sizeof(feedConfig.homeAssistant.slots[index].entityId),
                       slot["entityId"]);
        }
        if (!slot["label"].isNull()) {
            copyString(feedConfig.homeAssistant.slots[index].label,
                       sizeof(feedConfig.homeAssistant.slots[index].label),
                       slot["label"]);
        }
        if (!slot["unit"].isNull()) {
            copyString(feedConfig.homeAssistant.slots[index].unit,
                       sizeof(feedConfig.homeAssistant.slots[index].unit),
                       slot["unit"]);
        }

        ++index;
    }
}

void applyConfigObject(JsonObjectConst root) {
    applyProvidersObject(root["providers"].as<JsonObjectConst>());
    applyWeatherObject(root["weather"].as<JsonObjectConst>());
    applyMarketsArray(root["markets"].as<JsonArrayConst>());
    applyHomeAssistantObject(root["homeAssistant"].as<JsonObjectConst>());
    normalizeConfig();
}

constexpr uint16_t kWeatherJob = 1;
constexpr uint16_t kHomeAssistantJob = 2;
constexpr uint16_t kMarketJobBase = 4;

void runFeedJob() {
    if (searchStatus == FEED_SEARCH_PENDING && activeSyncMask == 0) {
        bool success = feedsSearchWeatherLocations(searchQuery, searchJson, &searchError);
        searchStatus = success ? FEED_SEARCH_READY : FEED_SEARCH_FAILED;
    } else {
        bool anySuccess = false;
        String firstError;
        auto record = [&](bool success, const String &error) {
            anySuccess = anySuccess || success;
            if (!success && firstError.isEmpty()) firstError = error;
        };
        String error;
        if (activeSyncMask & kWeatherJob) {
            record(syncWeather(&error), error);
            yield();
        }
        for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
            if (activeSyncMask & (kMarketJobBase << index)) {
                error = "";
                record(syncMarket(index, &error), error);
                yield();
            }
        }
        if (activeSyncMask & kHomeAssistantJob) {
            error = "";
            bool success = syncHomeAssistant(&error);
            // A partial poll must remain visibly unsuccessful at the job level.
            if (feedRuntime.homeAssistant.lastError[0]) {
                error = feedRuntime.homeAssistant.lastError;
                success = false;
            }
            record(success, error);
        }
        lastSyncSuccess = anySuccess && firstError.isEmpty();
        lastSyncError = firstError;
    }
    if (feedJobsCancelled) {
        lastSyncSuccess = false;
        lastSyncError = "Feed job cancelled";
        if (searchStatus == FEED_SEARCH_READY || searchStatus == FEED_SEARCH_PENDING) {
            searchStatus = FEED_SEARCH_FAILED;
            searchError = lastSyncError;
        }
    }
    feedJobComplete = true;
}

void pumpFeedJob() {
    if (!feedJobRunning) {
        if (pendingSyncMask == 0 && searchStatus != FEED_SEARCH_PENDING) return;
        feedContinuation.reset(new (std::nothrow) cont_t);
        if (!feedContinuation) {
            lastSyncSuccess = false;
            lastSyncError = "Feed worker allocation failed";
            pendingSyncMask = 0;
            if (searchStatus == FEED_SEARCH_PENDING) {
                searchStatus = FEED_SEARCH_FAILED;
                searchError = lastSyncError;
            }
            return;
        }
        cont_init(feedContinuation.get());
        activeSyncMask = pendingSyncMask;
        pendingSyncMask = 0;
        feedJobRunning = true;
        feedJobComplete = false;
    }
    if (activeHttpClient && (feedJobsCancelled || millis() - activeRequestStartedMs >= kRequestDeadlineMs)) {
        activeRequestExpired = true;
        if (activeTlsClient) activeTlsClient->abortTransport();
        else activeHttpClient->abort();
    }
    // Every core yield suspends this worker to this call site. The main
    // continuation must be restored before returning to application code.
    struct ContinuationGuard {
        cont_t *previous;
        explicit ContinuationGuard(cont_t *worker) : previous(g_pcont) { g_pcont = worker; }
        ~ContinuationGuard() { g_pcont = previous; }
    };
    {
        ContinuationGuard guard(feedContinuation.get());
        cont_run(feedContinuation.get(), runFeedJob);
        cont_check(feedContinuation.get());
    }
    if (feedJobComplete) {
        feedJobRunning = false;
        activeSyncMask = 0;
        feedContinuation.reset();
    }
}

}  // namespace

void feedsResetToDefaults() {
    setConfigDefaults();
    clearAllRuntime();
    feedsStartupReadyAtMs = 0;
}

void feedsInit() {
    feedsResetToDefaults();
    if (!feedsLoadConfig()) {
        feedsSaveConfig();
    }
}

bool feedsLoadConfig() {
    setConfigDefaults();
    clearAllRuntime();

    bool success = loadJsonFile(FEEDS_CONFIG_PATH, [](JsonObjectConst root) {
        applyConfigObject(root);
    });

    normalizeConfig();
    clearRuntimeForInactiveSources();
    captureSavedConfig();
    return success;
}

bool feedsSaveConfig() {
    if (feedsBusy()) return false;
    normalizeConfig();

    JsonDocument doc;
    fillConfigJson(doc.to<JsonObject>());
    bool success = writeJsonToFile(FEEDS_CONFIG_PATH, doc);
    if (success) {
        captureSavedConfig();
    }
    return success;
}

bool feedsPreviewConfigJson(const String &json, String *error) {
    if (feedsBusy()) {
        if (error) *error = F("Feed job already running");
        return false;
    }
    JsonDocument doc;
    DeserializationError deserializeError = deserializeJson(doc, json);
    if (deserializeError) {
        if (error != nullptr) {
            *error = deserializeError.c_str();
        }
        return false;
    }

    FeedConfig previousConfig = feedConfig;
    memcpy(&feedConfig, &savedFeedConfig, sizeof(feedConfig));
    applyConfigObject(doc.as<JsonObjectConst>());
    clearRuntimeForInactiveSources();

    if (weatherIdentityChanged(previousConfig, feedConfig)) {
        clearWeatherRuntime();
    }

    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (marketIdentityChanged(previousConfig.markets[index], feedConfig.markets[index])) {
            clearMarketRuntime(index);
        }
    }

    if (homeAssistantIdentityChanged(previousConfig.homeAssistant, feedConfig.homeAssistant)) {
        clearHomeAssistantRuntime();
    }

    feedDraftActive = !feedConfigEquals(feedConfig, savedFeedConfig);
    return true;
}

bool feedsApplyConfigJson(const String &json, String *error) {
    if (!feedsPreviewConfigJson(json, error)) {
        return false;
    }

    if (!feedsSaveConfig()) {
        if (error != nullptr) {
            *error = F("Failed to write feed config");
        }
        return false;
    }

    return true;
}

void feedsDiscardDraftChanges() {
    if (feedsBusy()) return;
    FeedConfig previousConfig = feedConfig;
    memcpy(&feedConfig, &savedFeedConfig, sizeof(feedConfig));
    clearRuntimeForInactiveSources();

    if (weatherIdentityChanged(previousConfig, feedConfig)) {
        clearWeatherRuntime();
    }

    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (marketIdentityChanged(previousConfig.markets[index], feedConfig.markets[index])) {
            clearMarketRuntime(index);
        }
    }

    if (homeAssistantIdentityChanged(previousConfig.homeAssistant, feedConfig.homeAssistant)) {
        clearHomeAssistantRuntime();
    }

    feedDraftActive = false;
}

bool feedsHasDraftChanges() {
    return feedDraftActive;
}

void feedsBuildStateJson(String &json) {
    JsonDocument doc;
    fillConfigJson(doc["config"].to<JsonObject>());
    fillStatusJson(doc["status"].to<JsonObject>());
    JsonObject meta = doc["meta"].to<JsonObject>();
    meta["hasDraft"] = feedDraftActive;
    meta["syncPending"] = feedsBusy();
    meta["lastSyncSuccess"] = lastSyncSuccess;
    meta["lastSyncError"] = lastSyncError;
    json = "";
    serializeJson(doc, json);
}

bool feedsBusy() {
    return feedJobRunning || pendingSyncMask != 0 || searchStatus == FEED_SEARCH_PENDING;
}

void feedsCancelJobs() {
    feedJobsCancelled = true;
    pendingSyncMask = 0;
    if (!feedJobRunning && searchStatus == FEED_SEARCH_PENDING) {
        searchStatus = FEED_SEARCH_FAILED;
        searchError = "Feed job cancelled";
    }
    // Socket-only cancellation retains BearSSL engine and worker locals until
    // the suspended network call returns and its normal destructors unwind.
    if (activeHttpClient) {
        activeRequestExpired = true;
        if (activeTlsClient) activeTlsClient->abortTransport();
        else activeHttpClient->abort();
    }
}

bool feedsSyncNow(const char *scope, String *error) {
    if (feedsBusy()) {
        if (error) *error = F("Feed job already running");
        return false;
    }
    String value = scope ? scope : "all";
    value.toLowerCase();
    if (value != "all" && value != "weather" && value != "markets" && value != "home" && value != "home-assistant") {
        if (error) *error = F("Invalid feed scope");
        return false;
    }
    uint16_t jobs = 0;
    if ((value == "all" || value == "weather") && feedConfig.weather.source == WEATHER_FEED_OPEN_METEO && feedsWeatherConfigured()) jobs |= kWeatherJob;
    if (value == "all" || value == "markets") {
        for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
            if (feedsMarketConfigured(index) && (feedConfig.markets[index].source == MARKET_FEED_FINNHUB || feedConfig.markets[index].source == MARKET_FEED_COINGECKO)) jobs |= kMarketJobBase << index;
        }
    }
    if ((value == "all" || value == "home" || value == "home-assistant") && feedsHomeAssistantConfigured()) jobs |= kHomeAssistantJob;
    if (!jobs) {
        if (error) *error = F("No live feeds configured");
        return false;
    }
    feedJobsCancelled = false;
    pendingSyncMask = jobs;
    lastSyncError = "";
    return true;
}

bool feedsQueueWeatherSearch(const String &query, String *error) {
    String trimmed = query;
    trimmed.trim();
    if (trimmed.length() < 2 || trimmed.length() > 80 || feedsBusy()) {
        if (error) *error = feedsBusy() ? "Feed job already running" : "Search must contain 2 to 80 characters";
        return false;
    }
    feedJobsCancelled = false;
    searchQuery = trimmed;
    searchJson = "";
    searchError = "";
    searchStatus = FEED_SEARCH_PENDING;
    return true;
}

FeedSearchStatus feedsWeatherSearchResult(String &json, String *error) {
    if (searchStatus == FEED_SEARCH_READY) json = searchJson;
    if (searchStatus == FEED_SEARCH_FAILED && error) *error = searchError;
    return searchStatus;
}

bool feedsSearchWeatherLocations(const String &query, String &json, String *error) {
    JsonDocument providerDoc;
    if (!fetchWeatherSearchJson(query, providerDoc, error)) {
        return false;
    }

    JsonDocument resultDoc;
    JsonArray results = resultDoc["results"].to<JsonArray>();
    JsonArrayConst providerResults = providerDoc["results"].as<JsonArrayConst>();

    for (JsonObjectConst item : providerResults) {
        JsonObject result = results.add<JsonObject>();
        String label = buildWeatherLocationLabel(item);

        result["name"] = item["name"] | "";
        result["label"] = label;
        result["country"] = item["country"] | "";
        result["admin1"] = item["admin1"] | "";
        result["latitude"] = item["latitude"] | 0.0f;
        result["longitude"] = item["longitude"] | 0.0f;
        result["timezone"] = item["timezone"] | "";
    }

    json = "";
    serializeJson(resultDoc, json);
    return true;
}

void feedsLoop() {
    uint32_t now = millis();
    uint32_t staleAfterMs = static_cast<uint32_t>(feedConfig.homeAssistant.refreshMinutes) * 120000UL;
    for (auto &slot : feedRuntime.homeAssistant.slots) {
        if (slot.hasData && (slot.lastError[0] || now - slot.lastSuccessMs > staleAfterMs)) slot.stale = true;
    }
    if (feedsBusy()) {
        pumpFeedJob();
        return;
    }
    if (WiFi.status() != WL_CONNECTED) return;
    if (feedsStartupReadyAtMs == 0) {
        feedsStartupReadyAtMs = now + kFeedStartupGracePeriodMs;
        return;
    }
    if (static_cast<int32_t>(now - feedsStartupReadyAtMs) < 0) return;
    auto due = [&](uint32_t lastAttempt, uint16_t minutes) {
        return lastAttempt == 0 || now - lastAttempt >= static_cast<uint32_t>(minutes) * 60000UL;
    };
    if (feedConfig.weather.source == WEATHER_FEED_OPEN_METEO && feedsWeatherConfigured() && due(feedRuntime.weather.lastAttemptMs, feedConfig.weather.refreshMinutes)) pendingSyncMask |= kWeatherJob;
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (feedsMarketConfigured(index) && (feedConfig.markets[index].source == MARKET_FEED_FINNHUB || feedConfig.markets[index].source == MARKET_FEED_COINGECKO) && due(feedRuntime.markets[index].lastAttemptMs, feedConfig.markets[index].refreshMinutes)) pendingSyncMask |= kMarketJobBase << index;
    }
    if (feedsHomeAssistantConfigured() && due(feedRuntime.homeAssistant.lastAttemptMs, feedConfig.homeAssistant.refreshMinutes)) pendingSyncMask |= kHomeAssistantJob;
    if (pendingSyncMask) feedJobsCancelled = false;
    pumpFeedJob();
}

uint8_t feedsWeatherSource() {
    return feedConfig.weather.source;
}

uint8_t feedsMarketSource(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return MARKET_FEED_DISABLED;
    }

    return feedConfig.markets[index].source;
}

bool feedsHomeAssistantConfigured() {
    if (!feedConfig.homeAssistant.enabled) {
        return false;
    }

    if (!baseUrlValid(String(feedConfig.homeAssistant.baseUrl))) {
        return false;
    }

    if (feedConfig.homeAssistant.token[0] == '\0') {
        return false;
    }

    if (String(feedConfig.homeAssistant.baseUrl).startsWith("https://") &&
        feedConfig.homeAssistant.tlsMode == HOME_ASSISTANT_TLS_FINGERPRINT &&
        feedConfig.homeAssistant.fingerprint[0] == '\0') {
        return false;
    }

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (homeAssistantSlotConfigured(feedConfig.homeAssistant.slots[index])) {
            return true;
        }
    }

    return false;
}

bool feedsHasHomeAssistantData() {
    return feedRuntime.homeAssistant.hasData;
}

const HomeAssistantSlotData* feedsHomeAssistantSlotData(uint8_t index) {
    if (index >= HOME_ASSISTANT_SLOT_COUNT) {
        return nullptr;
    }

    const HomeAssistantSlotData &slot = feedRuntime.homeAssistant.slots[index];
    return slot.enabled && (slot.hasData || slot.lastError[0] != '\0')
               ? &feedRuntime.homeAssistant.slots[index]
               : nullptr;
}

bool feedsWeatherConfigured() {
    if (feedConfig.weather.source != WEATHER_FEED_OPEN_METEO) {
        return false;
    }

    if (weatherCoordinatesValid(feedConfig.weather.latitude, feedConfig.weather.longitude)) {
        return true;
    }

    String query = feedConfig.weather.query;
    query.trim();
    return query.length() >= 2;
}

bool feedsMarketConfigured(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return false;
    }

    const MarketFeedConfig &market = feedConfig.markets[index];
    if (market.source != MARKET_FEED_FINNHUB && market.source != MARKET_FEED_COINGECKO) {
        return false;
    }

    return market.symbol[0] != '\0';
}

bool feedsHasWeatherData() {
    return feedRuntime.weather.hasData;
}

bool feedsHasMarketData(uint8_t index) {
    return index < DASHBOARD_MARKET_COUNT && feedRuntime.markets[index].hasData;
}

bool feedsWeatherUsesFahrenheit() {
    return feedConfig.weather.source == WEATHER_FEED_OPEN_METEO && feedConfig.weather.useFahrenheit;
}

const WeatherData* feedsWeatherData() {
    return feedRuntime.weather.hasData ? &feedRuntime.weather.data : nullptr;
}

const MarketData* feedsMarketData(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT || !feedRuntime.markets[index].hasData) {
        return nullptr;
    }

    return &feedRuntime.markets[index].data;
}
