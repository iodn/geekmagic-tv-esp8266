#include "settings.h"
#include <ctype.h>

#define EEPROM_SIZE 512
#define SETTINGS_MAGIC 0xCAFE
#define SETTINGS_ADDR 0
#define LEGACY_FIRMWARE_VERSION 2
#define BOOT_COUNTER_MAGIC 0xB007
#define BOOT_COUNTER_ADDR (SETTINGS_ADDR + sizeof(uint16_t) + sizeof(Settings))
#define BOOT_RECOVERY_THRESHOLD 2  // Enter recovery after 2 consecutive early boot failures
#define POWER_CYCLE_COUNTER_MAGIC 0x5C01  // 5C = "Power Cycle"
#define POWER_CYCLE_COUNTER_ADDR (BOOT_COUNTER_ADDR + sizeof(BootCounter))
#define POWER_CYCLE_THRESHOLD 5  // Factory reset after 5 quick power cycles
#define MIN_GMT_OFFSET (-12L * 3600L)
#define MAX_GMT_OFFSET (14L * 3600L)

namespace {

constexpr char kDeviceNamePrefix[] = "SmartClock";
constexpr char kHostnamePrefix[] = "smartclock";

struct LegacySettingsV2 {
    uint16_t version;
    int brightness;
    int theme;
    char reserved[64]; // Preserve EEPROM layout for existing settings.
    long gmtOffset;
    bool valid;
    uint32_t crc;
};

}  // namespace

// CRC32 lookup table for fast calculation
static const uint32_t crc32_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
};

static size_t boundedStringLength(const char *value, size_t maxLen) {
    size_t len = 0;

    while (len < maxLen && value[len] != '\0') {
        ++len;
    }

    return len;
}

static void copyBoundedString(const char *source,
                              size_t sourceSize,
                              char *destination,
                              size_t destinationSize) {
    if (destinationSize == 0) {
        return;
    }

    destination[0] = '\0';
    if (source == nullptr || sourceSize == 0) {
        return;
    }

    size_t sourceLength = boundedStringLength(source, sourceSize);
    if (sourceLength >= sourceSize) {
        sourceLength = sourceSize - 1;
    }

    size_t copyLength = min(sourceLength, destinationSize - 1);
    memcpy(destination, source, copyLength);
    destination[copyLength] = '\0';
}

void settingsDefaultDeviceName(char *buffer, size_t bufferSize) {
    if (bufferSize == 0) {
        return;
    }

    snprintf(buffer,
             bufferSize,
             "%s-%06X",
             kDeviceNamePrefix,
             static_cast<unsigned int>(ESP.getChipId() & 0xFFFFFFU));
    buffer[bufferSize - 1] = '\0';
}

void settingsSanitizeDeviceName(const char *source, char *buffer, size_t bufferSize) {
    if (bufferSize == 0) {
        return;
    }

    buffer[0] = '\0';
    if (source == nullptr) {
        settingsDefaultDeviceName(buffer, bufferSize);
        return;
    }

    size_t outputIndex = 0;
    bool lastWasSpace = false;
    for (size_t index = 0; source[index] != '\0' && outputIndex < bufferSize - 1; ++index) {
        unsigned char value = static_cast<unsigned char>(source[index]);
        if (isalnum(value) || value == '-' || value == '_') {
            buffer[outputIndex++] = static_cast<char>(value);
            lastWasSpace = false;
        } else if (value == ' ' || value == '.') {
            if (outputIndex > 0 && !lastWasSpace) {
                buffer[outputIndex++] = ' ';
                lastWasSpace = true;
            }
        }
    }

    while (outputIndex > 0 && buffer[outputIndex - 1] == ' ') {
        --outputIndex;
    }
    buffer[outputIndex] = '\0';

    if (outputIndex == 0) {
        settingsDefaultDeviceName(buffer, bufferSize);
    }
}

void settingsBuildHostname(const char *deviceName, char *buffer, size_t bufferSize) {
    if (bufferSize == 0) {
        return;
    }

    char safeName[SETTINGS_DEVICE_NAME_LENGTH];
    settingsSanitizeDeviceName(deviceName, safeName, sizeof(safeName));

    char slug[SETTINGS_DEVICE_NAME_LENGTH];
    size_t outputIndex = 0;
    bool lastWasDash = false;
    for (size_t index = 0; safeName[index] != '\0' && outputIndex < sizeof(slug) - 1; ++index) {
        unsigned char value = static_cast<unsigned char>(safeName[index]);
        if (isalnum(value)) {
            slug[outputIndex++] = static_cast<char>(tolower(value));
            lastWasDash = false;
        } else if (value == ' ' || value == '-' || value == '_') {
            if (outputIndex > 0 && !lastWasDash) {
                slug[outputIndex++] = '-';
                lastWasDash = true;
            }
        }
    }

    while (outputIndex > 0 && slug[outputIndex - 1] == '-') {
        --outputIndex;
    }
    slug[outputIndex] = '\0';

    if (slug[0] == '\0') {
        snprintf(buffer,
                 bufferSize,
                 "%s-%06x",
                 kHostnamePrefix,
                 static_cast<unsigned int>(ESP.getChipId() & 0xFFFFFFU));
    } else if (strcmp(slug, kHostnamePrefix) == 0) {
        snprintf(buffer,
                 bufferSize,
                 "%s-%06x",
                 kHostnamePrefix,
                 static_cast<unsigned int>(ESP.getChipId() & 0xFFFFFFU));
    } else if (strncmp(slug, "smartclock-", 11) == 0) {
        snprintf(buffer, bufferSize, "%s", slug);
    } else {
        snprintf(buffer, bufferSize, "%s-%s", kHostnamePrefix, slug);
    }

    buffer[bufferSize - 1] = '\0';
}

template <typename T>
static uint32_t calculateStructCRC(const T &settings) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *data = reinterpret_cast<const uint8_t*>(&settings);
    size_t len = sizeof(T) - sizeof(uint32_t);

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        crc = crc32_table[crc & 0x0F] ^ (crc >> 4);
        crc = crc32_table[crc & 0x0F] ^ (crc >> 4);
    }

    return ~crc;
}

static Settings normalizeSettingsForSave(const Settings &settings) {
    Settings normalized = {};
    normalized.version = FIRMWARE_VERSION;
    normalized.brightness = constrain(settings.brightness, 0, 100);
    normalized.theme = constrain(settings.theme, 0, 10);

    normalized.gmtOffset = constrain(settings.gmtOffset, MIN_GMT_OFFSET, MAX_GMT_OFFSET);
    char rawDeviceName[SETTINGS_DEVICE_NAME_LENGTH];
    copyBoundedString(settings.deviceName,
                      sizeof(settings.deviceName),
                      rawDeviceName,
                      sizeof(rawDeviceName));
    settingsSanitizeDeviceName(rawDeviceName, normalized.deviceName, sizeof(normalized.deviceName));
    normalized.valid = true;
    return normalized;
}

void settingsInit() {
    EEPROM.begin(EEPROM_SIZE);
}

// Calculate CRC32 checksum for settings (excluding the CRC field itself)
uint32_t settingsCalculateCRC(const Settings &settings) {
    return calculateStructCRC(settings);
}

static uint32_t legacySettingsCalculateCRC(const LegacySettingsV2 &settings) {
    return calculateStructCRC(settings);
}

static bool legacySettingsValidate(const LegacySettingsV2 &settings) {
    if (settings.version != LEGACY_FIRMWARE_VERSION) {
        Serial.printf("Legacy settings version mismatch: expected %d, got %d\n",
                      LEGACY_FIRMWARE_VERSION,
                      settings.version);
        return false;
    }

    uint32_t calculatedCRC = legacySettingsCalculateCRC(settings);
    if (calculatedCRC != settings.crc) {
        Serial.printf("Legacy settings CRC mismatch: expected 0x%08X, got 0x%08X\n",
                      calculatedCRC,
                      settings.crc);
        return false;
    }

    if (settings.brightness < 0 || settings.brightness > 100) {
        Serial.println(F("Legacy settings brightness out of range"));
        return false;
    }

    if (settings.theme < 0 || settings.theme > 10) {
        Serial.println(F("Legacy settings theme out of range"));
        return false;
    }

    if (settings.gmtOffset < MIN_GMT_OFFSET || settings.gmtOffset > MAX_GMT_OFFSET) {
        Serial.println(F("Legacy settings GMT offset out of range"));
        return false;
    }

    if (!settings.valid) {
        Serial.println(F("Legacy settings valid flag not set"));
        return false;
    }



    return true;
}

// Validate settings structure
bool settingsValidate(const Settings &settings) {
    // Check version compatibility
    if (settings.version != FIRMWARE_VERSION) {
        Serial.printf("Settings version mismatch: expected %d, got %d\n",
                      FIRMWARE_VERSION, settings.version);
        return false;
    }

    // Validate CRC
    uint32_t calculatedCRC = settingsCalculateCRC(settings);
    if (calculatedCRC != settings.crc) {
        Serial.printf("Settings CRC mismatch: expected 0x%08X, got 0x%08X\n",
                      calculatedCRC, settings.crc);
        return false;
    }

    // Sanity checks on values
    if (settings.brightness < 0 || settings.brightness > 100) {
        Serial.println(F("Settings brightness out of range"));
        return false;
    }

    if (settings.theme < 0 || settings.theme > 10) {
        Serial.println(F("Settings theme out of range"));
        return false;
    }

    if (settings.gmtOffset < MIN_GMT_OFFSET || settings.gmtOffset > MAX_GMT_OFFSET) {
        Serial.println(F("Settings GMT offset out of range"));
        return false;
    }

    if (!settings.valid) {
        Serial.println(F("Settings valid flag not set"));
        return false;
    }



    if (boundedStringLength(settings.deviceName, sizeof(settings.deviceName)) >= sizeof(settings.deviceName)) {
        Serial.println(F("Settings device name is not null-terminated"));
        return false;
    }

    if (settings.deviceName[0] == '\0') {
        Serial.println(F("Settings device name is empty"));
        return false;
    }

    return true;
}

// Reset settings to factory defaults
void settingsReset(Settings &settings) {
    Serial.println(F("Resetting settings to factory defaults"));

    memset(&settings, 0, sizeof(settings));
    settings.version = FIRMWARE_VERSION;
    settings.brightness = 70;
    settings.theme = 0;
    settings.gmtOffset = 0;         // Default to UTC for a neutral out-of-box setup
    settingsDefaultDeviceName(settings.deviceName, sizeof(settings.deviceName));
    settings.valid = true;
    settings.crc = settingsCalculateCRC(settings);
}

void settingsLoad(Settings &settings) {
    uint16_t magic;
    EEPROM.get(SETTINGS_ADDR, magic);

    if (magic == SETTINGS_MAGIC) {
        uint16_t storedVersion = 0;
        EEPROM.get(SETTINGS_ADDR + 2, storedVersion);

        if (storedVersion == FIRMWARE_VERSION) {
            EEPROM.get(SETTINGS_ADDR + 2, settings);

            if (!settingsValidate(settings)) {
                Serial.println(F("Settings validation failed - resetting to defaults"));
                settingsReset(settings);
                settingsSave(settings);
            } else {
                Serial.println(F("Settings loaded and validated successfully"));
            }
        } else if (storedVersion == LEGACY_FIRMWARE_VERSION) {
            LegacySettingsV2 legacySettings = {};
            EEPROM.get(SETTINGS_ADDR + 2, legacySettings);

            if (!legacySettingsValidate(legacySettings)) {
                Serial.println(F("Legacy settings validation failed - resetting to defaults"));
                settingsReset(settings);
                settingsSave(settings);
            } else {
                memset(&settings, 0, sizeof(settings));
                settings.version = FIRMWARE_VERSION;
                settings.brightness = legacySettings.brightness;
                settings.theme = legacySettings.theme;
                settings.gmtOffset = legacySettings.gmtOffset;
                settingsDefaultDeviceName(settings.deviceName, sizeof(settings.deviceName));
                settings.valid = true;
                settings = normalizeSettingsForSave(settings);
                settings.crc = settingsCalculateCRC(settings);
                settingsSave(settings);
                Serial.println(F("Migrated settings from version 2 to version 3"));
            }
        } else {
            Serial.printf("Unsupported settings version %u - resetting to defaults\n", storedVersion);
            settingsReset(settings);
            settingsSave(settings);
        }
    } else {
        Serial.println(F("No valid settings found - initializing defaults"));
        settingsReset(settings);
        settingsSave(settings);  // Save defaults on first boot
    }
}

bool settingsSave(const Settings &settings) {
    Settings tempSettings = normalizeSettingsForSave(settings);
    tempSettings.crc = settingsCalculateCRC(tempSettings);

    uint16_t magic = SETTINGS_MAGIC;
    uint16_t previousMagic;
    Settings previousSettings;
    EEPROM.get(SETTINGS_ADDR, previousMagic);
    EEPROM.get(SETTINGS_ADDR + 2, previousSettings);
    EEPROM.put(SETTINGS_ADDR, magic);
    EEPROM.put(SETTINGS_ADDR + 2, tempSettings);
    if (!EEPROM.commit()) {
        // A later boot-counter commit must not silently persist a failed settings save.
        EEPROM.put(SETTINGS_ADDR, previousMagic);
        EEPROM.put(SETTINGS_ADDR + 2, previousSettings);
        Serial.println(F("Settings save failed"));
        return false;
    }

    Serial.println(F("Settings saved with CRC validation"));
    return true;
}

// Boot counter functions for failure detection
void bootCounterInit() {
    // Boot counter is already initialized by EEPROM.begin()
    // Just increment the failure counter
    bootCounterIncrement();
}

uint8_t bootCounterGet() {
    BootCounter counter;
    uint16_t magic;

    EEPROM.get(BOOT_COUNTER_ADDR, magic);

    if (magic == BOOT_COUNTER_MAGIC) {
        EEPROM.get(BOOT_COUNTER_ADDR, counter);
        return counter.failCount;
    }

    return 0;
}

void bootCounterIncrement() {
    BootCounter counter;
    uint16_t magic;

    EEPROM.get(BOOT_COUNTER_ADDR, magic);

    if (magic == BOOT_COUNTER_MAGIC) {
        EEPROM.get(BOOT_COUNTER_ADDR, counter);
        counter.failCount++;
    } else {
        // Initialize boot counter
        counter.magic = BOOT_COUNTER_MAGIC;
        counter.failCount = 1;
        counter.lastBootTime = 0;
    }

    EEPROM.put(BOOT_COUNTER_ADDR, counter);
    EEPROM.commit();

    Serial.printf("Boot failure count: %d\n", counter.failCount);
}

void bootCounterReset() {
    BootCounter counter;
    counter.magic = BOOT_COUNTER_MAGIC;
    counter.failCount = 0;
    counter.lastBootTime = millis();

    EEPROM.put(BOOT_COUNTER_ADDR, counter);
    EEPROM.commit();

    Serial.println(F("Boot counter reset - successful boot"));
}

bool bootCounterShouldEnterRecovery() {
    uint8_t failCount = bootCounterGet();

    if (failCount >= BOOT_RECOVERY_THRESHOLD) {
        Serial.printf("RECOVERY: boot recovery threshold reached (%d failures)\n", failCount);
        return true;
    }

    return false;
}

// Power cycle counter functions for user-initiated factory reset
void powerCycleCounterInit(bool countThisBoot) {
    if (countThisBoot) {
        powerCycleCounterIncrement();
    } else if (powerCycleCounterGet() != 0) {
        powerCycleCounterReset();
        Serial.println(F("Power cycle counter cleared for non-manual reset"));
    } else {
        Serial.println(F("Power cycle counter unchanged for non-manual reset"));
    }
}

uint8_t powerCycleCounterGet() {
    PowerCycleCounter counter;
    uint16_t magic;

    EEPROM.get(POWER_CYCLE_COUNTER_ADDR, magic);

    if (magic == POWER_CYCLE_COUNTER_MAGIC) {
        EEPROM.get(POWER_CYCLE_COUNTER_ADDR, counter);
        return counter.cycleCount;
    }

    return 0;
}

void powerCycleCounterIncrement() {
    PowerCycleCounter counter;
    uint16_t magic;

    EEPROM.get(POWER_CYCLE_COUNTER_ADDR, magic);

    if (magic == POWER_CYCLE_COUNTER_MAGIC) {
        EEPROM.get(POWER_CYCLE_COUNTER_ADDR, counter);
        counter.cycleCount++;
    } else {
        // Initialize power cycle counter
        counter.magic = POWER_CYCLE_COUNTER_MAGIC;
        counter.cycleCount = 1;
    }

    EEPROM.put(POWER_CYCLE_COUNTER_ADDR, counter);
    EEPROM.commit();

    Serial.printf("Power cycle count: %d/%d\n", counter.cycleCount, POWER_CYCLE_THRESHOLD);
}

void powerCycleCounterReset() {
    PowerCycleCounter counter;
    counter.magic = POWER_CYCLE_COUNTER_MAGIC;
    counter.cycleCount = 0;

    EEPROM.put(POWER_CYCLE_COUNTER_ADDR, counter);
    EEPROM.commit();

    Serial.println(F("Power cycle counter reset"));
}

bool powerCycleCounterCheckReset() {
    uint8_t cycleCount = powerCycleCounterGet();

    if (cycleCount >= POWER_CYCLE_THRESHOLD) {
        Serial.printf("USER RESET: Power cycle threshold reached (%d cycles)\n", cycleCount);
        return true;
    }

    return false;
}
