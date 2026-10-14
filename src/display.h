#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "dashboard.h"

// Define buffer sizes for DisplayState char arrays
#define DISPLAY_LINE_BUFFER_SIZE 64
#define DISPLAY_IP_BUFFER_SIZE 24
#define DISPLAY_PATH_BUFFER_SIZE 64
#define DISPLAY_SSID_BUFFER_SIZE 36
#define DISPLAY_PASS_BUFFER_SIZE 16

struct DisplayState {
    char line2[DISPLAY_LINE_BUFFER_SIZE];       // Used for custom messages
    char ipInfo[DISPLAY_IP_BUFFER_SIZE];      // IP address or network info to show at top
    bool showImage;     // True if an image is currently displayed
    char imagePath[DISPLAY_PATH_BUFFER_SIZE];   // Path to the image file
    bool apMode;        // True when showing AP mode credentials screen
    char apSSID[DISPLAY_SSID_BUFFER_SIZE];      // AP mode SSID to display
    char apPassword[DISPLAY_PASS_BUFFER_SIZE];  // AP mode password to display
    uint8_t currentPage;                       // Active dashboard page
};

void displayInit();
void displaySetBrightness(int brightness);
void displayApplyBrightness(int brightness);
void displaySetClockSpriteAllowed(bool allowed);
void displaySuspendDynamicResources();
void displayResumeDynamicResources();
void displayUpdate();
void displayRenderClock();
void displayRenderAPMode();
void displayRenderImage(const char *path);
bool displayValidateImage(const char *path);
void displayInvalidateImage(const char *path);
void displayServiceTemporaryMessage();
void displayShowMessage(const String &msg);
void displayShowTemporaryMessage(const String &msg, uint32_t durationMs);
void displayShowAPScreen(const char* ssid, const char* password, const char* ip);
void displayShowDashboard();
void displayCycleNextPage(bool smoothTransition = false);
void displayToggleBacklight();

extern DisplayState displayState;
extern TFT_eSPI tft;
extern int scrollPos;

#endif
