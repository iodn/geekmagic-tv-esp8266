#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <user_interface.h>
#include "config.h"
#include "dashboard.h"
#include "display.h"
#include "feeds.h"
#include "auth.h"
#include "webserver.h"
#include "settings.h"
#include "logger.h"
#include "button.h"

// Define NTP Client
Settings appSettings;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDisplayProfileCheck = 0;
unsigned long lastPageRotation = 0;
unsigned long lastWiFiReconnectAttempt = 0;
bool wifiFailsafeMode = false;  // True when in AP-only mode after connection failures
String apPassword = "";  // Generated random AP password
bool powerCycleCounterCleared = false;  // Track if power cycle counter has been reset
bool recoveryBootMode = false;
bool bootSuccessRecorded = false;
bool clockSpriteWarmupDone = false;
bool timeServicesStarted = false;
bool mdnsStarted = false;
bool otaStarted = false;
unsigned long bootCompletedAt = 0;
unsigned long lastOptionalServiceLogAt = 0;
unsigned long lastMdnsStartAttempt = 0;

constexpr uint32_t kBootSuccessConfirmMs = 30000UL;
constexpr uint32_t kClockSpriteWarmupMs = 8000UL;
constexpr uint32_t kDisplayProfileCheckMs = 15000UL;
constexpr uint32_t kOptionalServiceWarmupMs = 1500UL;
constexpr uint32_t kOptionalServiceMinFreeHeapBytes = 8000UL;
constexpr uint32_t kOptionalServiceRetryLogMs = 5000UL;

void logHeapState(const char *stage) {
    uint32_t freeHeap = 0;
    uint32_t largestBlock = 0;
    uint8_t fragmentation = 0;
    ESP.getHeapStats(&freeHeap, &largestBlock, &fragmentation);
    logPrintf(F("Heap %s: free=%u max=%u frag=%u%%"), stage, freeHeap, largestBlock, fragmentation);
}

void logOptionalServiceDelay(const char *serviceName, uint32_t freeHeap) {
    unsigned long now = millis();
    if (now - lastOptionalServiceLogAt < kOptionalServiceRetryLogMs) {
        return;
    }

    lastOptionalServiceLogAt = now;
    logPrintf(F("Deferring %s, free heap too low: %u"), serviceName, freeHeap);
}

const char* describeResetReason(uint8_t reason) {
    switch (reason) {
        case REASON_DEFAULT_RST:
            return "power-on";
        case REASON_WDT_RST:
            return "hardware-watchdog";
        case REASON_EXCEPTION_RST:
            return "exception";
        case REASON_SOFT_WDT_RST:
            return "software-watchdog";
        case REASON_SOFT_RESTART:
            return "software-restart";
        case REASON_DEEP_SLEEP_AWAKE:
            return "deep-sleep";
        case REASON_EXT_SYS_RST:
            return "external-reset";
        default:
            return "unknown";
    }
}

bool isManualPowerCycleReset(uint8_t reason) {
    return reason == REASON_DEFAULT_RST || reason == REASON_EXT_SYS_RST;
}

// Generate a random numeric password (8 digits)
String generateRandomPassword(int length) {
    const char charset[] = "0123456789";
    String password = "";

    // Seed random with hardware random number generator
    randomSeed(ESP.getCycleCount() ^ micros() ^ ESP.getChipId());

    for (int i = 0; i < length; i++) {
        int index = random(0, sizeof(charset) - 1);
        password += charset[index];
    }

    return password;
}

void copyConfiguredHostname(char *buffer, size_t bufferSize) {
    settingsBuildHostname(appSettings.deviceName, buffer, bufferSize);
}

void applyConfiguredHostname() {
    char hostname[SETTINGS_HOSTNAME_LENGTH];
    copyConfiguredHostname(hostname, sizeof(hostname));
    WiFi.hostname(hostname);
}

bool wifiReconnectActive = false;
unsigned long wifiReconnectStartedAt = 0;
unsigned long lastApStartAttempt = 0;
bool setupApStarted = false;
DNSServer setupDns;

void ensureSetupAP() {
    if (setupApStarted || (lastApStartAttempt != 0 && millis() - lastApStartAttempt < 2000UL)) {
        return;
    }
    lastApStartAttempt = millis();
    WiFi.mode(WIFI_AP_STA);
    setupApStarted = WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());
    if (setupApStarted) {
        setupDns.start(53, "*", WiFi.softAPIP());
        displayShowAPScreen(WIFI_AP_NAME, apPassword.c_str(), WiFi.softAPIP().toString().c_str());
    }
}

void startWiFiReconnect() {
    if (WiFi.SSID().isEmpty()) {
        return;
    }
    ensureSetupAP();
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin();
    wifiReconnectStartedAt = millis();
    lastWiFiReconnectAttempt = wifiReconnectStartedAt;
    wifiReconnectActive = true;
}

void trackWiFiReconnectAttempt(bool active) {
    wifiReconnectActive = active;
    wifiReconnectStartedAt = millis();
    lastWiFiReconnectAttempt = wifiReconnectStartedAt;
}

void setupWiFi() {
    if (apPassword.isEmpty()) {
        apPassword = generateRandomPassword(8);
    }
    applyConfiguredHostname();
    wifiFailsafeMode = true;
    // Keep setup access available while the station connects or retries.
    ensureSetupAP();
    startWiFiReconnect();
}

void monitorWiFi() {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        wifiReconnectActive = false;
        if (wifiFailsafeMode) {
            wifiFailsafeMode = false;
            WiFi.softAPdisconnect(true);
            setupDns.stop();
            setupApStarted = false;
            WiFi.mode(WIFI_STA);
            snprintf(displayState.ipInfo, sizeof(displayState.ipInfo), "%s", WiFi.localIP().toString().c_str());
            displayShowDashboard();
            logPrintf(F("WiFi connected: %s"), WiFi.localIP().toString().c_str());
        }
        return;
    }

    if (!wifiFailsafeMode) {
        wifiFailsafeMode = true;
        setupApStarted = false;
        mdnsStarted = false;
        MDNS.end();
        if (otaStarted) {
            ArduinoOTA.end();
            otaStarted = false;
        }
    }
    ensureSetupAP();
    if (wifiReconnectActive) {
        if (millis() - wifiReconnectStartedAt >= WIFI_CONNECTION_TIMEOUT) {
            // Stop this attempt without erasing the saved station configuration.
            WiFi.disconnect(false, false);
            wifiReconnectActive = false;
        }
    } else if (lastWiFiReconnectAttempt == 0 ||
               millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
        startWiFiReconnect();
    }
}

bool setupMDNS() {
    char hostname[SETTINGS_HOSTNAME_LENGTH];
    snprintf(hostname, sizeof(hostname), "%s", WiFi.hostname().c_str());

    if (!MDNS.begin(hostname)) {
        Serial.println(F("mDNS failed"));
        return false;
    }

    MDNS.addService("http", "tcp", WEB_SERVER_PORT);
    MDNS.addServiceTxt("http", "tcp", "model", "SmartClock");
    MDNS.addServiceTxt("http", "tcp", "vendor", "Custom");
    MDNS.addServiceTxt("http", "tcp", "api", "geekmagic");
    MDNS.addServiceTxt("http", "tcp", "name", appSettings.deviceName);

    Serial.printf("mDNS started: %s.local\n", hostname);
    return true;
}

void setupOTA() {
    char hostname[SETTINGS_HOSTNAME_LENGTH];
    snprintf(hostname, sizeof(hostname), "%s", WiFi.hostname().c_str());

    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPasswordHash(authOtaPasswordHash());

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        Serial.println("OTA Start: " + type);
        displayShowMessage(F("OTA Update..."));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println(F("OTA Complete"));
        displayShowMessage(F("Success!"));
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int percent = (progress * 100) / total;
        Serial.printf("Progress: %u%%\n", percent);

        static int lastPercent = -1;
        if (percent != lastPercent) {
            tft.fillRect(20, 130, 200, 20, TFT_BLACK);
            tft.drawRect(20, 130, 200, 20, TFT_WHITE);
            tft.fillRect(22, 132, (percent * 196) / 100, 16, TFT_BLUE);
            lastPercent = percent;
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        displayShowMessage(F("OTA Failed!"));
    });

    ArduinoOTA.begin();
    Serial.println(F("OTA ready"));
}

void startTimeServices() {
    if (timeServicesStarted || wifiFailsafeMode || WiFi.status() != WL_CONNECTED) {
        return;
    }

    configTime(appSettings.gmtOffset, 0, NTP_SERVER);
    timeServicesStarted = true;
    logPrint(F("Time services started"));
    logHeapState("time services init");
}

void startDeferredNetworkServices() {
    if (recoveryBootMode || wifiFailsafeMode || WiFi.status() != WL_CONNECTED || bootCompletedAt == 0) {
        return;
    }

    if (millis() - bootCompletedAt < kOptionalServiceWarmupMs) {
        return;
    }

    if (!timeServicesStarted) {
        startTimeServices();
        return;
    }

    uint32_t freeHeap = ESP.getFreeHeap();
    if (!mdnsStarted) {
        if (freeHeap < kOptionalServiceMinFreeHeapBytes) {
            logOptionalServiceDelay("mDNS", freeHeap);
            return;
        }

        if (lastMdnsStartAttempt != 0 && millis() - lastMdnsStartAttempt < 5000UL) {
            return;
        }
        lastMdnsStartAttempt = millis();
        mdnsStarted = setupMDNS();
        logHeapState("mDNS init");
        return;
    }

    if (!otaStarted) {
        if (freeHeap < kOptionalServiceMinFreeHeapBytes) {
            logOptionalServiceDelay("OTA", freeHeap);
            return;
        }

        setupOTA();
        otaStarted = true;
        logHeapState("OTA init");
    }
}

void maybeMarkBootSuccessful() {
    if (bootSuccessRecorded || bootCompletedAt == 0 || millis() - bootCompletedAt < kBootSuccessConfirmMs) {
        return;
    }

    bootCounterReset();
    bootSuccessRecorded = true;

    if (!recoveryBootMode && !wifiFailsafeMode && !clockSpriteWarmupDone) {
        displaySetClockSpriteAllowed(true);
        clockSpriteWarmupDone = true;
    }

    logPrint(F("Boot stability confirmed"));

    logHeapState("stability window");
}

void maybeEnableClockSprite() {
    if (clockSpriteWarmupDone || recoveryBootMode || wifiFailsafeMode || bootCompletedAt == 0) {
        return;
    }

    if (millis() - bootCompletedAt < kClockSpriteWarmupMs) {
        return;
    }

    displaySetClockSpriteAllowed(true);
    clockSpriteWarmupDone = true;
    logPrint(F("Clock sprite warmup complete"));
}

void clearImageDirectory() {
    logPrint("Clearing image directory: " + String(IMAGE_DIR));
    Dir dir = LittleFS.openDir(IMAGE_DIR);
    int filesDeleted = 0;
    while (dir.next()) {
        String filepath = dir.fileName();
        if (LittleFS.remove(filepath)) {
            logPrintf(F("Deleted: %s"), filepath.c_str());
            filesDeleted++;
        } else {
            logPrintf(F("Failed to delete: %s"), filepath.c_str());
        }
    }
    logPrintf(F("Cleared %d files from image directory."), filesDeleted);
}

void setupFilesystem() {

    //LittleFS.format();

        if (!LittleFS.begin()) {

            Serial.println(F("LittleFS mount failed. Formatting LittleFS..."));

            displayShowMessage(F("Formatting FS..."));

            LittleFS.format(); // Format LittleFS if mounting fails

            Serial.println(F("LittleFS formatted. Restarting..."));

            delay(3000);

            ESP.restart(); // Restart after formatting

        }



    if (!LittleFS.exists(IMAGE_DIR)) {

        LittleFS.mkdir(IMAGE_DIR);

    }



    Serial.println(F("LittleFS ready"));



    clearImageDirectory(); // Call the new function here

}



void setup() {

    Serial.begin(115200);

    delay(100);



    loggerInit();

    logPrint(F("\n\n========================================"));

    logPrint(F("SmartClock Starting..."));

    logPrintf(F("Firmware Version: %d"), FIRMWARE_VERSION);

    const rst_info* resetInfo = ESP.getResetInfoPtr();
    uint8_t resetReason = resetInfo ? resetInfo->reason : 0xFF;
    logPrintf(F("Reset reason: %s (%u)"), describeResetReason(resetReason), resetReason);



    // Initialize EEPROM and boot counter

    settingsInit();

    bootCounterInit();  // Increment boot failure counter

    powerCycleCounterInit(isManualPowerCycleReset(resetReason));



    // Check for user-initiated factory reset (5 quick power cycles)

    if (powerCycleCounterCheckReset()) {

        Serial.println(F("========================================"));

        Serial.println(F("USER RESET: 5 quick power cycles detected!"));

        Serial.println(F("Performing factory reset..."));

        Serial.println(F("========================================"));



        // Factory reset sequence

        WiFi.disconnect(true);

        delay(100);



        ESP.eraseConfig();

        delay(100);



        settingsReset(appSettings);

        settingsSave(appSettings);

        bootCounterReset();

        powerCycleCounterReset();

        bool fsMountedForReset = LittleFS.begin();
        Serial.printf("LittleFS mount before manual reset: %s\n", fsMountedForReset ? "OK" : "FAILED");
        if (LittleFS.format()) {
            Serial.println(F("LittleFS format complete."));
        } else {
            Serial.println(F("LittleFS format failed."));
        }
        if (fsMountedForReset) {
            LittleFS.end();
        }



        Serial.println(F("Factory reset complete. System will restart in 5 seconds..."));

        delay(5000);

        ESP.restart();

    }



    recoveryBootMode = bootCounterShouldEnterRecovery();
    if (recoveryBootMode) {
        Serial.println(F("========================================"));
        Serial.println(F("RECOVERY MODE: repeated early boot failures detected"));
        Serial.println(F("Preserving saved settings and starting with optional services disabled"));
        Serial.println(F("========================================"));
    }



    // Load and validate settings

    settingsLoad(appSettings);



    displayInit();
    logHeapState("display init");
    displaySetClockSpriteAllowed(false);
    displaySetBrightness(appSettings.brightness);  // Prime saved brightness before dashboard profile loads

    buttonInit();  // Initialize GPIO button

    displayShowMessage(F("SmartClock\nInitializing..."));

    delay(2000);



        setupFilesystem();
        authInit();
        if (authWasProvisionedThisBoot()) {
            logPrintf(F("Admin credentials provisioned: user=%s password=%s"),
                      authUsername(),
                      authProvisionedPassword());
            displayShowMessage(String(F("Admin login\n")) + authUsername() + "\n" + authProvisionedPassword());
            delay(4000);
        }
        dashboardInit();
        feedsInit();
        logHeapState("filesystem/auth/dashboard/feeds init");

        currentBrightness = appSettings.brightness;
        currentTheme = dashboardConfig.theme;
        webserverApplyEffectiveBrightness(true);

        if (appSettings.lastImage[0] != '\0') {
            logPrint(F("Clearing stale image path after temporary image cleanup"));
            appSettings.lastImage[0] = '\0';
            settingsSave(appSettings);
        }



        setupWiFi();
        logHeapState("wifi setup");



    



        displayState.currentPage = dashboardFirstEnabledPage();



        strncpy(currentImage, appSettings.lastImage, sizeof(currentImage));



        currentImage[sizeof(currentImage) - 1] = '\0'; // Ensure null-termination



    // Always default to clock display on boot as images are cleared

    displayState.showImage = false;



        webserverInit();
        logHeapState("webserver init");



    



        // Only show "Ready!" message if not in AP mode



        if (recoveryBootMode && !displayState.apMode) {
            displayShowMessage(F("Recovery Mode\nOpen dashboard"));
            delay(1500);
        } else if (!displayState.apMode) {



            displayShowMessage(F("Ready!"));



            delay(2000);



        }



    logPrint(F("Calling displayUpdate()..."));

    displayUpdate();
    logHeapState("initial display render");
    lastPageRotation = millis();

    logPrint(F("Display updated"));
    bootCompletedAt = millis();

    if (recoveryBootMode) {
        logPrint(F("Core boot complete in recovery mode"));
    } else {
        logPrint(F("Core boot complete; deferred services will start after warmup"));
    }



    if (wifiFailsafeMode) {

        logPrintf(F("Running in FAILSAFE mode. AP IP: %s"), WiFi.softAPIP().toString().c_str());

    } else {

        logPrintf(F("Setup complete. IP: %s"), WiFi.localIP().toString().c_str());

    }

}



void loop() {

    if (wifiFailsafeMode) {
        setupDns.processNextRequest();
    }

    // Reset power cycle counter after 10 seconds of successful uptime

    // This prevents accidental factory reset from normal reboots

    if (!powerCycleCounterCleared && millis() > 10000) {

        powerCycleCounterReset();

        powerCycleCounterCleared = true;

        Serial.println(F("Power cycle counter cleared after successful boot"));

    }



    bool networkActionBusy = webserverHasPendingNetworkAction();

    // Monitor WiFi connection and handle failsafe mode
    if (feedsBusy() && WiFi.status() != WL_CONNECTED) {
        feedsCancelJobs();
    }
    if (!networkActionBusy && !feedsBusy()) {
        monitorWiFi();
    }

    // Handle button presses
    ButtonPress buttonPress = buttonUpdate();
    if (buttonPress == BUTTON_SHORT) {
        displayCycleNextPage();
        lastPageRotation = millis();
    } else if (buttonPress == BUTTON_LONG) {
        displayToggleBacklight();
    }

    webserverHandle();
    webserverProcessPendingActions();
    networkActionBusy = webserverHasPendingNetworkAction();

    // Only handle OTA and mDNS if not in failsafe mode
    if (!wifiFailsafeMode) {
        if (!networkActionBusy) {
            startDeferredNetworkServices();
            if (otaStarted && !feedsBusy()) {
                ArduinoOTA.handle();
            }
            if (mdnsStarted) {
                MDNS.update();
            }
        }
        // Only update time if not showing an image
        if (!displayState.showImage) {
            displayState.apMode = false;  // Ensure AP mode is disabled in normal mode
            strncpy(displayState.ipInfo, WiFi.localIP().toString().c_str(), sizeof(displayState.ipInfo));
            displayState.ipInfo[sizeof(displayState.ipInfo) - 1] = '\0';
        }
    } else {
        // In failsafe mode, show AP credentials on display
        if (!displayState.showImage) {
            displayState.apMode = true;
            strncpy(displayState.apSSID, WIFI_AP_NAME, sizeof(displayState.apSSID));
            displayState.apSSID[sizeof(displayState.apSSID) - 1] = '\0';
            strncpy(displayState.apPassword, apPassword.c_str(), sizeof(displayState.apPassword));
            displayState.apPassword[sizeof(displayState.apPassword) - 1] = '\0';
            strncpy(displayState.ipInfo, WiFi.softAPIP().toString().c_str(), sizeof(displayState.ipInfo));
            displayState.ipInfo[sizeof(displayState.ipInfo) - 1] = '\0';
        }
    }

    displayServiceTemporaryMessage();
    dashboardSyncRuntimeState();
    if (millis() - lastDisplayProfileCheck > kDisplayProfileCheckMs) {
        webserverApplyEffectiveBrightness(false);
        lastDisplayProfileCheck = millis();
    }
    if (feedsBusy() ||
        (!wifiFailsafeMode && !recoveryBootMode && !networkActionBusy && timeServicesStarted)) {
        feedsLoop();
    }

    if (!wifiFailsafeMode &&
        !networkActionBusy &&
        !displayState.showImage &&
        !displayState.apMode &&
        dashboardConfig.rotationEnabled &&
        millis() - lastPageRotation > static_cast<unsigned long>(dashboardConfig.rotationIntervalSec) * 1000UL) {
        displayCycleNextPage(true);
        lastPageRotation = millis();
    }

    maybeEnableClockSprite();

    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
        displayUpdate();
        lastDisplayUpdate = millis();
    }

    maybeMarkBootSuccessful();

    yield();
}
