#include "webserver.h"

#include "config.h"
#include "dashboard.h"
#include "display.h"
#include "feeds.h"
#include "auth.h"
#include "logger.h"
#include "settings.h"
#include "webui.h"
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
char currentImage[DISPLAY_PATH_BUFFER_SIZE];

extern Settings appSettings;
extern bool otaStarted;

File uploadFile;

namespace {

using SimpleHandler = void (*)();

Settings savedAppSettings = {};
bool savedSettingsReady = false;
bool factoryResetPending = false;
bool wifiReconfigurePending = false;
bool rebootPending = false;
unsigned long rebootRequestedAtMs = 0;
String uploadErrorMessage;
int uploadStatusCode = 200;
char uploadTargetPath[DISPLAY_PATH_BUFFER_SIZE] = {0};
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
    sendTextResponse(401, "Unauthorized");
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
            *error = "Missing JSON body";
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

bool imageExtensionAllowed(const String &filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

bool sanitizeImagePath(const String &input, String &normalizedPath, String *error = nullptr) {
    String candidate = input;
    candidate.trim();

    if (candidate.length() == 0) {
        normalizedPath = "";
        return true;
    }

    if (!candidate.startsWith(IMAGE_DIR) ||
        candidate.indexOf("..") >= 0 ||
        candidate.indexOf('\\') >= 0 ||
        !imageExtensionAllowed(candidate)) {
        if (error != nullptr) {
            *error = "Invalid image path";
        }
        return false;
    }

    normalizedPath = candidate;
    return true;
}

bool sanitizeUploadFilename(const String &input, String &safeFilename, String *error = nullptr) {
    String filename = input;
    filename.replace("\\", "/");
    int separatorIndex = filename.lastIndexOf('/');
    if (separatorIndex >= 0) {
        filename = filename.substring(separatorIndex + 1);
    }
    filename.trim();

    String sanitized = "";
    for (size_t index = 0; index < filename.length(); ++index) {
        char value = filename[index];
        if (isalnum(static_cast<unsigned char>(value)) || value == '-' || value == '_' || value == '.') {
            sanitized += value;
        } else if (value == ' ') {
            sanitized += '-';
        }
    }

    if (sanitized.length() == 0 || sanitized[0] == '.' || !imageExtensionAllowed(sanitized)) {
        if (error != nullptr) {
            *error = "Invalid JPEG filename";
        }
        return false;
    }

    safeFilename = sanitized;
    return true;
}

void clearCurrentImageSelection() {
    currentImage[0] = '\0';
    displayState.imagePath[0] = '\0';
    displayState.showImage = false;
}

bool setCurrentImageSelection(const String &requestedPath, String *error = nullptr) {
    String imagePath;
    if (!sanitizeImagePath(requestedPath, imagePath, error)) {
        return false;
    }

    if (imagePath.length() > 0 && !LittleFS.exists(imagePath)) {
        if (error != nullptr) {
            *error = "Image not found";
        }
        return false;
    }

    if (imagePath.length() == 0) {
        clearCurrentImageSelection();
        return true;
    }

    strncpy(currentImage, imagePath.c_str(), sizeof(currentImage) - 1);
    currentImage[sizeof(currentImage) - 1] = '\0';
    strncpy(displayState.imagePath, currentImage, sizeof(displayState.imagePath) - 1);
    displayState.imagePath[sizeof(displayState.imagePath) - 1] = '\0';
    displayState.showImage = true;
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

    if (persist && changed) {
        settingsSave(appSettings);
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
                *error = "Failed to save dashboard";
            }
            return false;
        }
        settingsSave(appSettings);
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
            *error = "Failed to save feed config";
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

    logPrintf("Starting WiFi connect task for SSID: %s", wifiConnectTask.ssid);
    displayState.showImage = false;
    displayState.apMode = false;
    wifiFailsafeMode = false;
    displayShowMessage(String("Connecting\n") + wifiConnectTask.ssid);

    copyString(wifiConnectTask.previousSsid, sizeof(wifiConnectTask.previousSsid), WiFi.SSID().c_str());
    copyString(wifiConnectTask.previousPassword, sizeof(wifiConnectTask.previousPassword), WiFi.psk().c_str());
    wifiConnectTask.previousPersistent = WiFi.getPersistent();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    delay(100);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());
    delay(100);
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
            delay(1200);
            ESP.restart();
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
    json += "\"img\":\"" + String(currentImage) + "\",";
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
        sendTextResponse(401, "Unauthorized");
        return;
    }

    if (!authVerifyPassword(String(password))) {
        clearSessionCookie();
        sendTextResponse(401, "Invalid password");
        return;
    }

    startAuthenticatedSession();
    attachSessionCookie(kAuthSessionMaxAgeSec);
    sendTextResponse(200, "OK");
}

void handleAuthLogout() {
    if (requestHasValidSession()) {
        clearSessionState();
    }
    clearSessionCookie();
    sendTextResponse(200, "OK");
}

void handleAuthReveal() {
    if (!authCanRevealPassword()) {
        sendTextResponse(409,
                         "Custom or older passwords cannot be displayed. If you forgot it, factory reset with 5 quick power cycles.");
        return;
    }

    const char *password = authProvisionedPassword();
    if (password == nullptr || password[0] == '\0') {
        sendTextResponse(409, "No revealable setup password is available.");
        return;
    }

    displayShowTemporaryMessage(String(F("Admin login\n")) + authUsername() + "\n" + password, 15000UL);
    sendTextResponse(200, "Password shown on device for 15 seconds.");
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
    sendTextResponse(200, "Password updated. Device restarting.");
}

void handleDashboardConfigSave() {
    if (!server.hasArg("plain")) {
        sendTextResponse(400, "Missing JSON body");
        return;
    }

    String error;
    if (!dashboardApplyConfigJson(server.arg("plain"), &error)) {
        sendTextResponse(400, "Invalid config: " + error);
        return;
    }

    currentTheme = dashboardConfig.theme;
    if (appSettings.theme != currentTheme) {
        appSettings.theme = currentTheme;
        settingsSave(appSettings);
        captureSavedSettings();
    }
    applyEffectiveBrightness(false);
    displayState.currentPage = dashboardFirstEnabledPage();
    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleDashboardDataSave() {
    if (!server.hasArg("plain")) {
        sendTextResponse(400, "Missing JSON body");
        return;
    }

    String error;
    if (!dashboardApplyDataJson(server.arg("plain"), &error)) {
        sendTextResponse(400, "Invalid data: " + error);
        return;
    }

    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleDashboardReset() {
    dashboardResetToDefaults();
    if (!dashboardSaveAll()) {
        sendTextResponse(500, "Failed to reset dashboard");
        return;
    }

    currentTheme = dashboardConfig.theme;
    if (appSettings.theme != currentTheme) {
        appSettings.theme = currentTheme;
        settingsSave(appSettings);
        captureSavedSettings();
    }
    applyEffectiveBrightness(false);
    displayState.currentPage = dashboardFirstEnabledPage();
    displayUpdate();
    sendTextResponse(200, "OK");
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
    sendTextResponse(200, "OK");
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
            sendTextResponse(500, "Failed to save dashboard");
            return;
        }
        settingsSave(appSettings);
        captureSavedSettings();
        applyEffectiveBrightness(false);
    }

    displayUpdate();
    sendTextResponse(200, "OK");
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
    sendTextResponse(200, "OK");
}

void handleFeedsLive() {
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
    sendTextResponse(200, "OK");
}

void handleFeedsSave() {
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
        sendTextResponse(500, "Failed to save feed config");
        return;
    }

    sendTextResponse(200, "OK");
}

void handleFeedsDiscard() {
    feedsDiscardDraftChanges();
    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleFeedsReset() {
    feedsResetToDefaults();
    if (!feedsSaveConfig()) {
        sendTextResponse(500, "Failed to reset feed config");
        return;
    }

    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleFeedsSync() {
    String scope = server.hasArg("scope") ? server.arg("scope") : "all";
    String error;
    if (!feedsSyncNow(scope.c_str(), &error)) {
        sendTextResponse(502, error.length() > 0 ? error : "Feed sync failed");
        return;
    }

    displayUpdate();
    String json;
    feedsBuildStateJson(json);
    sendJsonResponse(200, json);
}

void handleFeedSearch() {
    if (!server.hasArg("query")) {
        sendTextResponse(400, "Missing query parameter");
        return;
    }

    String query = server.arg("query");
    query.trim();
    if (query.length() < 2) {
        sendTextResponse(400, "Enter at least 2 characters");
        return;
    }

    String json;
    String error;
    if (!feedsSearchWeatherLocations(query, json, &error)) {
        sendTextResponse(502, error.length() > 0 ? error : "Search failed");
        return;
    }

    sendJsonResponse(200, json);
}

void handleFileUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        uploadErrorMessage = "";
        uploadStatusCode = 200;
        uploadTargetPath[0] = '\0';

        String safeFilename;
        if (!sanitizeUploadFilename(upload.filename, safeFilename, &uploadErrorMessage)) {
            uploadStatusCode = 400;
            logPrintf("ERROR: Upload rejected: %s", uploadErrorMessage.c_str());
            return;
        }

        String filePath = String(IMAGE_DIR) + safeFilename;
        strncpy(uploadTargetPath, filePath.c_str(), sizeof(uploadTargetPath) - 1);
        uploadTargetPath[sizeof(uploadTargetPath) - 1] = '\0';
        uploadFile = LittleFS.open(uploadTargetPath, "w");

        if (!uploadFile) {
            uploadStatusCode = 500;
            uploadErrorMessage = "Failed to open upload target";
            logPrintf("ERROR: Failed to open file %s for writing", uploadTargetPath);
            uploadTargetPath[0] = '\0';
        } else {
            logPrintf("INFO: Upload started for %s", uploadTargetPath);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) {
            uploadFile.close();
        }
        if (uploadTargetPath[0] != '\0') {
            LittleFS.remove(uploadTargetPath);
        }
        uploadStatusCode = 400;
        uploadErrorMessage = "Upload aborted";
        uploadTargetPath[0] = '\0';
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            uploadFile.close();
            logPrintf("INFO: Upload complete: %s (%u bytes)", uploadTargetPath, upload.totalSize);
        }
    }
}

void handleUploadDone() {
    String message = uploadErrorMessage.length() > 0
                         ? uploadErrorMessage
                         : (uploadTargetPath[0] != '\0' ? String(uploadTargetPath) : String("OK"));
    sendTextResponse(uploadStatusCode, message);
    uploadErrorMessage = "";
    uploadStatusCode = 200;
    uploadTargetPath[0] = '\0';
}

void handleDelete() {
    JsonDocument doc;
    String error;
    String filePath = server.hasArg("file") ? server.arg("file") : "";
    if (server.hasArg("plain")) {
        if (!parseJsonBody(doc, &error)) {
            sendTextResponse(400, error);
            return;
        }
        filePath = doc["file"] | filePath;
    }

    String normalizedPath;
    if (!sanitizeImagePath(filePath, normalizedPath, &error) || normalizedPath.length() == 0) {
        sendTextResponse(400, error.length() > 0 ? error : "Missing file parameter");
        return;
    }

    if (!LittleFS.remove(normalizedPath)) {
        sendTextResponse(404, "Not found");
        return;
    }

    if (String(currentImage) == normalizedPath) {
        clearCurrentImageSelection();
    }

    sendTextResponse(200, "Deleted");
}

void handleImageShow() {
    JsonDocument doc;
    String error;
    if (!parseJsonBody(doc, &error)) {
        sendTextResponse(400, error);
        return;
    }

    if (!setCurrentImageSelection(doc["path"] | "", &error)) {
        sendTextResponse(400, error);
        return;
    }

    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleApiUpdate() {
    if (!server.hasArg("plain")) {
        sendTextResponse(400, "No JSON body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        sendTextResponse(400, "Invalid JSON");
        return;
    }

    if (!doc["line1"].isNull() && !doc["line1"].is<const char*>()) {
        sendTextResponse(400, "line1 must be a string");
        return;
    }
    if (!doc["line1"].isNull()) {
        strncpy(displayState.line2, doc["line1"].as<const char*>(), sizeof(displayState.line2) - 1);
        displayState.line2[sizeof(displayState.line2) - 1] = '\0';
    } else {
        displayState.line2[0] = '\0';
    }

    displayState.showImage = false;
    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleTestCard() {
    clearCurrentImageSelection();
    displayState.apMode = false;
    displayState.line2[0] = '\0';
    displayState.currentPage = DASHBOARD_PAGE_CLOCK;
    displayUpdate();
    sendTextResponse(200, "OK");
}

void handleReconfigureWiFi() {
    if (webserverHasPendingNetworkAction()) {
        sendTextResponse(409, "Another network task is already running");
        return;
    }

    wifiReconfigurePending = true;
    sendTextResponse(202, "WiFi credentials will be cleared. Device restarting to AP mode.");
}

void handleFactoryReset() {
    factoryResetPending = true;
    sendTextResponse(202, "Factory reset requested. Device will restart shortly.");
}

void handleOTAForm() {
    prepareNoStoreHeaders();
    server.send_P(200, "text/html", ota_html);
}

void handleOTAUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        displayShowMessage("OTA Update...");
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            displayShowMessage("OTA complete");
        } else {
            Update.printError(Serial);
            displayShowMessage("OTA failed");
        }
    }
}

void handleOTADone() {
    bool shouldReboot = !Update.hasError();
    sendTextResponse(200, shouldReboot ? "OK - Rebooting..." : "FAIL");

    if (shouldReboot) {
        delay(1000);
        ESP.restart();
    }
}

void handleLog() {
    sendTextResponse(200, logGetAll());
}

void handleWiFiScan() {
    int networkCount = WiFi.scanNetworks(false, true);
    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();
    for (int index = 0; index < networkCount; ++index) {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(index);
        network["rssi"] = WiFi.RSSI(index);
        network["encryption"] = WiFi.encryptionType(index);
    }

    String json;
    serializeJson(doc, json);
    WiFi.scanDelete();
    sendJsonResponse(200, json);
}

void handleWiFiConnect() {
    if (webserverHasPendingNetworkAction()) {
        sendTextResponse(409, "Another network task is already running");
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
    if (ssid == nullptr || ssid[0] == '\0') {
        sendTextResponse(400, "Missing SSID");
        return;
    }

    copyString(wifiConnectTask.ssid, sizeof(wifiConnectTask.ssid), ssid);
    copyString(wifiConnectTask.password, sizeof(wifiConnectTask.password), password);
    wifiConnectTask.queued = true;
    sendTextResponse(202, "Connection started. Device will restart if successful.");
}

void handleRoot() {
    if (requestHasValidSession()) {
        attachSessionCookie(kAuthSessionMaxAgeSec);
    } else if (server.hasHeader("Cookie")) {
        clearSessionCookie();
    }

    prepareNoStoreHeaders();
    server.send_P(200, "text/html", index_html);
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
    registerProtectedRoute("/delete", HTTP_POST, handleDelete);
    registerProtectedRoute("/log", HTTP_GET, handleLog);
    registerProtectedRoute("/reconfigurewifi", HTTP_POST, handleReconfigureWiFi);
    registerProtectedRoute("/factoryreset", HTTP_POST, handleFactoryReset);
    registerProtectedRoute("/scan", HTTP_GET, handleWiFiScan);
    registerProtectedRoute("/connect", HTTP_POST, handleWiFiConnect);
    registerProtectedRoute("/test", HTTP_POST, handleTestCard);
    registerProtectedRoute("/image/show", HTTP_POST, handleImageShow);
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
    registerProtectedRoute("/feeds/search", HTTP_GET, handleFeedSearch);
    registerProtectedRoute("/api/update", HTTP_POST, handleApiUpdate);
    registerProtectedRoute("/api/dashboard", HTTP_POST, handleDashboardDataSave);
    registerProtectedRoute("/auth/password", HTTP_POST, handleAuthPasswordChange);

    server.on("/image/upload", HTTP_POST,
              []() {
                  if (!ensureAuthenticatedRequest()) {
                      return;
                  }
                  handleUploadDone();
              },
              []() {
                  if (!requestHasValidSession()) {
                      return;
                  }
                  handleFileUpload();
              });
    server.on("/doUpload", HTTP_POST,
              []() {
                  if (!ensureAuthenticatedRequest()) {
                      return;
                  }
                  handleUploadDone();
              },
              []() {
                  if (!requestHasValidSession()) {
                      return;
                  }
                  handleFileUpload();
              });
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

    server.begin();
    Serial.println(F("Web server started"));
}

void webserverHandle() {
    server.handleClient();
}

void webserverProcessPendingActions() {
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
    return rebootPending ||
           factoryResetPending ||
           wifiReconfigurePending ||
           wifiConnectTask.queued ||
           wifiConnectTask.active;
}
