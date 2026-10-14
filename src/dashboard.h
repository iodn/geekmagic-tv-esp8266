#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <Arduino.h>

#define DASHBOARD_MARKET_COUNT 3
#define DASHBOARD_WORLD_CLOCK_COUNT 2
#define DASHBOARD_CONFIG_PATH "/dashboard-config.json"
#define DASHBOARD_DATA_PATH "/dashboard-data.json"
#define DASHBOARD_CONFIG_VERSION 3

enum DashboardPageId : uint8_t {
    DASHBOARD_PAGE_CLOCK = 0,
    DASHBOARD_PAGE_WEATHER,
    DASHBOARD_PAGE_MARKETS,
    DASHBOARD_PAGE_HOME,
    DASHBOARD_PAGE_FOCUS,
    DASHBOARD_PAGE_WORLD,
    DASHBOARD_PAGE_EVENT,
    DASHBOARD_PAGE_QUOTE,
    DASHBOARD_PAGE_STATUS,
    DASHBOARD_PAGE_COUNT
};

enum DashboardThemeId : uint8_t {
    DASHBOARD_THEME_AURORA = 0,
    DASHBOARD_THEME_SUNSET,
    DASHBOARD_THEME_TERMINAL,
    DASHBOARD_THEME_NORD,
    DASHBOARD_THEME_DRACULA,
    DASHBOARD_THEME_MOCHA,
    DASHBOARD_THEME_LATTE,
    DASHBOARD_THEME_SOLARIZED_DARK,
    DASHBOARD_THEME_SOLARIZED_LIGHT,
    DASHBOARD_THEME_PAPER,
    DASHBOARD_THEME_ROSE,
    DASHBOARD_THEME_OCEAN,
    DASHBOARD_THEME_LAVENDER,
    DASHBOARD_THEME_GRAPHITE,
    DASHBOARD_THEME_COUNT
};

struct WeatherData {
    char location[24];
    char condition[24];
    int temperature;
    int high;
    int low;
    int rainChance;
};

struct MarketData {
    bool enabled;
    char symbol[12];
    char label[16];
    float price;
    float change;
    float changePercent;
};

struct FocusData {
    char label[24];
    bool running;
    bool breakMode;
    uint16_t durationMinutes;
    uint32_t remainingSeconds;
    uint32_t updatedAtEpoch;
};

struct WorldClockData {
    bool enabled;
    char label[16];
    long offsetSeconds;
};

struct EventData {
    char title[32];
    char subtitle[24];
    uint32_t remainingSeconds;
    uint32_t updatedAtEpoch;
};

struct QuoteData {
    char text[96];
    char author[24];
};

struct StatusData {
    char line1[32];
    char line2[32];
};

struct DashboardConfig {
    uint16_t version;
    uint8_t theme;
    bool customThemeEnabled;
    char customBackground[8];
    char customSurface[8];
    char customAccent[8];
    char customText[8];
    bool nightModeEnabled;
    uint16_t nightStartMinutes;
    uint16_t nightEndMinutes;
    uint8_t nightBrightness;
    bool nightThemeEnabled;
    uint8_t nightTheme;
    bool nightCustomThemeEnabled;
    char nightCustomBackground[8];
    char nightCustomSurface[8];
    char nightCustomAccent[8];
    char nightCustomText[8];
    bool rotationEnabled;
    uint16_t rotationIntervalSec;
    bool use24Hour;
    bool showSeconds;
    bool showIp;
    bool enabledPages[DASHBOARD_PAGE_COUNT];
};

struct DashboardData {
    WeatherData weather;
    MarketData markets[DASHBOARD_MARKET_COUNT];
    FocusData focus;
    WorldClockData worldClocks[DASHBOARD_WORLD_CLOCK_COUNT];
    EventData event;
    QuoteData quote;
    StatusData status;
};

extern DashboardConfig dashboardConfig;
extern DashboardData dashboardData;

void dashboardInit();
void dashboardResetToDefaults();
bool dashboardLoadConfig();
bool dashboardLoadData();
bool dashboardSaveConfig();
bool dashboardSaveData();
bool dashboardSaveAll();
bool dashboardApplyConfigJson(const String &json, String *error = nullptr);
bool dashboardApplyDataJson(const String &json, String *error = nullptr);
bool dashboardPreviewConfigJson(const String &json, String *error = nullptr);
bool dashboardPreviewDataJson(const String &json, String *error = nullptr);
void dashboardDiscardDraftChanges();
bool dashboardHasDraftChanges();
void dashboardBuildConfigJson(String &json);
void dashboardBuildDataJson(String &json);
void dashboardBuildFullJson(String &json);
bool dashboardSyncRuntimeState();
uint8_t dashboardFirstEnabledPage();
uint8_t dashboardNextEnabledPage(uint8_t currentPage);
bool dashboardPageEnabled(uint8_t pageId);
const char* dashboardPageName(uint8_t pageId);
uint32_t dashboardCurrentEpoch();
int32_t dashboardFocusRemainingSeconds();
int32_t dashboardEventRemainingSeconds();
bool dashboardNightModeActive();
int dashboardEffectiveBrightness(int dayBrightness);

#endif
