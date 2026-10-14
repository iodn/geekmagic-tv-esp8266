#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#define LOG_BUFFER_SIZE 16
#define LOG_LINE_LENGTH 96

void loggerInit();
void logPrint(const String &msg);
void logPrintf(const char* format, ...);
void logPrintf(const __FlashStringHelper* format, ...);
String logGetAll();

#endif
