#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
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
unsigned long lastWiFiCheck = 0;
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
constexpr uint32_t kOptionalServiceMinFreeHeapBytes = 28000UL;
constexpr uint32_t kOptionalServiceRetryLogMs = 5000UL;

void logHeapState(const char *stage) {
    logPrintf("Free heap after %s: %u", stage, ESP.getFreeHeap());
}

void logOptionalServiceDelay(const char *serviceName, uint32_t freeHeap) {
    unsigned long now = millis();
    if (now - lastOptionalServiceLogAt < kOptionalServiceRetryLogMs) {
        return;
    }

    lastOptionalServiceLogAt = now;
    logPrintf("Deferring %s, free heap too low: %u", serviceName, freeHeap);
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

bool tryConnectWiFi(int maxAttempts) {
    Serial.printf("Attempting WiFi connection (max %d attempts)...\n", maxAttempts);

    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        Serial.printf("WiFi attempt %d/%d\n", attempt, maxAttempts);
        char msgBuffer[64];
        snprintf(msgBuffer, sizeof(msgBuffer), "WiFi...\nAttempt %d/%d", attempt, maxAttempts);
        displayShowMessage(msgBuffer);

        WiFi.mode(wifiFailsafeMode ? WIFI_AP_STA : WIFI_STA);
        applyConfiguredHostname();
        WiFi.begin();

        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - startAttempt < WIFI_CONNECTION_TIMEOUT) {
            delay(100);
            yield();
        }

                // Wait for IP address to be assigned after WiFi connection
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println(F("WiFi associated, waiting for IP..."));
                    unsigned long ipWaitStart = millis();
                    while (WiFi.localIP() == IPAddress(0,0,0,0) &&
                           millis() - ipWaitStart < 10000) { // Wait up to 10 seconds for IP
                        delay(100);
                        yield();
                    }
                }

                if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {

                    Serial.println(F("WiFi connected!"));

                    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

                    char ipMsgBuffer[64];
                    snprintf(ipMsgBuffer, sizeof(ipMsgBuffer), "WiFi OK\n%s", WiFi.localIP().toString().c_str());
                    displayShowMessage(ipMsgBuffer);

                    delay(2000);

                    return true;

                }

        

                // Exponential backoff between retries (except on last attempt)

                if (attempt < maxAttempts) {

                    int delayMs = WIFI_RETRY_DELAY_MS * (1 << (attempt - 1)); // 2s, 4s, 8s, 16s...

                    delayMs = min(delayMs, 30000); // Cap at 30 seconds

                    Serial.printf("Retry in %d ms...\n", delayMs);

                    delay(delayMs);

                }

            }

        

            return false;

        }

        

        void setupWiFi() {

            displayShowMessage(F("WiFi Setup..."));

            Serial.println(F("=== WiFi Setup Start ==="));

        

            // Generate random AP password if not already generated

            if (apPassword.isEmpty()) {

                apPassword = generateRandomPassword(8);

                Serial.printf("Generated AP Password: %s\n", apPassword.c_str());

            }

        

            // Check if WiFi credentials are saved BEFORE attempting connection

            String ssid = WiFi.SSID();

        

            // Flag to indicate if we need to proceed to the Failsafe AP section

            bool needsFailsafeAP = false;

        

            if (ssid.isEmpty() || ssid.length() == 0) {

                Serial.println(F("No saved WiFi credentials - going directly to failsafe AP"));

                needsFailsafeAP = true;

            } else {

                // Try to connect to saved WiFi credentials with retry

                Serial.println(F("Attempting to connect with saved credentials..."));

                if (tryConnectWiFi(WIFI_RETRY_ATTEMPTS)) {

                    wifiFailsafeMode = false;

                    Serial.println(F("Connected successfully!"));

                    return; // Exit setupWiFi as connection is established

                } else {

                    // Connection failed, try WiFiManager config portal

                    Serial.println(F("WiFi connection failed - attempting WiFiManager config portal"));

                    displayShowMessage(F("WiFi Failed!\nStarting AP..."));

                    delay(1000);

        

                    // Set timeout - don't reset settings, let WiFiManager try saved credentials first
                    WiFiManager wifiManager;
                    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);
                    applyConfiguredHostname();

                    Serial.printf("Starting WiFiManager autoConnect (timeout: %d seconds)...\n", WIFI_TIMEOUT);

                    displayShowMessage(F("Config Portal\nStarting..."));

                    yield();

        

                    // Try autoConnect with error handling

                    Serial.println(F("Calling wifiManager.autoConnect()..."));

                    bool connectedViaManager = wifiManager.autoConnect(WIFI_AP_NAME, apPassword.c_str());

                    yield();

                    Serial.printf("autoConnect returned: %s\n", connectedViaManager ? "true" : "false");

        

                    if (!connectedViaManager) {

                        needsFailsafeAP = true;

                    } else {

                        wifiFailsafeMode = false;

                        Serial.println(F("WiFiManager connected successfully!"));

                        char ipMsgBuffer[64];
                        snprintf(ipMsgBuffer, sizeof(ipMsgBuffer), "WiFi OK\n%s", WiFi.localIP().toString().c_str());
                        displayShowMessage(ipMsgBuffer);

                        delay(2000);

                        return; // Exit setupWiFi as connection is established via manager

                    }

                }

            }

        

            // This block is executed only if needsFailsafeAP is true

            if (needsFailsafeAP) {

                Serial.println(F("Entering failsafe AP mode"));

                displayShowMessage(F("Starting\nFailsafe AP..."));

                delay(1000);

        

                // Ensure WiFi is in AP mode

                WiFi.disconnect(true, false);

                yield();

                WiFi.mode(WIFI_AP);

                yield();

        

                Serial.printf("Attempting to start AP: SSID='%s', Password='%s'\n", WIFI_AP_NAME, apPassword.c_str());

                bool apStarted = WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());

                Serial.printf("AP Start result: %s\n", apStarted ? "SUCCESS" : "FAILED");

        

                if (!apStarted) {

                    // If AP failed to start, try one more time after delay

                    Serial.println(F("AP start failed, retrying after delay..."));

                    delay(2000);

                    WiFi.mode(WIFI_OFF);

                    delay(500);

                    WiFi.mode(WIFI_AP);

                    delay(500);

                    apStarted = WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());

                    Serial.printf("Retry AP Start result: %s\n", apStarted ? "SUCCESS" : "FAILED");

                }

        

                wifiFailsafeMode = true;

        

                Serial.printf("Failsafe AP started\n");

                Serial.printf("  SSID: %s\n", WIFI_AP_NAME);

                Serial.printf("  Password: %s\n", apPassword.c_str());

                Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());

                displayBlankScreen(); // Blank screen before showing AP credentials
        
                displayShowAPScreen(WIFI_AP_NAME, apPassword.c_str(), WiFi.softAPIP().toString().c_str());

                delay(5000);

            }

        

            Serial.println(F("=== WiFi Setup Complete ==="));
}

void monitorWiFi() {
    // In failsafe mode, periodically try to reconnect to WiFi (only if credentials are saved)
    if (wifiFailsafeMode) {
        // Only attempt reconnection if WiFi credentials are actually saved
        String ssid = WiFi.SSID();
        if (!ssid.isEmpty() && ssid.length() > 0) {
            if (millis() - lastWiFiReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
                Serial.println(F("Failsafe mode: attempting WiFi reconnection..."));
                lastWiFiReconnectAttempt = millis();

                if (tryConnectWiFi(2)) {  // Quick 2 attempts
                    wifiFailsafeMode = false;
                    Serial.println(F("Reconnected! Exiting failsafe mode"));
                    // Restart to reinitialize services properly
                    delay(1000);
                    ESP.restart();
                }
            }
        }
        return;
    }

    // Normal mode: check WiFi connection periodically
    if (millis() - lastWiFiCheck > WIFI_MONITOR_INTERVAL) {
        lastWiFiCheck = millis();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi connection lost - attempting reconnection"));

            if (!tryConnectWiFi(3)) {  // Try 3 quick attempts
                Serial.println(F("Reconnection failed - entering failsafe mode"));

                WiFi.mode(WIFI_AP);
                WiFi.softAP(WIFI_AP_NAME, apPassword.c_str());
                wifiFailsafeMode = true;

                Serial.printf("Failsafe AP started\n");
                Serial.printf("  SSID: %s\n", WIFI_AP_NAME);
                Serial.printf("  Password: %s\n", apPassword.c_str());
                Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());

                displayBlankScreen(); // Blank screen before showing AP credentials
                // Set AP mode display state
                displayShowAPScreen(WIFI_AP_NAME, apPassword.c_str(), WiFi.softAPIP().toString().c_str());
                delay(3000);
            }
        }
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
            logPrintf("Deleted: %s", filepath.c_str());
            filesDeleted++;
        } else {
            logPrintf("Failed to delete: %s", filepath.c_str());
        }
    }
    logPrintf("Cleared %d files from image directory.", filesDeleted);
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

    logPrintf("Firmware Version: %d", FIRMWARE_VERSION);

    const rst_info* resetInfo = ESP.getResetInfoPtr();
    uint8_t resetReason = resetInfo ? resetInfo->reason : 0xFF;
    logPrintf("Reset reason: %s (%u)", describeResetReason(resetReason), resetReason);



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
            logPrintf("Admin credentials provisioned: user=%s password=%s",
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

        logPrintf("Running in FAILSAFE mode. AP IP: %s", WiFi.softAPIP().toString().c_str());

    } else {

        logPrintf("Setup complete. IP: %s", WiFi.localIP().toString().c_str());

    }

}



void loop() {

    // Reset power cycle counter after 10 seconds of successful uptime

    // This prevents accidental factory reset from normal reboots

    if (!powerCycleCounterCleared && millis() > 10000) {

        powerCycleCounterReset();

        powerCycleCounterCleared = true;

        Serial.println(F("Power cycle counter cleared after successful boot"));

    }



    bool networkActionBusy = webserverHasPendingNetworkAction();

    // Monitor WiFi connection and handle failsafe mode
    if (!networkActionBusy) {
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
    if (!wifiFailsafeMode && !networkActionBusy) {
        startDeferredNetworkServices();
        if (otaStarted) {
            ArduinoOTA.handle();
        }
        if (mdnsStarted) {
            MDNS.update();
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
    if (!wifiFailsafeMode && !recoveryBootMode && !networkActionBusy && timeServicesStarted) {
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
