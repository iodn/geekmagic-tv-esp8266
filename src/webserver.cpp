#include "webserver.h"

#include "config.h"
#include "dashboard.h"
#include "display.h"
#include "feeds.h"
#include "auth.h"
#include "logger.h"
#include "settings.h"
#include "webui_gzip.h"
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ctype.h>

ESP8266WebServer server(WEB_SERVER_PORT);

int currentBrightness = 70;
int currentTheme = 0;

extern Settings appSettings;
extern bool otaStarted;
extern void trackWiFiReconnectAttempt(bool active);


namespace {

using SimpleHandler = void (*)();

Settings savedAppSettings = {};
bool savedSettingsReady = false;
bool factoryResetPending = false;
bool wifiReconfigurePending = false;
bool rebootPending = false;
unsigned long rebootRequestedAtMs = 0;
bool wifiScanStarted = false;
bool otaUploadComplete = false;
String otaUploadError;
String authSessionToken;
unsigned long authSessionLastSeenMs = 0;
int lastEffectiveBrightness = -1;

struct WiFiConnectTask {
    bool queued;
    bool active;
    bool waitingForIp;
    char ssid[33];
    char password[65];
    char previousSsid[33];
    char previousPassword[65];
    bool previousPersistent;
    unsigned long startedAtMs;
    unsigned long ipWaitStartedAtMs;
};

WiFiConnectTask wifiConnectTask = {};

constexpr unsigned long kWiFiConnectTimeoutMs = 20000UL;
constexpr unsigned long kWiFiIpWaitTimeoutMs = 10000UL;
constexpr unsigned long kAuthSessionDurationMs = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kAuthSessionMaxAgeSec = 12UL * 60UL * 60UL;
constexpr char kAuthSessionCookieName[] = "SCSESSID";

void prepareNoStoreHeaders() {
    // Release browser connections between API polls to leave room for TLS.
    server.keepAlive(false);
    server.sendHeader("Cache-Control", "no-store, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
}

void sendTextResponse(int code, const String &message) {
    prepareNoStoreHeaders();
    server.send(code, "text/plain", message);
}

void sendJsonResponse(int code, const String &json) {
    prepareNoStoreHeaders();
    server.send(code, "application/json", json);
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

void copyConfiguredHostname(char *buffer, size_t bufferSize) {
    settingsBuildHostname(appSettings.deviceName, buffer, bufferSize);
}

void copyActiveHostname(char *buffer, size_t bufferSize) {
    copyString(buffer, bufferSize, WiFi.hostname().c_str());
}

bool hostnameRestartRequired() {
    char hostname[SETTINGS_HOSTNAME_LENGTH];
    copyConfiguredHostname(hostname, sizeof(hostname));
    return WiFi.hostname() != hostname;
}

void queueRestart() {
    if (otaStarted) {
        ArduinoOTA.end();
        otaStarted = false;
    }
    rebootPending = true;
    rebootRequestedAtMs = millis();
}

void copyDefaultDeviceName(char *buffer, size_t bufferSize) {
    settingsDefaultDeviceName(buffer, bufferSize);
}

bool displaySettingsEqual(const Settings &left, const Settings &right) {
    return left.brightness == right.brightness &&
           left.theme == right.theme &&
           left.gmtOffset == right.gmtOffset &&
           strncmp(left.deviceName, right.deviceName, sizeof(left.deviceName)) == 0;
}

void captureSavedSettings() {
    memcpy(&savedAppSettings, &appSettings, sizeof(savedAppSettings));
    savedSettingsReady = true;
}

bool displayHasDraftChanges() {
    return (savedSettingsReady && !displaySettingsEqual(appSettings, savedAppSettings)) ||
           dashboardHasDraftChanges();
}

void applyEffectiveBrightness(bool syncPreference) {
    int dayBrightness = constrain(appSettings.brightness, 0, 100);
    int desiredBrightness = dashboardEffectiveBrightness(dayBrightness);

    if (syncPreference) {
        currentBrightness = dayBrightness;
        displaySetBrightness(dayBrightness);
        if (desiredBrightness != dayBrightness) {
            displayApplyBrightness(desiredBrightness);
        }
        lastEffectiveBrightness = desiredBrightness;
        return;
    }

    if (lastEffectiveBrightness == desiredBrightness) {
        return;
    }

    displayApplyBrightness(desiredBrightness);
    lastEffectiveBrightness = desiredBrightness;
}

void restoreSavedDisplaySettings() {
    if (!savedSettingsReady) {
        return;
    }

    appSettings.brightness = savedAppSettings.brightness;
    appSettings.gmtOffset = savedAppSettings.gmtOffset;
    appSettings.theme = savedAppSettings.theme;
    copyString(appSettings.deviceName, sizeof(appSettings.deviceName), savedAppSettings.deviceName);
    currentBrightness = appSettings.brightness;
    currentTheme = appSettings.theme;

    configTime(appSettings.gmtOffset, 0, NTP_SERVER);
    applyEffectiveBrightness(true);
}

String cookieValue(const String &cookieHeader, const char *name) {
    if (name == nullptr || name[0] == '\0') {
        return "";
    }

    String prefix = String(name) + "=";
    int headerLength = static_cast<int>(cookieHeader.length());
    int start = 0;
    while (start < headerLength) {
        while (start < headerLength &&
               (cookieHeader.charAt(start) == ' ' || cookieHeader.charAt(start) == ';')) {
            ++start;
        }

        int end = cookieHeader.indexOf(';', start);
        if (end < 0) {
            end = headerLength;
        }

        String part = cookieHeader.substring(start, end);
        part.trim();
        if (part.startsWith(prefix)) {
            return part.substring(prefix.length());
        }

        start = end + 1;
    }

    return "";
}

void clearSessionState() {
    authSessionToken = "";
    authSessionLastSeenMs = 0;
}

String generateSessionToken() {
    char buffer[33];
    for (uint8_t index = 0; index < 4; ++index) {
        uint32_t value = static_cast<uint32_t>(ESP.getCycleCount()) ^
                         static_cast<uint32_t>(micros()) ^
                         static_cast<uint32_t>(millis()) ^
                         static_cast<uint32_t>(random(0x7fffffff));
        snprintf(buffer + (index * 8), sizeof(buffer) - (index * 8), "%08lx", static_cast<unsigned long>(value));
    }
    buffer[32] = '\0';
    return String(buffer);
}

void attachSessionCookie(uint32_t maxAgeSeconds) {
    String cookie = String(kAuthSessionCookieName) + "=" + authSessionToken +
                    "; Path=/; Max-Age=" + String(maxAgeSeconds) +
                    "; HttpOnly; SameSite=Strict";
    server.sendHeader("Set-Cookie", cookie);
}

void clearSessionCookie() {
    server.sendHeader("Set-Cookie",
                      String(kAuthSessionCookieName) + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
}

void startAuthenticatedSession() {
    randomSeed(ESP.getChipId() ^ micros() ^ ESP.getCycleCount() ^ millis());
    authSessionToken = generateSessionToken();
    authSessionLastSeenMs = millis();
}

bool requestHasValidSession() {
    if (authSessionToken.length() == 0) {
        return false;
    }

    if (millis() - authSessionLastSeenMs >= kAuthSessionDurationMs) {
        clearSessionState();
        return false;
    }

    if (!server.hasHeader("Cookie")) {
        return false;
    }

    String cookie = cookieValue(server.header("Cookie"), kAuthSessionCookieName);
    if (cookie.length() == 0 || !cookie.equalsConstantTime(authSessionToken)) {
        return false;
    }

    authSessionLastSeenMs = millis();
    return true;
}

bool ensureAuthenticatedRequest() {
    if (requestHasValidSession()) {
        attachSessionCookie(kAuthSessionMaxAgeSec);
        return true;
    }

    clearSessionCookie();
    sendTextResponse(401, F("Unauthorized"));
    return false;
}

void registerProtectedRoute(const char *uri, HTTPMethod method, SimpleHandler handler) {
    server.on(uri, method, [handler]() {
        if (!ensureAuthenticatedRequest()) {
            return;
        }

        handler();
    });
}

bool parseJsonBody(JsonDocument &doc, String *error = nullptr) {
    if (!server.hasArg("plain")) {
        if (error != nullptr) {
            *error = F("Missing JSON body");
        }
        return false;
    }

    DeserializationError deserializeError = deserializeJson(doc, server.arg("plain"));
    if (deserializeError) {
        if (error != nullptr) {
            *error = deserializeError.c_str();
        }
        return false;
    }

    if (error != nullptr) {
        *error = "";
    }
    return true;
}











bool applyRuntimeSettingsObject(JsonObjectConst settings, bool persist, String *error = nullptr) {
    bool changed = false;
    bool brightnessChanged = false;
    bool timezoneChanged = false;

    if (!settings["brightness"].isNull()) {
        currentBrightness = constrain(settings["brightness"].as<int>(), 0, 100);
        appSettings.brightness = currentBrightness;
        changed = true;
        brightnessChanged = true;
    }

    if (!settings["gmtOffset"].isNull()) {
        long gmtOffset = constrain(settings["gmtOffset"].as<long>(), -12L * 3600L, 14L * 3600L);
        if (appSettings.gmtOffset != gmtOffset) {
            appSettings.gmtOffset = gmtOffset;
            configTime(appSettings.gmtOffset, 0, NTP_SERVER);
            changed = true;
            timezoneChanged = true;
        }
    }

    if (!settings["deviceName"].isNull()) {
        char nextDeviceName[SETTINGS_DEVICE_NAME_LENGTH];
        settingsSanitizeDeviceName(settings["deviceName"].as<const char*>(), nextDeviceName, sizeof(nextDeviceName));
        if (strncmp(appSettings.deviceName, nextDeviceName, sizeof(appSettings.deviceName)) != 0) {
            copyString(appSettings.deviceName, sizeof(appSettings.deviceName), nextDeviceName);
            changed = true;
        }
    }

    if (brightnessChanged || timezoneChanged) {
        applyEffectiveBrightness(brightnessChanged);
    }

    if (persist && (changed || displayHasDraftChanges())) {
        if (!settingsSave(appSettings)) {
            if (error != nullptr) *error = F("Failed to save settings");
            return false;
        }
        captureSavedSettings();
    }

    if (error != nullptr) {
        *error = "";
    }

    return true;
}

bool applyDashboardPayload(JsonDocument &doc, bool persist, String *error = nullptr) {
    JsonObjectConst settings = doc["settings"].as<JsonObjectConst>();
    if (!settings.isNull() && !applyRuntimeSettingsObject(settings, false, error)) {
        return false;
    }

    JsonVariantConst config = doc["config"];
    if (!config.isNull()) {
        String configJson;
        serializeJson(config, configJson);
        if (!dashboardPreviewConfigJson(configJson, error)) {
            return false;
        }
    }

    JsonVariantConst data = doc["data"];
    if (!data.isNull()) {
        String dataJson;
        serializeJson(data, dataJson);
        if (!dashboardPreviewDataJson(dataJson, error)) {
            return false;
        }
    }

    currentTheme = dashboardConfig.theme;
    appSettings.theme = currentTheme;
    if (!dashboardPageEnabled(displayState.currentPage)) {
        displayState.currentPage = dashboardFirstEnabledPage();
    }

    if (persist) {
        if (!dashboardSaveAll()) {
            if (error != nullptr) {
                *error = F("Failed to save dashboard");
            }
            return false;
        }
        if (!settingsSave(appSettings)) {
            if (error != nullptr) *error = F("Failed to save settings");
            return false;
        }
        captureSavedSettings();
    }

    applyEffectiveBrightness(false);

    if (error != nullptr) {
        *error = "";
    }
    return true;
}

bool applyFeedPayload(JsonDocument &doc, bool persist, String *error = nullptr) {
    String configJson;
    serializeJson(doc.as<JsonVariantConst>(), configJson);
    if (!feedsPreviewConfigJson(configJson, error)) {
        return false;
    }

    if (persist && !feedsSaveConfig()) {
        if (error != nullptr) {
            *error = F("Failed to save feed config");
        }
        return false;
    }

    if (error != nullptr) {
        *error = "";
    }
    return true;
}

void performFactoryResetNow() {
    logPrint(F("Performing factory reset..."));
    displayShowMessage(F("Factory reset\nRestarting..."));

    WiFi.disconnect(true);
    delay(100);
    ESP.eraseConfig();
    delay(100);

    settingsInit();
    Settings defaultSettings;
    settingsReset(defaultSettings);
    settingsSave(defaultSettings);
    bootCounterReset();

    LittleFS.format();
    delay(500);
    ESP.restart();
}

void performWiFiReconfigureNow() {
    logPrint(F("Resetting WiFi credentials and restarting to AP mode..."));
    displayShowMessage(F("WiFi reset\nRestarting..."));

    WiFi.disconnect(true);
    delay(100);
    ESP.eraseConfig();
    delay(200);
    ESP.restart();
}

void startQueuedWiFiConnect() {
    if (!wifiConnectTask.queued) {
        return;
    }

    wifiConnectTask.queued = false;
    wifiConnectTask.active = true;
    wifiConnectTask.waitingForIp = false;
    wifiConnectTask.startedAtMs = millis();
    wifiConnectTask.ipWaitStartedAtMs = 0;

    logPrintf(F("Starting WiFi connect task for SSID: %s"), wifiConnectTask.ssid);
    displayState.apMode = false;
    wifiFailsafeMode = false;
    displayShowMessage(String("Connecting\n") + wifiConnectTask.ssid);

    copyString(wifiConnectTask.previousSsid, sizeof(wifiConnectTask.previousSsid), WiFi.SSID().c_str());
    copyString(wifiConnectTask.previousPassword, sizeof(wifiConnectTask.previousPassword), WiFi.psk().c_str());
    wifiConnectTask.previousPersistent = WiFi.getPersistent();
    trackWiFiReconnectAttempt(false);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());
    char hostname[SETTINGS_HOSTNAME_LENGTH];
    copyConfiguredHostname(hostname, sizeof(hostname));
    WiFi.hostname(hostname);
    WiFi.begin(wifiConnectTask.ssid, wifiConnectTask.password);
}

void failWiFiConnectTask(const __FlashStringHelper *reason) {
    logPrint(String(F("WiFi connect failed: ")) + String(reason));
    wifiConnectTask.active = false;
    wifiConnectTask.waitingForIp = false;

    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP_STA);
    if (wifiConnectTask.previousSsid[0] != '\0') {
        WiFi.begin(wifiConnectTask.previousSsid, wifiConnectTask.previousPassword);
    } else {
        // Clear only the failed candidate in RAM; persisted credentials are untouched.
        WiFi.disconnect(false, true);
    }
    trackWiFiReconnectAttempt(wifiConnectTask.previousSsid[0] != '\0');
    WiFi.persistent(wifiConnectTask.previousPersistent);
    memset(wifiConnectTask.password, 0, sizeof(wifiConnectTask.password));
    memset(wifiConnectTask.previousPassword, 0, sizeof(wifiConnectTask.previousPassword));
    WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());
    wifiFailsafeMode = true;
    displayShowAPScreen(WIFI_AP_NAME, apPassword.c_str(), WiFi.softAPIP().toString().c_str());
}

void processWiFiConnectTask() {
    if (wifiConnectTask.queued) {
        startQueuedWiFiConnect();
        return;
    }

    if (!wifiConnectTask.active) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
            wifiConnectTask.active = false;
            wifiConnectTask.waitingForIp = false;
            WiFi.persistent(true);
            WiFi.begin(wifiConnectTask.ssid, wifiConnectTask.password);
            WiFi.persistent(wifiConnectTask.previousPersistent);
            memset(wifiConnectTask.password, 0, sizeof(wifiConnectTask.password));
            memset(wifiConnectTask.previousPassword, 0, sizeof(wifiConnectTask.previousPassword));
            displayShowMessage("WiFi OK\n" + WiFi.localIP().toString());
            queueRestart();
            return;
        }

        if (!wifiConnectTask.waitingForIp) {
            wifiConnectTask.waitingForIp = true;
            wifiConnectTask.ipWaitStartedAtMs = millis();
        } else if (millis() - wifiConnectTask.ipWaitStartedAtMs > kWiFiIpWaitTimeoutMs) {
            failWiFiConnectTask(F("IP timeout"));
        }
        return;
    }

    if (millis() - wifiConnectTask.startedAtMs > kWiFiConnectTimeoutMs) {
        failWiFiConnectTask(F("association timeout"));
    }
}

}  // namespace

void webserverApplyEffectiveBrightness(bool syncPreference) {
    applyEffectiveBrightness(syncPreference);
}

void handleAppJson() {
    char hostName[SETTINGS_HOSTNAME_LENGTH];
    char defaultDeviceName[SETTINGS_DEVICE_NAME_LENGTH];
    copyActiveHostname(hostName, sizeof(hostName));
    copyDefaultDeviceName(defaultDeviceName, sizeof(defaultDeviceName));

    String json = "{";
    json += "\"deviceName\":\"" + String(appSettings.deviceName) + "\",";
    json += "\"defaultDeviceName\":\"" + String(defaultDeviceName) + "\",";
    json += "\"hostName\":\"" + String(hostName) + "\",";
    char configuredHostName[SETTINGS_HOSTNAME_LENGTH];
    copyConfiguredHostname(configuredHostName, sizeof(configuredHostName));
    json += "\"configuredHostName\":\"" + String(configuredHostName) + "\",";
    json += "\"rebootRequired\":" + String(hostnameRestartRequired() ? "true" : "false") + ",";
    json += "\"theme\":" + String(currentTheme) + ",";
    json += "\"brt\":" + String(currentBrightness) + ",";
    json += "\"gmtOffset\":" + String(appSettings.gmtOffset) + ",";
    json += "\"displayDraft\":" + String(displayHasDraftChanges() ? "true" : "false") + ",";
    json += "\"networkBusy\":" + String(webserverHasPendingNetworkAction() ? "true" : "false");
    json += "}";
    sendJsonResponse(200, json);
}

void handleDashboardJson() {
    String json;
    dashboardBuildFullJson(json);
    sendJsonResponse(200, json);
}

void handleFeedsJson() {
    String json;
    feedsBuildStateJson(json);
    sendJsonResponse(200, json);
}

void handleSpaceJson() {
    FSInfo fsInfo;
    LittleFS.info(fsInfo);

    String json = "{";
    json += "\"total\":" + String(fsInfo.totalBytes) + ",";
    json += "\"free\":" + String(fsInfo.totalBytes - fsInfo.usedBytes);
    json += "}";
    sendJsonResponse(200, json);
}

void handleBrtJson() {
    sendJsonResponse(200, "{\"brt\":\"" + String(currentBrightness) + "\"}");
}

void handleVersionJson() {
    char hostName[SETTINGS_HOSTNAME_LENGTH];
    copyActiveHostname(hostName, sizeof(hostName));

    String json = "{";
    json += "\"version\":\"" + String(FIRMWARE_VERSION_STRING) + "\",";
    json += "\"deviceName\":\"" + String(appSettings.deviceName) + "\",";
    json += "\"hostName\":\"" + String(hostName) + "\"";
    json += "}";
    sendJsonResponse(200, json);
}

void handleAuthStatus() {
    char hostName[SETTINGS_HOSTNAME_LENGTH];
    char defaultDeviceName[SETTINGS_DEVICE_NAME_LENGTH];
    copyActiveHostname(hostName, sizeof(hostName));
    copyDefaultDeviceName(defaultDeviceName, sizeof(defaultDeviceName));

    bool authenticated = requestHasValidSession();
    if (authenticated) {
        attachSessionCookie(kAuthSessionMaxAgeSec);
    } else if (server.hasHeader("Cookie")) {
        clearSessionCookie();
    }

    String json = "{";
    json += "\"enabled\":true,";
    json += "\"authenticated\":" + String(authenticated ? "true" : "false") + ",";
    json += "\"deviceName\":\"" + String(appSettings.deviceName) + "\",";
    json += "\"defaultDeviceName\":\"" + String(defaultDeviceName) + "\",";
    json += "\"hostName\":\"" + String(hostName) + "\",";
    json += "\"username\":\"" + String(authUsername()) + "\",";
    json += "\"provisionedThisBoot\":" + String(authWasProvisionedThisBoot() ? "true" : "false") + ",";
    json += "\"canRevealPassword\":" + String(authCanRevealPassword() ? "true" : "false");
    json += "}";
    sendJsonResponse(200, json);
}

void handleAuthLogin() {
    JsonDocument doc;
    String error;
    if (!parseJsonBody(doc, &error)) {
        sendTextResponse(400, error);
        return;
    }

    const char *username = doc["username"] | authUsername();
    const char *password = doc["password"] | "";
    if (username == nullptr || String(username) != authUsername()) {
        clearSessionCookie();
        sendTextResponse(401, F("Unauthorized"));
        return;
    }

    if (!authVerifyPassword(String(password))) {
        clearSessionCookie();
        sendTextResponse(401, F("Invalid password"));
        return;
    }

    startAuthenticatedSession();
    attachSessionCookie(kAuthSessionMaxAgeSec);
    sendTextResponse(200, F("OK"));
}

void handleAuthLogout() {
    if (requestHasValidSession()) {
        clearSessionState();
    }
    clearSessionCookie();
    sendTextResponse(200, F("OK"));
}

void handleAuthReveal() {
    if (!authCanRevealPassword()) {
        sendTextResponse(409,
                         F("Custom or older passwords cannot be displayed. If you forgot it, factory reset with 5 quick power cycles."));
        return;
    }

    const char *password = authProvisionedPassword();
    if (password == nullptr || password[0] == '\0') {
        sendTextResponse(409, F("No revealable setup password is available."));
        return;
    }

    displayShowTemporaryMessage(String(F("Admin login\n")) + authUsername() + "\n" + password, 15000UL);
    sendTextResponse(200, F("Password shown on device for 15 seconds."));
}

void handleAuthPasswordChange() {
    JsonDocument doc;
    String error;
    if (!parseJsonBody(doc, &error)) {
        sendTextResponse(400, error);
        return;
    }

    const char *currentPassword = doc["currentPassword"] | "";
    const char *newPassword = doc["newPassword"] | "";
    if (!authUpdatePassword(String(currentPassword), String(newPassword), &error)) {
        sendTextResponse(400, error.length() > 0 ? error : "Password update failed");
        return;
    }

    clearSessionState();
    clearSessionCookie();
    queueRestart();
    sendTextResponse(200, F("Password updated. Device restarting."));
}

void handleDashboardConfigSave() {
    if (!server.hasArg("plain")) {
        sendTextResponse(400, F("Missing JSON body"));
        return;
    }

    String error;
    if (!dashboardApplyConfigJson(server.arg("plain"), &error)) {
        sendTextResponse(400, "Invalid config: " + error);
        return;
    }

    currentTheme = dashboardConfig.theme;
    if (appSettings.theme != currentTheme || displayHasDraftChanges()) {
        appSettings.theme = currentTheme;
        if (!settingsSave(appSettings)) {
            sendTextResponse(500, F("Failed to save settings"));
            return;
        }
        captureSavedSettings();
    }
    applyEffectiveBrightness(false);
    displayState.currentPage = dashboardFirstEnabledPage();
    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleDashboardDataSave() {
    if (!server.hasArg("plain")) {
        sendTextResponse(400, F("Missing JSON body"));
        return;
    }

    String error;
    if (!dashboardApplyDataJson(server.arg("plain"), &error)) {
        sendTextResponse(400, "Invalid data: " + error);
        return;
    }

    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleDashboardReset() {
    dashboardResetToDefaults();
    if (!dashboardSaveAll()) {
        sendTextResponse(500, F("Failed to reset dashboard"));
        return;
    }

    currentTheme = dashboardConfig.theme;
    if (appSettings.theme != currentTheme || displayHasDraftChanges()) {
        appSettings.theme = currentTheme;
        if (!settingsSave(appSettings)) {
            sendTextResponse(500, F("Failed to save settings"));
            return;
        }
        captureSavedSettings();
    }
    applyEffectiveBrightness(false);
    displayState.currentPage = dashboardFirstEnabledPage();
    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleDashboardLive() {
    JsonDocument doc;
    String error;
    if (!parseJsonBody(doc, &error)) {
        sendTextResponse(400, error);
        return;
    }

    if (!applyDashboardPayload(doc, false, &error)) {
        sendTextResponse(400, error);
        return;
    }

    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleDashboardSave() {
    String error;
    if (server.hasArg("plain")) {
        JsonDocument doc;
        if (!parseJsonBody(doc, &error)) {
            sendTextResponse(400, error);
            return;
        }

        if (!applyDashboardPayload(doc, true, &error)) {
            sendTextResponse(500, error);
            return;
        }
    } else {
        appSettings.theme = dashboardConfig.theme;
        if (!dashboardSaveAll()) {
            sendTextResponse(500, F("Failed to save dashboard"));
            return;
        }
        if (!settingsSave(appSettings)) {
            sendTextResponse(500, F("Failed to save settings"));
            return;
        }
        captureSavedSettings();
        applyEffectiveBrightness(false);
    }

    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleDashboardDiscard() {
    dashboardDiscardDraftChanges();
    restoreSavedDisplaySettings();
    currentTheme = dashboardConfig.theme;
    appSettings.theme = currentTheme;
    if (!dashboardPageEnabled(displayState.currentPage)) {
        displayState.currentPage = dashboardFirstEnabledPage();
    }
    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleFeedsLive() {
    if (feedsBusy() || webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network request is already running"));
        return;
    }
    JsonDocument doc;
    String error;
    if (!parseJsonBody(doc, &error)) {
        sendTextResponse(400, error);
        return;
    }

    if (!applyFeedPayload(doc, false, &error)) {
        sendTextResponse(400, error);
        return;
    }

    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleFeedsSave() {
    if (feedsBusy() || webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network request is already running"));
        return;
    }
    String error;
    if (server.hasArg("plain")) {
        JsonDocument doc;
        if (!parseJsonBody(doc, &error)) {
            sendTextResponse(400, error);
            return;
        }

        if (!applyFeedPayload(doc, true, &error)) {
            sendTextResponse(500, error);
            return;
        }
    } else if (!feedsSaveConfig()) {
        sendTextResponse(500, F("Failed to save feed config"));
        return;
    }

    sendTextResponse(200, F("OK"));
}

void handleFeedsDiscard() {
    if (feedsBusy() || webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network request is already running"));
        return;
    }
    feedsDiscardDraftChanges();
    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleFeedsReset() {
    if (feedsBusy() || webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network request is already running"));
        return;
    }
    feedsResetToDefaults();
    if (!feedsSaveConfig()) {
        sendTextResponse(500, F("Failed to reset feed config"));
        return;
    }

    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleFeedsSync() {
    if (feedsBusy() || webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network request is already running"));
        return;
    }
    String scope = server.hasArg("scope") ? server.arg("scope") : "all";
    String error;
    if (!feedsSyncNow(scope.c_str(), &error)) {
        sendTextResponse(400, error.length() > 0 ? error : "Feed sync rejected");
        return;
    }
    sendJsonResponse(202, "{\"queued\":true}");
}

void handleFeedJob() {
    if (feedsBusy()) {
        sendJsonResponse(202, "{\"pending\":true}");
        return;
    }
    String json;
    feedsBuildStateJson(json);
    sendJsonResponse(200, json);
}

void handleFeedSearch() {
    String error;
    if (server.hasArg("query")) {
        if (feedsBusy() || webserverHasPendingNetworkAction()) {
            sendTextResponse(409, F("Another network request is already running"));
            return;
        }
        if (!feedsQueueWeatherSearch(server.arg("query"), &error)) {
            sendTextResponse(400, error);
            return;
        }
        sendJsonResponse(202, "{\"pending\":true}");
        return;
    }
    String json;
    FeedSearchStatus status = feedsWeatherSearchResult(json, &error);
    if (status == FEED_SEARCH_PENDING) {
        sendJsonResponse(202, "{\"pending\":true}");
    } else if (status == FEED_SEARCH_READY) {
        sendJsonResponse(200, json);
    } else {
        sendTextResponse(status == FEED_SEARCH_IDLE ? 409 : 502,
                         error.length() > 0 ? error : "No search queued");
    }
}









void handleApiUpdate() {
    if (!server.hasArg("plain")) {
        sendTextResponse(400, F("No JSON body"));
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        sendTextResponse(400, F("Invalid JSON"));
        return;
    }

    if (!doc["line1"].isNull() && !doc["line1"].is<const char*>()) {
        sendTextResponse(400, F("line1 must be a string"));
        return;
    }
    if (!doc["line1"].isNull()) {
        strncpy(displayState.line2, doc["line1"].as<const char*>(), sizeof(displayState.line2) - 1);
        displayState.line2[sizeof(displayState.line2) - 1] = '\0';
    } else {
        displayState.line2[0] = '\0';
    }

    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleTestCard() {
    displayState.apMode = false;
    displayState.line2[0] = '\0';
    displayState.currentPage = DASHBOARD_PAGE_CLOCK;
    displayUpdate();
    sendTextResponse(200, F("OK"));
}

void handleReconfigureWiFi() {
    if (webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network task is already running"));
        return;
    }

    wifiReconfigurePending = true;
    sendTextResponse(202, F("WiFi credentials will be cleared. Device restarting to AP mode."));
}

void handleFactoryReset() {
    factoryResetPending = true;
    sendTextResponse(202, F("Factory reset requested. Device will restart shortly."));
}

void handleOTAForm() {
    prepareNoStoreHeaders();
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, PSTR("text/html"), reinterpret_cast<PGM_P>(ota_html_gzip), sizeof(ota_html_gzip));
}

void handleOTAUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        otaUploadComplete = false;
        otaUploadError = "";
        if (feedsBusy() || webserverHasPendingNetworkAction()) {
            otaUploadError = "Another network request is running";
            return;
        }
        displayShowMessage(F("OTA Update..."));
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) {
            otaUploadError = "Firmware update could not start";
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE && otaUploadError.isEmpty()) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaUploadError = "Incomplete firmware write";
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaUploadComplete = false;
        otaUploadError = "Firmware upload aborted";
        if (Update.isRunning()) Update.end();
    } else if (upload.status == UPLOAD_FILE_END && otaUploadError.isEmpty()) {
        if (Update.end(true)) {
            otaUploadComplete = true;
            displayShowMessage(F("OTA complete"));
        } else {
            Update.printError(Serial);
            displayShowMessage(F("OTA failed"));
        }
    }
}

void handleOTADone() {
    if (!otaUploadComplete && Update.isRunning()) {
        Update.end();
    }
    bool shouldReboot = otaUploadComplete && !Update.hasError() && otaUploadError.isEmpty();
    sendTextResponse(shouldReboot ? 200 : 409,
                     shouldReboot ? "OK - Rebooting..." :
                     (otaUploadError.length() ? otaUploadError : String("Incomplete firmware upload")));

    if (shouldReboot) {
        queueRestart();
    }
}

void handleLog() {
    sendTextResponse(200, logGetAll());
}

void handleWiFiScan() {
    if (server.hasArg("start") && server.arg("start") == "1") {
        if (webserverHasPendingNetworkAction() || feedsBusy()) {
            sendTextResponse(409, F("Another network task is running"));
            return;
        }
        WiFi.scanDelete();
        int result = WiFi.scanNetworks(true, true);
        if (result == WIFI_SCAN_FAILED) {
            sendTextResponse(502, F("WiFi scan could not start"));
            return;
        }
        wifiScanStarted = true;
    }
    if (!wifiScanStarted) {
        sendTextResponse(409, F("No scan queued"));
        return;
    }
    int networkCount = WiFi.scanComplete();
    if (networkCount == WIFI_SCAN_RUNNING) {
        sendJsonResponse(202, "{\"pending\":true}");
        return;
    }
    if (networkCount < 0) {
        wifiScanStarted = false;
        sendTextResponse(502, F("WiFi scan failed"));
        return;
    }
    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();
    for (int index = 0; index < networkCount; ++index) {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(index);
        network["rssi"] = WiFi.RSSI(index);
        network["encryption"] = WiFi.encryptionType(index);
    }
    String json;
    if (doc.overflowed()) {
        sendTextResponse(503, F("Scan results exceed available memory"));
    } else {
        serializeJson(doc, json);
        sendJsonResponse(200, json);
    }
    WiFi.scanDelete();
    wifiScanStarted = false;
}

void handleWiFiConnect() {
    if (webserverHasPendingNetworkAction()) {
        sendTextResponse(409, F("Another network task is already running"));
        return;
    }

    JsonDocument doc;
    String error;
    if (!parseJsonBody(doc, &error)) {
        sendTextResponse(400, error);
        return;
    }

    const char *ssid = doc["ssid"];
    const char *password = doc["password"] | "";
    if (ssid == nullptr || ssid[0] == '\0' || strlen(ssid) >= sizeof(wifiConnectTask.ssid) ||
        strlen(password) >= sizeof(wifiConnectTask.password)) {
        sendTextResponse(400, F("Missing SSID"));
        return;
    }

    copyString(wifiConnectTask.ssid, sizeof(wifiConnectTask.ssid), ssid);
    copyString(wifiConnectTask.password, sizeof(wifiConnectTask.password), password);
    wifiConnectTask.queued = true;
    sendTextResponse(202, F("Connection started. Device will restart if successful."));
}

void handleRoot() {
    if (requestHasValidSession()) {
        attachSessionCookie(kAuthSessionMaxAgeSec);
    } else if (server.hasHeader("Cookie")) {
        clearSessionCookie();
    }

    prepareNoStoreHeaders();
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, PSTR("text/html"), reinterpret_cast<PGM_P>(index_html_gzip), sizeof(index_html_gzip));
}

void webserverInit() {
    static const char* kCollectedHeaders[] = {"Cookie"};

    currentTheme = dashboardConfig.theme;
    appSettings.theme = currentTheme;
    captureSavedSettings();
    server.collectHeaders(kCollectedHeaders, sizeof(kCollectedHeaders) / sizeof(kCollectedHeaders[0]));

    server.on("/", HTTP_GET, handleRoot);
    server.on("/auth/status", HTTP_GET, handleAuthStatus);
    server.on("/auth/login", HTTP_POST, handleAuthLogin);
    server.on("/auth/logout", HTTP_POST, handleAuthLogout);
    server.on("/auth/reveal", HTTP_POST, handleAuthReveal);

    registerProtectedRoute("/app.json", HTTP_GET, handleAppJson);
    registerProtectedRoute("/dashboard.json", HTTP_GET, handleDashboardJson);
    registerProtectedRoute("/feeds.json", HTTP_GET, handleFeedsJson);
    registerProtectedRoute("/space.json", HTTP_GET, handleSpaceJson);
    registerProtectedRoute("/brt.json", HTTP_GET, handleBrtJson);
    registerProtectedRoute("/version.json", HTTP_GET, handleVersionJson);
    registerProtectedRoute("/log", HTTP_GET, handleLog);
    registerProtectedRoute("/reconfigurewifi", HTTP_POST, handleReconfigureWiFi);
    registerProtectedRoute("/factoryreset", HTTP_POST, handleFactoryReset);
    registerProtectedRoute("/scan", HTTP_GET, handleWiFiScan);
    registerProtectedRoute("/connect", HTTP_POST, handleWiFiConnect);
    registerProtectedRoute("/test", HTTP_POST, handleTestCard);
    registerProtectedRoute("/dashboard/config", HTTP_POST, handleDashboardConfigSave);
    registerProtectedRoute("/dashboard/data", HTTP_POST, handleDashboardDataSave);
    registerProtectedRoute("/dashboard/live", HTTP_POST, handleDashboardLive);
    registerProtectedRoute("/dashboard/save", HTTP_POST, handleDashboardSave);
    registerProtectedRoute("/dashboard/discard", HTTP_POST, handleDashboardDiscard);
    registerProtectedRoute("/dashboard/reset", HTTP_POST, handleDashboardReset);
    registerProtectedRoute("/feeds/live", HTTP_POST, handleFeedsLive);
    registerProtectedRoute("/feeds/save", HTTP_POST, handleFeedsSave);
    registerProtectedRoute("/feeds/discard", HTTP_POST, handleFeedsDiscard);
    registerProtectedRoute("/feeds/reset", HTTP_POST, handleFeedsReset);
    registerProtectedRoute("/feeds/sync", HTTP_POST, handleFeedsSync);
    registerProtectedRoute("/feeds/job", HTTP_GET, handleFeedJob);
    registerProtectedRoute("/feeds/search", HTTP_GET, handleFeedSearch);
    registerProtectedRoute("/api/update", HTTP_POST, handleApiUpdate);
    registerProtectedRoute("/api/dashboard", HTTP_POST, handleDashboardDataSave);
    registerProtectedRoute("/auth/password", HTTP_POST, handleAuthPasswordChange);



    server.on("/update", HTTP_GET, []() {
        if (!ensureAuthenticatedRequest()) {
            return;
        }
        handleOTAForm();
    });
    server.on("/update", HTTP_POST,
              []() {
                  if (!ensureAuthenticatedRequest()) {
                      return;
                  }
                  handleOTADone();
              },
              []() {
                  if (!requestHasValidSession()) {
                      return;
                  }
                  handleOTAUpload();
              });

    server.onNotFound([]() {
        if (wifiFailsafeMode) {
            server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
            server.send(302, "text/plain", "Open setup dashboard");
        } else {
            sendTextResponse(404, F("Not found"));
        }
    });

    server.begin();
    Serial.println(F("Web server started"));
}

void webserverHandle() {
    server.handleClient();
}

void webserverProcessPendingActions() {
    if (webserverHasPendingNetworkAction() && feedsBusy()) {
        feedsCancelJobs();
        return;
    }
    if (rebootPending) {
        if (millis() - rebootRequestedAtMs >= 1000UL) {
            ESP.restart();
        }
        return;
    }
    if (factoryResetPending) {
        factoryResetPending = false;
        performFactoryResetNow();
        return;
    }

    if (wifiReconfigurePending) {
        wifiReconfigurePending = false;
        performWiFiReconfigureNow();
        return;
    }

    processWiFiConnectTask();
}

bool webserverHasPendingNetworkAction() {
    return (wifiScanStarted && WiFi.scanComplete() == WIFI_SCAN_RUNNING) ||
           rebootPending ||
           factoryResetPending ||
           wifiReconfigurePending ||
           wifiConnectTask.queued ||
           wifiConnectTask.active;
}
