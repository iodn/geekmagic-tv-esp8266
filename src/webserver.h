#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>
#include "display.h" // Include for display functions

void webserverInit();
void webserverHandle();
void webserverProcessPendingActions();
bool webserverHasPendingNetworkAction();
void webserverApplyEffectiveBrightness(bool syncPreference = false);
void handleRoot();

extern ESP8266WebServer server;
extern int currentBrightness;
extern int currentTheme;
extern String apPassword; // Random AP password generated at runtime
extern bool wifiFailsafeMode; // WiFi failsafe mode flag

#endif
