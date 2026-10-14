#include "dashboard.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <LittleFS.h>
#include <time.h>

DashboardConfig dashboardConfig;
DashboardData dashboardData;

namespace {

constexpr uint32_t kValidEpochFloor = 946684800UL;  // 2000-01-01T00:00:00Z
constexpr char kDefaultCustomBackground[] = "#080F1D";
constexpr char kDefaultCustomSurface[] = "#122035";
constexpr char kDefaultCustomAccent[] = "#58DAC1";
constexpr char kDefaultCustomText[] = "#F0F7FF";
constexpr char kDefaultNightCustomBackground[] = "#050B17";
constexpr char kDefaultNightCustomSurface[] = "#101A28";
constexpr char kDefaultNightCustomAccent[] = "#A8C7FA";
constexpr char kDefaultNightCustomText[] = "#F3F6FC";

DashboardConfig savedDashboardConfig;
DashboardData savedDashboardData;
bool dashboardConfigDraftActive = false;
bool dashboardDataDraftActive = false;

uint16_t normalizeMinutesOfDay(int value) {
    int normalized = value % (24 * 60);
    if (normalized < 0) {
        normalized += 24 * 60;
    }
    return static_cast<uint16_t>(normalized);
}

void enableAllPages() {
    for (uint8_t page = 0; page < DASHBOARD_PAGE_COUNT; ++page) {
        dashboardConfig.enabledPages[page] = true;
    }
}

bool anyPageEnabled() {
    for (uint8_t page = 0; page < DASHBOARD_PAGE_COUNT; ++page) {
        if (dashboardConfig.enabledPages[page]) {
            return true;
        }
    }

    return false;
}

void ensureAtLeastOnePageEnabled() {
    if (anyPageEnabled()) {
        return;
    }

    dashboardConfig.enabledPages[DASHBOARD_PAGE_CLOCK] = true;
}

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

    snprintf(destination, destinationSize, "%s", source);
}

bool isHexColorChar(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

void normalizeHexColor(char *destination, size_t destinationSize, const char *fallback) {
    if (destinationSize < 8) {
        return;
    }

    char normalized[8];
    normalized[0] = '#';
    normalized[7] = '\0';

    const char *source = destination;
    if (source == nullptr || source[0] == '\0') {
        copyString(destination, destinationSize, fallback);
        return;
    }

    if (source[0] == '#') {
        ++source;
    }

    for (uint8_t index = 0; index < 6; ++index) {
        char value = source[index];
        if (value == '\0' || !isHexColorChar(value)) {
            copyString(destination, destinationSize, fallback);
            return;
        }

        normalized[index + 1] = static_cast<char>(toupper(value));
    }

    if (source[6] != '\0') {
        copyString(destination, destinationSize, fallback);
        return;
    }

    copyString(destination, destinationSize, normalized);
}

void setConfigDefaults() {
    memset(&dashboardConfig, 0, sizeof(dashboardConfig));
    dashboardConfig.version = DASHBOARD_CONFIG_VERSION;
    dashboardConfig.theme = DASHBOARD_THEME_AURORA;
    dashboardConfig.customThemeEnabled = false;
    copyString(dashboardConfig.customBackground, sizeof(dashboardConfig.customBackground), kDefaultCustomBackground);
    copyString(dashboardConfig.customSurface, sizeof(dashboardConfig.customSurface), kDefaultCustomSurface);
    copyString(dashboardConfig.customAccent, sizeof(dashboardConfig.customAccent), kDefaultCustomAccent);
    copyString(dashboardConfig.customText, sizeof(dashboardConfig.customText), kDefaultCustomText);
    dashboardConfig.nightModeEnabled = false;
    dashboardConfig.nightStartMinutes = 22 * 60;
    dashboardConfig.nightEndMinutes = 7 * 60;
    dashboardConfig.nightBrightness = 14;
    dashboardConfig.nightThemeEnabled = false;
    dashboardConfig.nightTheme = DASHBOARD_THEME_AURORA;
    dashboardConfig.nightCustomThemeEnabled = false;
    copyString(dashboardConfig.nightCustomBackground,
               sizeof(dashboardConfig.nightCustomBackground),
               kDefaultNightCustomBackground);
    copyString(dashboardConfig.nightCustomSurface,
               sizeof(dashboardConfig.nightCustomSurface),
               kDefaultNightCustomSurface);
    copyString(dashboardConfig.nightCustomAccent,
               sizeof(dashboardConfig.nightCustomAccent),
               kDefaultNightCustomAccent);
    copyString(dashboardConfig.nightCustomText,
               sizeof(dashboardConfig.nightCustomText),
               kDefaultNightCustomText);
    dashboardConfig.rotationEnabled = true;
    dashboardConfig.rotationIntervalSec = 10;
    dashboardConfig.use24Hour = true;
    dashboardConfig.showSeconds = false;
    dashboardConfig.showIp = false;
    enableAllPages();
}

void setDataDefaults() {
    memset(&dashboardData, 0, sizeof(dashboardData));

    dashboardData.weather.temperature = 21;
    dashboardData.weather.high = 24;
    dashboardData.weather.low = 18;
    dashboardData.weather.rainChance = 10;

    dashboardData.focus.durationMinutes = 25;
    dashboardData.focus.remainingSeconds = 25 * 60;
    dashboardData.focus.label[0] = '\0';
}

void normalizeConfig() {
    dashboardConfig.version = DASHBOARD_CONFIG_VERSION;
    dashboardConfig.theme = constrain(dashboardConfig.theme, 0, DASHBOARD_THEME_COUNT - 1);
    normalizeHexColor(dashboardConfig.customBackground, sizeof(dashboardConfig.customBackground), kDefaultCustomBackground);
    normalizeHexColor(dashboardConfig.customSurface, sizeof(dashboardConfig.customSurface), kDefaultCustomSurface);
    normalizeHexColor(dashboardConfig.customAccent, sizeof(dashboardConfig.customAccent), kDefaultCustomAccent);
    normalizeHexColor(dashboardConfig.customText, sizeof(dashboardConfig.customText), kDefaultCustomText);
    dashboardConfig.nightStartMinutes = normalizeMinutesOfDay(dashboardConfig.nightStartMinutes);
    dashboardConfig.nightEndMinutes = normalizeMinutesOfDay(dashboardConfig.nightEndMinutes);
    dashboardConfig.nightBrightness = constrain(dashboardConfig.nightBrightness, 1, 100);
    dashboardConfig.nightTheme = constrain(dashboardConfig.nightTheme, 0, DASHBOARD_THEME_COUNT - 1);
    normalizeHexColor(dashboardConfig.nightCustomBackground,
                      sizeof(dashboardConfig.nightCustomBackground),
                      kDefaultNightCustomBackground);
    normalizeHexColor(dashboardConfig.nightCustomSurface,
                      sizeof(dashboardConfig.nightCustomSurface),
                      kDefaultNightCustomSurface);
    normalizeHexColor(dashboardConfig.nightCustomAccent,
                      sizeof(dashboardConfig.nightCustomAccent),
                      kDefaultNightCustomAccent);
    normalizeHexColor(dashboardConfig.nightCustomText,
                      sizeof(dashboardConfig.nightCustomText),
                      kDefaultNightCustomText);
    dashboardConfig.rotationIntervalSec = constrain(dashboardConfig.rotationIntervalSec, 3, 120);
    ensureAtLeastOnePageEnabled();
}

void normalizeData() {
    dashboardData.weather.rainChance = constrain(dashboardData.weather.rainChance, 0, 100);

    for (uint8_t marketIndex = 0; marketIndex < DASHBOARD_MARKET_COUNT; ++marketIndex) {
        dashboardData.markets[marketIndex].symbol[sizeof(dashboardData.markets[marketIndex].symbol) - 1] = '\0';
        dashboardData.markets[marketIndex].label[sizeof(dashboardData.markets[marketIndex].label) - 1] = '\0';
    }

    dashboardData.focus.durationMinutes = constrain(dashboardData.focus.durationMinutes, 1, 240);
    dashboardData.focus.remainingSeconds = constrain(dashboardData.focus.remainingSeconds, 0UL, 24UL * 3600UL);

    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        dashboardData.worldClocks[clockIndex].offsetSeconds =
            constrain(dashboardData.worldClocks[clockIndex].offsetSeconds, -12L * 3600L, 14L * 3600L);
        dashboardData.worldClocks[clockIndex].label[sizeof(dashboardData.worldClocks[clockIndex].label) - 1] = '\0';
    }

    dashboardData.weather.location[sizeof(dashboardData.weather.location) - 1] = '\0';
    dashboardData.weather.condition[sizeof(dashboardData.weather.condition) - 1] = '\0';
    dashboardData.focus.label[sizeof(dashboardData.focus.label) - 1] = '\0';
    dashboardData.event.title[sizeof(dashboardData.event.title) - 1] = '\0';
    dashboardData.event.subtitle[sizeof(dashboardData.event.subtitle) - 1] = '\0';
    dashboardData.quote.text[sizeof(dashboardData.quote.text) - 1] = '\0';
    dashboardData.quote.author[sizeof(dashboardData.quote.author) - 1] = '\0';
    dashboardData.status.line1[sizeof(dashboardData.status.line1) - 1] = '\0';
    dashboardData.status.line2[sizeof(dashboardData.status.line2) - 1] = '\0';

    dashboardData.event.remainingSeconds = constrain(dashboardData.event.remainingSeconds, 0UL, 7UL * 24UL * 3600UL);
    if (dashboardData.focus.updatedAtEpoch != 0 && dashboardData.focus.updatedAtEpoch < kValidEpochFloor) {
        dashboardData.focus.updatedAtEpoch = 0;
    }
    if (dashboardData.event.updatedAtEpoch != 0 && dashboardData.event.updatedAtEpoch < kValidEpochFloor) {
        dashboardData.event.updatedAtEpoch = 0;
    }
}

bool weatherEquals(const WeatherData &left, const WeatherData &right) {
    return strcmp(left.location, right.location) == 0 &&
           strcmp(left.condition, right.condition) == 0 &&
           left.temperature == right.temperature &&
           left.high == right.high &&
           left.low == right.low &&
           left.rainChance == right.rainChance;
}

bool marketEquals(const MarketData &left, const MarketData &right) {
    return left.enabled == right.enabled &&
           strcmp(left.symbol, right.symbol) == 0 &&
           strcmp(left.label, right.label) == 0 &&
           left.price == right.price &&
           left.change == right.change &&
           left.changePercent == right.changePercent;
}

bool focusEquals(const FocusData &left, const FocusData &right) {
    return strcmp(left.label, right.label) == 0 &&
           left.running == right.running &&
           left.breakMode == right.breakMode &&
           left.durationMinutes == right.durationMinutes &&
           left.remainingSeconds == right.remainingSeconds &&
           left.updatedAtEpoch == right.updatedAtEpoch;
}

bool worldClockEquals(const WorldClockData &left, const WorldClockData &right) {
    return left.enabled == right.enabled &&
           strcmp(left.label, right.label) == 0 &&
           left.offsetSeconds == right.offsetSeconds;
}

bool eventEquals(const EventData &left, const EventData &right) {
    return strcmp(left.title, right.title) == 0 &&
           strcmp(left.subtitle, right.subtitle) == 0 &&
           left.remainingSeconds == right.remainingSeconds &&
           left.updatedAtEpoch == right.updatedAtEpoch;
}

bool quoteEquals(const QuoteData &left, const QuoteData &right) {
    return strcmp(left.text, right.text) == 0 &&
           strcmp(left.author, right.author) == 0;
}

bool statusEquals(const StatusData &left, const StatusData &right) {
    return strcmp(left.line1, right.line1) == 0 &&
           strcmp(left.line2, right.line2) == 0;
}

bool configEquals(const DashboardConfig &left, const DashboardConfig &right) {
    if (left.version != right.version ||
        left.theme != right.theme ||
        left.customThemeEnabled != right.customThemeEnabled ||
        strcmp(left.customBackground, right.customBackground) != 0 ||
        strcmp(left.customSurface, right.customSurface) != 0 ||
        strcmp(left.customAccent, right.customAccent) != 0 ||
        strcmp(left.customText, right.customText) != 0 ||
        left.nightModeEnabled != right.nightModeEnabled ||
        left.nightStartMinutes != right.nightStartMinutes ||
        left.nightEndMinutes != right.nightEndMinutes ||
        left.nightBrightness != right.nightBrightness ||
        left.nightThemeEnabled != right.nightThemeEnabled ||
        left.nightTheme != right.nightTheme ||
        left.nightCustomThemeEnabled != right.nightCustomThemeEnabled ||
        strcmp(left.nightCustomBackground, right.nightCustomBackground) != 0 ||
        strcmp(left.nightCustomSurface, right.nightCustomSurface) != 0 ||
        strcmp(left.nightCustomAccent, right.nightCustomAccent) != 0 ||
        strcmp(left.nightCustomText, right.nightCustomText) != 0 ||
        left.rotationEnabled != right.rotationEnabled ||
        left.rotationIntervalSec != right.rotationIntervalSec ||
        left.use24Hour != right.use24Hour ||
        left.showSeconds != right.showSeconds ||
        left.showIp != right.showIp) {
        return false;
    }

    for (uint8_t page = 0; page < DASHBOARD_PAGE_COUNT; ++page) {
        if (left.enabledPages[page] != right.enabledPages[page]) {
            return false;
        }
    }

    return true;
}

bool dataEquals(const DashboardData &left, const DashboardData &right) {
    if (!weatherEquals(left.weather, right.weather) ||
        !focusEquals(left.focus, right.focus) ||
        !eventEquals(left.event, right.event) ||
        !quoteEquals(left.quote, right.quote) ||
        !statusEquals(left.status, right.status)) {
        return false;
    }

    for (uint8_t marketIndex = 0; marketIndex < DASHBOARD_MARKET_COUNT; ++marketIndex) {
        if (!marketEquals(left.markets[marketIndex], right.markets[marketIndex])) {
            return false;
        }
    }

    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        if (!worldClockEquals(left.worldClocks[clockIndex], right.worldClocks[clockIndex])) {
            return false;
        }
    }

    return true;
}

void captureSavedConfig() {
    memcpy(&savedDashboardConfig, &dashboardConfig, sizeof(savedDashboardConfig));
    dashboardConfigDraftActive = false;
}

void captureSavedData() {
    memcpy(&savedDashboardData, &dashboardData, sizeof(savedDashboardData));
    dashboardDataDraftActive = false;
}

void fillConfigJson(JsonObject root) {
    root["version"] = dashboardConfig.version;
    root["theme"] = dashboardConfig.theme;
    root["customThemeEnabled"] = dashboardConfig.customThemeEnabled;
    root["rotationEnabled"] = dashboardConfig.rotationEnabled;
    root["rotationIntervalSec"] = dashboardConfig.rotationIntervalSec;
    root["use24Hour"] = dashboardConfig.use24Hour;
    root["showSeconds"] = dashboardConfig.showSeconds;
    root["showIp"] = dashboardConfig.showIp;

    JsonObject customTheme = root["customTheme"].to<JsonObject>();
    customTheme["background"] = dashboardConfig.customBackground;
    customTheme["surface"] = dashboardConfig.customSurface;
    customTheme["accent"] = dashboardConfig.customAccent;
    customTheme["text"] = dashboardConfig.customText;

    JsonObject nightMode = root["nightMode"].to<JsonObject>();
    nightMode["enabled"] = dashboardConfig.nightModeEnabled;
    nightMode["startMinutes"] = dashboardConfig.nightStartMinutes;
    nightMode["endMinutes"] = dashboardConfig.nightEndMinutes;
    nightMode["brightness"] = dashboardConfig.nightBrightness;
    nightMode["themeEnabled"] = dashboardConfig.nightThemeEnabled;
    nightMode["theme"] = dashboardConfig.nightTheme;
    nightMode["customThemeEnabled"] = dashboardConfig.nightCustomThemeEnabled;

    JsonObject nightCustomTheme = nightMode["customTheme"].to<JsonObject>();
    nightCustomTheme["background"] = dashboardConfig.nightCustomBackground;
    nightCustomTheme["surface"] = dashboardConfig.nightCustomSurface;
    nightCustomTheme["accent"] = dashboardConfig.nightCustomAccent;
    nightCustomTheme["text"] = dashboardConfig.nightCustomText;

    JsonObject pages = root["pages"].to<JsonObject>();
    pages["clock"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_CLOCK];
    pages["weather"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_WEATHER];
    pages["markets"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_MARKETS];
    pages["home"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_HOME];
    pages["focus"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_FOCUS];
    pages["world"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_WORLD];
    pages["event"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_EVENT];
    pages["quote"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_QUOTE];
    pages["status"] = dashboardConfig.enabledPages[DASHBOARD_PAGE_STATUS];
}

void fillDataJson(JsonObject root) {
    JsonObject weather = root["weather"].to<JsonObject>();
    weather["location"] = dashboardData.weather.location;
    weather["condition"] = dashboardData.weather.condition;
    weather["temperature"] = dashboardData.weather.temperature;
    weather["high"] = dashboardData.weather.high;
    weather["low"] = dashboardData.weather.low;
    weather["rainChance"] = dashboardData.weather.rainChance;

    JsonArray markets = root["markets"].to<JsonArray>();
    for (uint8_t marketIndex = 0; marketIndex < DASHBOARD_MARKET_COUNT; ++marketIndex) {
        JsonObject market = markets.add<JsonObject>();
        market["enabled"] = dashboardData.markets[marketIndex].enabled;
        market["symbol"] = dashboardData.markets[marketIndex].symbol;
        market["label"] = dashboardData.markets[marketIndex].label;
        market["price"] = dashboardData.markets[marketIndex].price;
        market["change"] = dashboardData.markets[marketIndex].change;
        market["changePercent"] = dashboardData.markets[marketIndex].changePercent;
    }

    JsonObject focus = root["focus"].to<JsonObject>();
    focus["label"] = dashboardData.focus.label;
    focus["running"] = dashboardData.focus.running;
    focus["breakMode"] = dashboardData.focus.breakMode;
    focus["durationMinutes"] = dashboardData.focus.durationMinutes;
    focus["remainingSeconds"] = dashboardData.focus.remainingSeconds;
    focus["updatedAtEpoch"] = dashboardData.focus.updatedAtEpoch;

    JsonArray worldClocks = root["worldClocks"].to<JsonArray>();
    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        JsonObject clock = worldClocks.add<JsonObject>();
        clock["enabled"] = dashboardData.worldClocks[clockIndex].enabled;
        clock["label"] = dashboardData.worldClocks[clockIndex].label;
        clock["offsetSeconds"] = dashboardData.worldClocks[clockIndex].offsetSeconds;
    }

    JsonObject event = root["event"].to<JsonObject>();
    event["title"] = dashboardData.event.title;
    event["subtitle"] = dashboardData.event.subtitle;
    event["remainingSeconds"] = dashboardData.event.remainingSeconds;
    event["updatedAtEpoch"] = dashboardData.event.updatedAtEpoch;

    JsonObject quote = root["quote"].to<JsonObject>();
    quote["text"] = dashboardData.quote.text;
    quote["author"] = dashboardData.quote.author;

    JsonObject status = root["status"].to<JsonObject>();
    status["line1"] = dashboardData.status.line1;
    status["line2"] = dashboardData.status.line2;
}

void applyPagesObject(JsonObjectConst pages) {
    if (pages.isNull()) {
        return;
    }

    if (!pages["clock"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_CLOCK] = pages["clock"].as<bool>();
    }
    if (!pages["weather"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_WEATHER] = pages["weather"].as<bool>();
    }
    if (!pages["markets"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_MARKETS] = pages["markets"].as<bool>();
    }
    if (!pages["home"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_HOME] = pages["home"].as<bool>();
    }
    if (!pages["focus"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_FOCUS] = pages["focus"].as<bool>();
    }
    if (!pages["world"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_WORLD] = pages["world"].as<bool>();
    }
    if (!pages["event"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_EVENT] = pages["event"].as<bool>();
    }
    if (!pages["quote"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_QUOTE] = pages["quote"].as<bool>();
    }
    if (!pages["status"].isNull()) {
        dashboardConfig.enabledPages[DASHBOARD_PAGE_STATUS] = pages["status"].as<bool>();
    }
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

    bool success = serializeJson(doc, file) > 0;
    file.flush();
    file.close();
    if (!success) {
        LittleFS.remove(tempPath);
        return false;
    }

    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
    }

    if (!LittleFS.rename(tempPath, path)) {
        LittleFS.remove(tempPath);
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

void applyConfigObject(JsonObjectConst root) {
    if (!root["theme"].isNull()) {
        dashboardConfig.theme = root["theme"].as<uint8_t>();
    }
    if (!root["customThemeEnabled"].isNull()) {
        dashboardConfig.customThemeEnabled = root["customThemeEnabled"].as<bool>();
    }
    if (!root["rotationEnabled"].isNull()) {
        dashboardConfig.rotationEnabled = root["rotationEnabled"].as<bool>();
    }
    if (!root["rotationIntervalSec"].isNull()) {
        dashboardConfig.rotationIntervalSec = root["rotationIntervalSec"].as<uint16_t>();
    }
    if (!root["use24Hour"].isNull()) {
        dashboardConfig.use24Hour = root["use24Hour"].as<bool>();
    }
    if (!root["showSeconds"].isNull()) {
        dashboardConfig.showSeconds = root["showSeconds"].as<bool>();
    }
    if (!root["showIp"].isNull()) {
        dashboardConfig.showIp = root["showIp"].as<bool>();
    }

    JsonObjectConst customTheme = root["customTheme"].as<JsonObjectConst>();
    if (!customTheme.isNull()) {
        if (!customTheme["background"].isNull()) {
            copyString(dashboardConfig.customBackground, sizeof(dashboardConfig.customBackground), customTheme["background"]);
        }
        if (!customTheme["surface"].isNull()) {
            copyString(dashboardConfig.customSurface, sizeof(dashboardConfig.customSurface), customTheme["surface"]);
        }
        if (!customTheme["accent"].isNull()) {
            copyString(dashboardConfig.customAccent, sizeof(dashboardConfig.customAccent), customTheme["accent"]);
        }
        if (!customTheme["text"].isNull()) {
            copyString(dashboardConfig.customText, sizeof(dashboardConfig.customText), customTheme["text"]);
        }
    }

    JsonObjectConst nightMode = root["nightMode"].as<JsonObjectConst>();
    if (!nightMode.isNull()) {
        if (!nightMode["enabled"].isNull()) {
            dashboardConfig.nightModeEnabled = nightMode["enabled"].as<bool>();
        }
        if (!nightMode["startMinutes"].isNull()) {
            dashboardConfig.nightStartMinutes = nightMode["startMinutes"].as<uint16_t>();
        }
        if (!nightMode["endMinutes"].isNull()) {
            dashboardConfig.nightEndMinutes = nightMode["endMinutes"].as<uint16_t>();
        }
        if (!nightMode["brightness"].isNull()) {
            dashboardConfig.nightBrightness = nightMode["brightness"].as<uint8_t>();
        }
        if (!nightMode["themeEnabled"].isNull()) {
            dashboardConfig.nightThemeEnabled = nightMode["themeEnabled"].as<bool>();
        }
        if (!nightMode["theme"].isNull()) {
            dashboardConfig.nightTheme = nightMode["theme"].as<uint8_t>();
        }
        if (!nightMode["customThemeEnabled"].isNull()) {
            dashboardConfig.nightCustomThemeEnabled = nightMode["customThemeEnabled"].as<bool>();
        }

        JsonObjectConst nightCustomTheme = nightMode["customTheme"].as<JsonObjectConst>();
        if (!nightCustomTheme.isNull()) {
            if (!nightCustomTheme["background"].isNull()) {
                copyString(dashboardConfig.nightCustomBackground,
                           sizeof(dashboardConfig.nightCustomBackground),
                           nightCustomTheme["background"]);
            }
            if (!nightCustomTheme["surface"].isNull()) {
                copyString(dashboardConfig.nightCustomSurface,
                           sizeof(dashboardConfig.nightCustomSurface),
                           nightCustomTheme["surface"]);
            }
            if (!nightCustomTheme["accent"].isNull()) {
                copyString(dashboardConfig.nightCustomAccent,
                           sizeof(dashboardConfig.nightCustomAccent),
                           nightCustomTheme["accent"]);
            }
            if (!nightCustomTheme["text"].isNull()) {
                copyString(dashboardConfig.nightCustomText,
                           sizeof(dashboardConfig.nightCustomText),
                           nightCustomTheme["text"]);
            }
        }
    }

    applyPagesObject(root["pages"].as<JsonObjectConst>());
    normalizeConfig();
}

void applyWeatherObject(JsonObjectConst weather) {
    if (weather.isNull()) {
        return;
    }

    if (!weather["location"].isNull()) {
        copyString(dashboardData.weather.location, sizeof(dashboardData.weather.location), weather["location"]);
    }
    if (!weather["condition"].isNull()) {
        copyString(dashboardData.weather.condition, sizeof(dashboardData.weather.condition), weather["condition"]);
    }
    if (!weather["temperature"].isNull()) {
        dashboardData.weather.temperature = weather["temperature"].as<int>();
    }
    if (!weather["high"].isNull()) {
        dashboardData.weather.high = weather["high"].as<int>();
    }
    if (!weather["low"].isNull()) {
        dashboardData.weather.low = weather["low"].as<int>();
    }
    if (!weather["rainChance"].isNull()) {
        dashboardData.weather.rainChance = weather["rainChance"].as<int>();
    }
}

void applyMarketsArray(JsonArrayConst markets) {
    if (markets.isNull()) {
        return;
    }

    uint8_t marketIndex = 0;
    for (JsonObjectConst market : markets) {
        if (marketIndex >= DASHBOARD_MARKET_COUNT) {
            break;
        }

        if (!market["enabled"].isNull()) {
            dashboardData.markets[marketIndex].enabled = market["enabled"].as<bool>();
        }
        if (!market["symbol"].isNull()) {
            copyString(dashboardData.markets[marketIndex].symbol,
                       sizeof(dashboardData.markets[marketIndex].symbol),
                       market["symbol"]);
        }
        if (!market["label"].isNull()) {
            copyString(dashboardData.markets[marketIndex].label,
                       sizeof(dashboardData.markets[marketIndex].label),
                       market["label"]);
        }
        if (!market["price"].isNull()) {
            dashboardData.markets[marketIndex].price = market["price"].as<float>();
        }
        if (!market["change"].isNull()) {
            dashboardData.markets[marketIndex].change = market["change"].as<float>();
        }
        if (!market["changePercent"].isNull()) {
            dashboardData.markets[marketIndex].changePercent = market["changePercent"].as<float>();
        }

        ++marketIndex;
    }
}

void applyFocusObject(JsonObjectConst focus) {
    if (focus.isNull()) {
        return;
    }

    bool wasRunning = dashboardData.focus.running;
    bool runningProvided = !focus["running"].isNull();
    bool remainingProvided = !focus["remainingSeconds"].isNull();
    bool remainingChanged = remainingProvided && focus["remainingSeconds"].as<uint32_t>() != dashboardData.focus.remainingSeconds;
    bool updatedAtProvided = !focus["updatedAtEpoch"].isNull();
    int32_t currentRemaining = dashboardFocusRemainingSeconds();

    if (!focus["label"].isNull()) {
        copyString(dashboardData.focus.label, sizeof(dashboardData.focus.label), focus["label"]);
    }
    if (!focus["breakMode"].isNull()) {
        dashboardData.focus.breakMode = focus["breakMode"].as<bool>();
    }
    if (!focus["durationMinutes"].isNull()) {
        dashboardData.focus.durationMinutes = focus["durationMinutes"].as<uint16_t>();
    }
    if (remainingProvided) {
        dashboardData.focus.remainingSeconds = focus["remainingSeconds"].as<uint32_t>();
    }
    if (updatedAtProvided) {
        dashboardData.focus.updatedAtEpoch = focus["updatedAtEpoch"].as<uint32_t>();
    }
    if (runningProvided) {
        bool newRunning = focus["running"].as<bool>();
        if (wasRunning && !newRunning && !remainingChanged) {
            dashboardData.focus.remainingSeconds = currentRemaining > 0 ? static_cast<uint32_t>(currentRemaining) : 0U;
        }
        dashboardData.focus.running = newRunning;
    }

    uint32_t now = dashboardCurrentEpoch();
    if (remainingChanged && !updatedAtProvided) {
        dashboardData.focus.updatedAtEpoch = now;
    } else if (runningProvided && dashboardData.focus.running && !wasRunning && !updatedAtProvided) {
        dashboardData.focus.updatedAtEpoch = now;
    }
}

void applyWorldClocksArray(JsonArrayConst worldClocks) {
    if (worldClocks.isNull()) {
        return;
    }

    uint8_t clockIndex = 0;
    for (JsonObjectConst clock : worldClocks) {
        if (clockIndex >= DASHBOARD_WORLD_CLOCK_COUNT) {
            break;
        }

        if (!clock["enabled"].isNull()) {
            dashboardData.worldClocks[clockIndex].enabled = clock["enabled"].as<bool>();
        }
        if (!clock["label"].isNull()) {
            copyString(dashboardData.worldClocks[clockIndex].label,
                       sizeof(dashboardData.worldClocks[clockIndex].label),
                       clock["label"]);
        }
        if (!clock["offsetSeconds"].isNull()) {
            dashboardData.worldClocks[clockIndex].offsetSeconds = clock["offsetSeconds"].as<long>();
        }

        ++clockIndex;
    }
}

void applyEventObject(JsonObjectConst event) {
    if (event.isNull()) {
        return;
    }

    if (!event["title"].isNull()) {
        copyString(dashboardData.event.title, sizeof(dashboardData.event.title), event["title"]);
    }
    if (!event["subtitle"].isNull()) {
        copyString(dashboardData.event.subtitle, sizeof(dashboardData.event.subtitle), event["subtitle"]);
    }
    if (!event["remainingSeconds"].isNull()) {
        bool remainingChanged = dashboardData.event.remainingSeconds != event["remainingSeconds"].as<uint32_t>();
        dashboardData.event.remainingSeconds = event["remainingSeconds"].as<uint32_t>();
        if (remainingChanged && event["updatedAtEpoch"].isNull()) {
            dashboardData.event.updatedAtEpoch = dashboardCurrentEpoch();
        }
    }
    if (!event["updatedAtEpoch"].isNull()) {
        dashboardData.event.updatedAtEpoch = event["updatedAtEpoch"].as<uint32_t>();
    }
}

void applyQuoteObject(JsonObjectConst quote) {
    if (quote.isNull()) {
        return;
    }

    if (!quote["text"].isNull()) {
        copyString(dashboardData.quote.text, sizeof(dashboardData.quote.text), quote["text"]);
    }
    if (!quote["author"].isNull()) {
        copyString(dashboardData.quote.author, sizeof(dashboardData.quote.author), quote["author"]);
    }
}

void applyStatusObject(JsonObjectConst status) {
    if (status.isNull()) {
        return;
    }

    if (!status["line1"].isNull()) {
        copyString(dashboardData.status.line1, sizeof(dashboardData.status.line1), status["line1"]);
    }
    if (!status["line2"].isNull()) {
        copyString(dashboardData.status.line2, sizeof(dashboardData.status.line2), status["line2"]);
    }
}

void applyDataObject(JsonObjectConst root) {
    applyWeatherObject(root["weather"].as<JsonObjectConst>());
    applyMarketsArray(root["markets"].as<JsonArrayConst>());
    applyFocusObject(root["focus"].as<JsonObjectConst>());
    applyWorldClocksArray(root["worldClocks"].as<JsonArrayConst>());
    applyEventObject(root["event"].as<JsonObjectConst>());
    applyQuoteObject(root["quote"].as<JsonObjectConst>());
    applyStatusObject(root["status"].as<JsonObjectConst>());
    normalizeData();
}

}  // namespace

void dashboardResetToDefaults() {
    setConfigDefaults();
    setDataDefaults();
}

void dashboardInit() {
    dashboardResetToDefaults();

    bool configLoaded = dashboardLoadConfig();
    bool dataLoaded = dashboardLoadData();

    if (!configLoaded) {
        dashboardSaveConfig();
    }
    if (!dataLoaded) {
        dashboardSaveData();
    }
}

bool dashboardLoadConfig() {
    setConfigDefaults();
    bool success = loadJsonFile(DASHBOARD_CONFIG_PATH, [](JsonObjectConst root) {
        applyConfigObject(root);
    });

    normalizeConfig();
    captureSavedConfig();
    return success;
}

bool dashboardLoadData() {
    setDataDefaults();
    bool success = loadJsonFile(DASHBOARD_DATA_PATH, [](JsonObjectConst root) {
        applyDataObject(root);
    });

    normalizeData();
    captureSavedData();
    return success;
}

bool dashboardSaveConfig() {
    normalizeConfig();

    JsonDocument doc;
    fillConfigJson(doc.to<JsonObject>());
    bool success = writeJsonToFile(DASHBOARD_CONFIG_PATH, doc);
    if (success) {
        captureSavedConfig();
    }
    return success;
}

bool dashboardSaveData() {
    normalizeData();

    JsonDocument doc;
    fillDataJson(doc.to<JsonObject>());
    bool success = writeJsonToFile(DASHBOARD_DATA_PATH, doc);
    if (success) {
        captureSavedData();
    }
    return success;
}

bool dashboardSaveAll() {
    return dashboardSaveConfig() && dashboardSaveData();
}

bool dashboardApplyConfigJson(const String &json, String *error) {
    JsonDocument doc;
    DeserializationError deserializeError = deserializeJson(doc, json);
    if (deserializeError) {
        if (error != nullptr) {
            *error = deserializeError.c_str();
        }
        return false;
    }

    applyConfigObject(doc.as<JsonObjectConst>());
    if (!dashboardSaveConfig()) {
        if (error != nullptr) {
            *error = "Failed to write config";
        }
        return false;
    }

    return true;
}

bool dashboardApplyDataJson(const String &json, String *error) {
    JsonDocument doc;
    DeserializationError deserializeError = deserializeJson(doc, json);
    if (deserializeError) {
        if (error != nullptr) {
            *error = deserializeError.c_str();
        }
        return false;
    }

    applyDataObject(doc.as<JsonObjectConst>());
    if (!dashboardSaveData()) {
        if (error != nullptr) {
            *error = "Failed to write data";
        }
        return false;
    }

    return true;
}

bool dashboardPreviewConfigJson(const String &json, String *error) {
    JsonDocument doc;
    DeserializationError deserializeError = deserializeJson(doc, json);
    if (deserializeError) {
        if (error != nullptr) {
            *error = deserializeError.c_str();
        }
        return false;
    }

    memcpy(&dashboardConfig, &savedDashboardConfig, sizeof(dashboardConfig));
    applyConfigObject(doc.as<JsonObjectConst>());
    dashboardConfigDraftActive = !configEquals(dashboardConfig, savedDashboardConfig);
    return true;
}

bool dashboardPreviewDataJson(const String &json, String *error) {
    JsonDocument doc;
    DeserializationError deserializeError = deserializeJson(doc, json);
    if (deserializeError) {
        if (error != nullptr) {
            *error = deserializeError.c_str();
        }
        return false;
    }

    FocusData previousFocus = dashboardData.focus;
    EventData previousEvent = dashboardData.event;
    memcpy(&dashboardData, &savedDashboardData, sizeof(dashboardData));
    applyDataObject(doc.as<JsonObjectConst>());
    JsonObjectConst focus = doc["focus"].as<JsonObjectConst>();
    JsonObjectConst event = doc["event"].as<JsonObjectConst>();
    if (!focus["remainingSeconds"].isNull() && focus["updatedAtEpoch"].isNull() &&
        dashboardData.focus.remainingSeconds == previousFocus.remainingSeconds &&
        dashboardData.focus.running == previousFocus.running) {
        dashboardData.focus.updatedAtEpoch = previousFocus.updatedAtEpoch;
    }
    if (!event["remainingSeconds"].isNull() && event["updatedAtEpoch"].isNull() &&
        dashboardData.event.remainingSeconds == previousEvent.remainingSeconds) {
        dashboardData.event.updatedAtEpoch = previousEvent.updatedAtEpoch;
    }
    dashboardDataDraftActive = !dataEquals(dashboardData, savedDashboardData);
    return true;
}

void dashboardDiscardDraftChanges() {
    memcpy(&dashboardConfig, &savedDashboardConfig, sizeof(dashboardConfig));
    memcpy(&dashboardData, &savedDashboardData, sizeof(dashboardData));
    dashboardConfigDraftActive = false;
    dashboardDataDraftActive = false;
}

bool dashboardHasDraftChanges() {
    return dashboardConfigDraftActive || dashboardDataDraftActive;
}

void dashboardBuildConfigJson(String &json) {
    JsonDocument doc;
    fillConfigJson(doc.to<JsonObject>());
    json = "";
    serializeJson(doc, json);
}

void dashboardBuildDataJson(String &json) {
    JsonDocument doc;
    fillDataJson(doc.to<JsonObject>());
    json = "";
    serializeJson(doc, json);
}

void dashboardBuildFullJson(String &json) {
    JsonDocument doc;
    fillConfigJson(doc["config"].to<JsonObject>());
    fillDataJson(doc["data"].to<JsonObject>());
    JsonObject meta = doc["meta"].to<JsonObject>();
    meta["configDraft"] = dashboardConfigDraftActive;
    meta["dataDraft"] = dashboardDataDraftActive;
    meta["hasDraft"] = dashboardHasDraftChanges();
    json = "";
    serializeJson(doc, json);
}

bool dashboardSyncRuntimeState() {
    uint32_t now = dashboardCurrentEpoch();
    if (now == 0) {
        return false;
    }

    bool changed = false;

    if (dashboardData.focus.running &&
        dashboardData.focus.remainingSeconds > 0 &&
        dashboardData.focus.updatedAtEpoch == 0) {
        dashboardData.focus.updatedAtEpoch = now;
        changed = true;
    }

    if (dashboardData.event.remainingSeconds > 0 &&
        dashboardData.event.updatedAtEpoch == 0) {
        dashboardData.event.updatedAtEpoch = now;
        changed = true;
    }

    if (changed) {
        normalizeData();
        dashboardSaveData();
    }

    return changed;
}

bool dashboardPageEnabled(uint8_t pageId) {
    if (pageId >= DASHBOARD_PAGE_COUNT) {
        return false;
    }

    return dashboardConfig.enabledPages[pageId];
}

uint8_t dashboardFirstEnabledPage() {
    for (uint8_t page = 0; page < DASHBOARD_PAGE_COUNT; ++page) {
        if (dashboardPageEnabled(page)) {
            return page;
        }
    }

    return DASHBOARD_PAGE_CLOCK;
}

uint8_t dashboardNextEnabledPage(uint8_t currentPage) {
    for (uint8_t offset = 1; offset <= DASHBOARD_PAGE_COUNT; ++offset) {
        uint8_t page = (currentPage + offset) % DASHBOARD_PAGE_COUNT;
        if (dashboardPageEnabled(page)) {
            return page;
        }
    }

    return DASHBOARD_PAGE_CLOCK;
}

const char* dashboardPageName(uint8_t pageId) {
    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            return "clock";
        case DASHBOARD_PAGE_WEATHER:
            return "weather";
        case DASHBOARD_PAGE_MARKETS:
            return "markets";
        case DASHBOARD_PAGE_HOME:
            return "home";
        case DASHBOARD_PAGE_FOCUS:
            return "focus";
        case DASHBOARD_PAGE_WORLD:
            return "world";
        case DASHBOARD_PAGE_EVENT:
            return "event";
        case DASHBOARD_PAGE_QUOTE:
            return "quote";
        case DASHBOARD_PAGE_STATUS:
            return "status";
        default:
            return "clock";
    }
}

uint32_t dashboardCurrentEpoch() {
    time_t now = time(nullptr);
    if (now < kValidEpochFloor) {
        return 0;
    }

    return static_cast<uint32_t>(now);
}

bool dashboardNightModeActive() {
    if (!dashboardConfig.nightModeEnabled || dashboardCurrentEpoch() == 0) {
        return false;
    }

    time_t now = time(nullptr);
    tm localTimeInfo;
    localtime_r(&now, &localTimeInfo);

    uint16_t currentMinutes = static_cast<uint16_t>((localTimeInfo.tm_hour * 60) + localTimeInfo.tm_min);
    uint16_t startMinutes = dashboardConfig.nightStartMinutes;
    uint16_t endMinutes = dashboardConfig.nightEndMinutes;

    if (startMinutes == endMinutes) {
        return true;
    }

    if (startMinutes < endMinutes) {
        return currentMinutes >= startMinutes && currentMinutes < endMinutes;
    }

    return currentMinutes >= startMinutes || currentMinutes < endMinutes;
}

int dashboardEffectiveBrightness(int dayBrightness) {
    int normalizedDayBrightness = constrain(dayBrightness, 0, 100);
    if (!dashboardNightModeActive()) {
        return normalizedDayBrightness;
    }

    return constrain(static_cast<int>(dashboardConfig.nightBrightness), 1, 100);
}

int32_t dashboardFocusRemainingSeconds() {
    int32_t remainingSeconds = static_cast<int32_t>(dashboardData.focus.remainingSeconds);

    if (!dashboardData.focus.running) {
        return remainingSeconds;
    }

    uint32_t now = dashboardCurrentEpoch();
    if (now == 0 || dashboardData.focus.updatedAtEpoch == 0 || now < dashboardData.focus.updatedAtEpoch) {
        return remainingSeconds;
    }

    uint32_t elapsedSeconds = now - dashboardData.focus.updatedAtEpoch;
    if (elapsedSeconds >= dashboardData.focus.remainingSeconds) {
        return 0;
    }

    return static_cast<int32_t>(dashboardData.focus.remainingSeconds - elapsedSeconds);
}

int32_t dashboardEventRemainingSeconds() {
    uint32_t now = dashboardCurrentEpoch();
    if (now == 0 || dashboardData.event.updatedAtEpoch == 0 || now < dashboardData.event.updatedAtEpoch) {
        return static_cast<int32_t>(dashboardData.event.remainingSeconds);
    }

    uint32_t elapsedSeconds = now - dashboardData.event.updatedAtEpoch;
    if (elapsedSeconds >= dashboardData.event.remainingSeconds) {
        return 0;
    }

    return static_cast<int32_t>(dashboardData.event.remainingSeconds - elapsedSeconds);
}
