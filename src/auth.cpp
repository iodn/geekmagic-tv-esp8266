#include "auth.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <MD5Builder.h>

namespace {

struct AuthConfig {
    uint16_t version;
    char passwordMd5[33];
    char provisionedPassword[AUTH_MAX_PASSWORD_LENGTH + 1];
};

constexpr uint16_t kLegacyAuthConfigVersion = 1;

AuthConfig authConfig = {};
bool provisionedThisBoot = false;

bool buildTempPath(const char *path, char *tempPath, size_t tempPathSize) {
    int tempPathLength = snprintf(tempPath, tempPathSize, "%s.tmp", path);
    return tempPathLength > 0 && static_cast<size_t>(tempPathLength) < tempPathSize;
}

void copyString(char *destination, size_t destinationSize, const char *source) {
    if (destinationSize == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    snprintf(destination, destinationSize, "%s", source);
}

bool isHexCharacter(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

bool passwordHashValid(const char *value) {
    if (value == nullptr || strlen(value) != 32) {
        return false;
    }

    for (uint8_t index = 0; index < 32; ++index) {
        if (!isHexCharacter(value[index])) {
            return false;
        }
    }

    return true;
}

String computePasswordMd5(const String &password) {
    MD5Builder builder;
    builder.begin();
    builder.add(password);
    builder.calculate();
    return builder.toString();
}

bool passwordTextValid(const char *value) {
    if (value == nullptr) {
        return false;
    }

    size_t length = strlen(value);
    if (length < AUTH_MIN_PASSWORD_LENGTH || length > AUTH_MAX_PASSWORD_LENGTH) {
        return false;
    }

    for (size_t index = 0; index < length; ++index) {
        char character = value[index];
        if (character < 33 || character > 126) {
            return false;
        }
    }

    return true;
}

bool provisionedPasswordMatchesHash(const char *password, const char *hash) {
    if (password == nullptr || password[0] == '\0') {
        return false;
    }

    if (!passwordHashValid(hash)) {
        return false;
    }

    String candidateHash = computePasswordMd5(String(password));
    return candidateHash.equalsConstantTime(String(hash));
}

bool writeAuthConfig() {
    JsonDocument doc;
    doc["version"] = authConfig.version;
    doc["passwordMd5"] = authConfig.passwordMd5;
    if (authConfig.provisionedPassword[0] != '\0') {
        doc["provisionedPassword"] = authConfig.provisionedPassword;
    }

    if (doc.overflowed()) {
        return false;
    }

    char tempPath[40];
    if (!buildTempPath(AUTH_CONFIG_PATH, tempPath, sizeof(tempPath))) {
        return false;
    }

    LittleFS.remove(tempPath);
    File file = LittleFS.open(tempPath, "w");
    if (!file) {
        return false;
    }

    size_t expectedSize = measureJson(doc);
    size_t writtenSize = serializeJson(doc, file);
    file.flush();
    bool success = writtenSize == expectedSize && file.size() == expectedSize;
    file.close();
    if (!success) {
        LittleFS.remove(tempPath);
        return false;
    }

    if (LittleFS.exists(AUTH_CONFIG_PATH)) {
        LittleFS.remove(AUTH_CONFIG_PATH);
    }

    if (!LittleFS.rename(tempPath, AUTH_CONFIG_PATH)) {
        LittleFS.remove(tempPath);
        return false;
    }

    return true;
}

bool loadAuthConfig() {
    File file = LittleFS.open(AUTH_CONFIG_PATH, "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        return false;
    }

    uint16_t storedVersion = doc["version"] | 0;
    if (storedVersion != kLegacyAuthConfigVersion && storedVersion != AUTH_CONFIG_VERSION) {
        return false;
    }

    authConfig.version = AUTH_CONFIG_VERSION;
    copyString(authConfig.passwordMd5, sizeof(authConfig.passwordMd5), doc["passwordMd5"] | "");
    authConfig.provisionedPassword[0] = '\0';

    if (!passwordHashValid(authConfig.passwordMd5)) {
        return false;
    }

    if (storedVersion >= AUTH_CONFIG_VERSION) {
        copyString(authConfig.provisionedPassword,
                   sizeof(authConfig.provisionedPassword),
                   doc["provisionedPassword"] | "");

        if (authConfig.provisionedPassword[0] != '\0' &&
            (!passwordTextValid(authConfig.provisionedPassword) ||
             !provisionedPasswordMatchesHash(authConfig.provisionedPassword, authConfig.passwordMd5))) {
            authConfig.provisionedPassword[0] = '\0';
        }
    }

    return true;
}

String generateDefaultPassword() {
    const char digits[] = "0123456789";
    String password;
    password.reserve(10);

    randomSeed(ESP.getChipId() ^ micros() ^ ESP.getCycleCount());
    for (uint8_t index = 0; index < 10; ++index) {
        password += digits[random(0, static_cast<int>(sizeof(digits) - 1))];
    }

    return password;
}

bool provisionFreshPassword() {
    memset(&authConfig, 0, sizeof(authConfig));
    authConfig.version = AUTH_CONFIG_VERSION;

    String password = generateDefaultPassword();
    copyString(authConfig.provisionedPassword, sizeof(authConfig.provisionedPassword), password.c_str());
    copyString(authConfig.passwordMd5, sizeof(authConfig.passwordMd5), computePasswordMd5(password).c_str());
    provisionedThisBoot = true;
    return writeAuthConfig();
}

}  // namespace

void authInit() {
    memset(&authConfig, 0, sizeof(authConfig));
    provisionedThisBoot = false;

    if (loadAuthConfig()) {
        return;
    }

    provisionFreshPassword();
}

bool authValidatePasswordPolicy(const String &password, String *error) {
    if (password.length() < AUTH_MIN_PASSWORD_LENGTH || password.length() > AUTH_MAX_PASSWORD_LENGTH) {
        if (error != nullptr) {
            *error = "Use 8 to 32 characters";
        }
        return false;
    }

    if (!passwordTextValid(password.c_str())) {
        if (error != nullptr) {
            *error = "Use printable ASCII without spaces";
        }
        return false;
    }

    if (error != nullptr) {
        *error = "";
    }
    return true;
}

bool authVerifyPassword(const String &password) {
    if (!passwordHashValid(authConfig.passwordMd5)) {
        return false;
    }

    String candidateHash = computePasswordMd5(password);
    return candidateHash.equalsConstantTime(String(authConfig.passwordMd5));
}

bool authUpdatePassword(const String &currentPassword, const String &newPassword, String *error) {
    if (!authVerifyPassword(currentPassword)) {
        if (error != nullptr) {
            *error = "Current password is incorrect";
        }
        return false;
    }

    if (!authValidatePasswordPolicy(newPassword, error)) {
        return false;
    }

    authConfig.version = AUTH_CONFIG_VERSION;
    copyString(authConfig.passwordMd5, sizeof(authConfig.passwordMd5), computePasswordMd5(newPassword).c_str());
    authConfig.provisionedPassword[0] = '\0';
    if (!writeAuthConfig()) {
        if (error != nullptr) {
            *error = "Failed to save password";
        }
        return false;
    }

    provisionedThisBoot = false;
    if (error != nullptr) {
        *error = "";
    }
    return true;
}

const char* authUsername() {
    return "admin";
}

const char* authOtaPasswordHash() {
    return authConfig.passwordMd5;
}

bool authWasProvisionedThisBoot() {
    return provisionedThisBoot;
}

bool authCanRevealPassword() {
    return authConfig.provisionedPassword[0] != '\0';
}

const char* authProvisionedPassword() {
    return authConfig.provisionedPassword;
}
