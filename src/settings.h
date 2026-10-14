#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <EEPROM.h>

// EEPROM layout version - increment when Settings structure changes
#define FIRMWARE_VERSION 3
#define SETTINGS_DEVICE_NAME_LENGTH 32
#define SETTINGS_HOSTNAME_LENGTH 32

// Semantic firmware release version
#ifndef FIRMWARE_VERSION_STRING
#define FIRMWARE_VERSION_STRING "2.3.1"
#endif

struct Settings {
    uint16_t version;          // Firmware version for compatibility check
    int brightness;
    int theme;
    char reserved[64]; // Preserve EEPROM layout for existing settings.
    long gmtOffset;            // GMT offset in seconds
    char deviceName[SETTINGS_DEVICE_NAME_LENGTH];
    bool valid;
    uint32_t crc;              // CRC32 checksum for data integrity
};

// Boot failure tracking structure (separate from settings)
struct BootCounter {
    uint16_t magic;            // Magic number to validate boot counter
    uint8_t failCount;         // Number of consecutive boot failures
    uint32_t lastBootTime;     // Timestamp of last successful boot
};

// Power cycle reset structure (user-initiated factory reset)
struct PowerCycleCounter {
    uint16_t magic;            // Magic number to validate power cycle counter (0x5C01)
    uint8_t cycleCount;        // Number of quick power cycles
};

void settingsInit();
void settingsLoad(Settings &settings);
bool settingsSave(const Settings &settings);
void settingsReset(Settings &settings);
uint32_t settingsCalculateCRC(const Settings &settings);
bool settingsValidate(const Settings &settings);
void settingsDefaultDeviceName(char *buffer, size_t bufferSize);
void settingsSanitizeDeviceName(const char *source, char *buffer, size_t bufferSize);
void settingsBuildHostname(const char *deviceName, char *buffer, size_t bufferSize);

// Boot counter functions
void bootCounterInit();
uint8_t bootCounterGet();
void bootCounterIncrement();
void bootCounterReset();
bool bootCounterShouldEnterRecovery();

// Power cycle counter functions (user-initiated factory reset)
void powerCycleCounterInit(bool countThisBoot);
uint8_t powerCycleCounterGet();
void powerCycleCounterIncrement();
void powerCycleCounterReset();
bool powerCycleCounterCheckReset();

#endif
