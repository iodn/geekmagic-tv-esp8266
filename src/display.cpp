#include "display.h"

#include "auth.h"
#include "config.h"
#include "feeds.h"
#include "logger.h"
#include <ESP8266WiFi.h>
#include <time.h>
#include <math.h>
#include <vector>

#define FONT_INFO 1
#define FONT_LABEL 2
#define FONT_BODY 4
#define FONT_TITLE 6
#define FONT_HUGE 7

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clockDynamicSprite = TFT_eSprite(&tft);
DisplayState displayState;
int scrollPos = 240;

namespace {

constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
}

constexpr int kPageMargin = 4;
constexpr int kPageWidth = DISPLAY_WIDTH - (kPageMargin * 2);
constexpr int kHeaderTitleY = 10;
constexpr int kHeaderSubtitleY = 12;
constexpr int kHeaderRuleY = 32;
constexpr int kContentTopY = 40;
constexpr int kClockDynamicRegionX = 12;
constexpr int kClockDynamicRegionY = 44;
constexpr int kClockDynamicRegionWidth = DISPLAY_WIDTH - (kClockDynamicRegionX * 2);
constexpr int kClockDynamicRegionHeight = 64;
constexpr int kClockTimeY = 54;
constexpr int kClockMetaY = 14;
constexpr int kClockFooterY = 148;
constexpr int kClockFooterHeight = 78;
// The 1 bpp buffer takes about 1.7 KB; keep at least 8 KB for request handling.
constexpr uint32_t kClockSpriteMinFreeHeapBytes = 10000UL;
constexpr uint32_t kClockSpriteRetryIntervalMs = 5000UL;
constexpr uint8_t kPageTransitionFadeDownSteps = 2;
constexpr uint8_t kPageTransitionFadeUpSteps = 3;
constexpr uint16_t kPageTransitionFadeStepDelayMs = 12;
constexpr uint8_t kPageTransitionDimPercent = 82;
constexpr uint8_t kPageTransitionMinimumBrightness = 18;
constexpr size_t kTemporaryMessageBufferSize = 96;

struct ThemePalette {
    uint16_t background;
    uint16_t surface;
    uint16_t surfaceAlt;
    uint16_t accent;
    uint16_t accentSoft;
    uint16_t text;
    uint16_t muted;
    uint16_t positive;
    uint16_t negative;
    uint16_t warning;
};

struct Rgb888 {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct ThemeSelection {
    uint8_t theme;
    bool customThemeEnabled;
    const char *background;
    const char *surface;
    const char *accent;
    const char *text;
};

#include "device_theme_palettes.h"

int savedBrightness = 100;
int appliedBrightness = -1;
bool backlightOn = true;

enum DisplayRenderMode : uint8_t {
    DISPLAY_MODE_DASHBOARD = 0,
    DISPLAY_MODE_TEMP_MESSAGE,
    DISPLAY_MODE_AP,
};

struct RenderCache {
    bool valid;
    uint8_t mode;
    uint8_t page;
    uint8_t theme;
    uint32_t staticHash;
    uint32_t dynamicHash;
};

RenderCache renderCache = {false, DISPLAY_MODE_DASHBOARD, 0, 0, 0, 0};

struct ClockMetaCache {
    bool valid;
    char metaLine[40];
};

ClockMetaCache clockMetaCache = {false, {0}};
char footerClockCache[16] = {0};
bool clockDynamicSpriteReady = false;
bool clockDynamicSpriteAttempted = false;
uint32_t clockDynamicSpriteLastAttemptMs = 0;
bool clockDynamicSpriteAllowed = false;
bool resumeClockSpriteAfterDynamicSuspend = false;
uint8_t dynamicResourceSuspendDepth = 0;
uint8_t selectedItemPage = DASHBOARD_PAGE_COUNT;
uint8_t selectedItemIndex = 0;

uint8_t dashboardItemCount(uint8_t pageId);
uint8_t dashboardItemSlot(uint8_t pageId);

struct TemporaryMessageState {
    bool active;
    unsigned long expiresAtMs;
    char message[kTemporaryMessageBufferSize];
};

TemporaryMessageState temporaryMessage = {false, 0, {0}};

constexpr uint32_t kFnvOffset = 2166136261UL;
constexpr uint32_t kFnvPrime = 16777619UL;

void hashBytes(uint32_t &hash, const uint8_t *data, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        hash ^= data[index];
        hash *= kFnvPrime;
    }
}

template <typename TValue>
void hashValue(uint32_t &hash, const TValue &value) {
    hashBytes(hash, reinterpret_cast<const uint8_t*>(&value), sizeof(TValue));
}

void hashCString(uint32_t &hash, const char *value) {
    if (value == nullptr) {
        const uint8_t zero = 0;
        hashBytes(hash, &zero, sizeof(zero));
        return;
    }

    hashBytes(hash, reinterpret_cast<const uint8_t*>(value), strlen(value));
    const uint8_t terminator = 0;
    hashBytes(hash, &terminator, sizeof(terminator));
}

void invalidateRenderCache() {
    renderCache.valid = false;
    clockMetaCache.valid = false;
    footerClockCache[0] = '\0';
    clockMetaCache.metaLine[0] = '\0';
}

void clearTemporaryMessage() {
    temporaryMessage.active = false;
    temporaryMessage.expiresAtMs = 0;
    temporaryMessage.message[0] = '\0';
}

bool temporaryMessageExpired() {
    return temporaryMessage.active &&
           static_cast<long>(millis() - temporaryMessage.expiresAtMs) >= 0;
}

bool temporaryMessageVisible() {
    return temporaryMessage.active &&
           temporaryMessage.message[0] != '\0' &&
           !temporaryMessageExpired();
}

Rgb888 rgb565ToRgb888(uint16_t color) {
    Rgb888 rgb;
    rgb.red = static_cast<uint8_t>(((color >> 11) & 0x1F) * 255 / 31);
    rgb.green = static_cast<uint8_t>(((color >> 5) & 0x3F) * 255 / 63);
    rgb.blue = static_cast<uint8_t>((color & 0x1F) * 255 / 31);
    return rgb;
}

uint16_t rgb888ToRgb565(const Rgb888 &rgb) {
    return rgb565(rgb.red, rgb.green, rgb.blue);
}

bool parseHexColor888(const char *value, Rgb888 &rgb) {
    if (value == nullptr) {
        return false;
    }

    const char *source = value[0] == '#' ? value + 1 : value;
    if (strlen(source) != 6) {
        return false;
    }

    char *end = nullptr;
    unsigned long raw = strtoul(source, &end, 16);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    rgb.red = static_cast<uint8_t>((raw >> 16) & 0xFF);
    rgb.green = static_cast<uint8_t>((raw >> 8) & 0xFF);
    rgb.blue = static_cast<uint8_t>(raw & 0xFF);
    return true;
}

Rgb888 blendRgb(const Rgb888 &base, const Rgb888 &mix, uint8_t mixWeightPercent) {
    uint16_t baseWeight = 100U - constrain(mixWeightPercent, 0, 100);
    uint16_t mixWeight = constrain(mixWeightPercent, 0, 100);

    Rgb888 result;
    result.red = static_cast<uint8_t>((base.red * baseWeight + mix.red * mixWeight) / 100U);
    result.green = static_cast<uint8_t>((base.green * baseWeight + mix.green * mixWeight) / 100U);
    result.blue = static_cast<uint8_t>((base.blue * baseWeight + mix.blue * mixWeight) / 100U);
    return result;
}

ThemeSelection effectiveThemeSelection() {
    if (dashboardNightModeActive() && dashboardConfig.nightThemeEnabled) {
        return {
            dashboardConfig.nightTheme,
            dashboardConfig.nightCustomThemeEnabled,
            dashboardConfig.nightCustomBackground,
            dashboardConfig.nightCustomSurface,
            dashboardConfig.nightCustomAccent,
            dashboardConfig.nightCustomText
        };
    }

    return {
        dashboardConfig.theme,
        dashboardConfig.customThemeEnabled,
        dashboardConfig.customBackground,
        dashboardConfig.customSurface,
        dashboardConfig.customAccent,
        dashboardConfig.customText
    };
}

uint8_t activeThemeIdValue() {
    ThemeSelection selection = effectiveThemeSelection();
    return constrain(selection.theme, 0, DASHBOARD_THEME_COUNT - 1);
}

float themeLuminance(uint16_t color) {
    Rgb888 rgb = rgb565ToRgb888(color);
    const uint8_t channels[] = {rgb.red, rgb.green, rgb.blue};
    const float weights[] = {0.2126f, 0.7152f, 0.0722f};
    float luminance = 0;
    for (uint8_t index = 0; index < 3; ++index) {
        float value = channels[index] / 255.0f;
        luminance += weights[index] * (value <= 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f));
    }
    return luminance;
}

uint16_t readableDerivedColor(uint16_t color, uint16_t background) {
    float backgroundLuminance = themeLuminance(background);
    Rgb888 original = rgb565ToRgb888(color);
    // Pick the endpoint with the strongest contrast, then retain as much hue as possible.
    Rgb888 endpoint = backgroundLuminance < 0.179f ? Rgb888{255, 255, 255} : Rgb888{0, 0, 0};
    for (uint8_t weight = 0; weight <= 100; weight += 4) {
        uint16_t candidate = rgb888ToRgb565(blendRgb(original, endpoint, weight));
        float luminance = themeLuminance(candidate);
        float contrast = (max(luminance, backgroundLuminance) + 0.05f) / (min(luminance, backgroundLuminance) + 0.05f);
        if (contrast >= 4.5f) return candidate;
    }
    return rgb888ToRgb565(endpoint);
}

ThemePalette buildCustomTheme(const ThemePalette &base,
                              const char *backgroundColor,
                              const char *surfaceColor,
                              const char *accentColor,
                              const char *textColor) {
    Rgb888 background = rgb565ToRgb888(base.background);
    Rgb888 surface = rgb565ToRgb888(base.surface);
    Rgb888 accent = rgb565ToRgb888(base.accent);
    Rgb888 text = rgb565ToRgb888(base.text);

    parseHexColor888(backgroundColor, background);
    parseHexColor888(surfaceColor, surface);
    parseHexColor888(accentColor, accent);
    parseHexColor888(textColor, text);

    ThemePalette theme = {};
    theme.background = rgb888ToRgb565(background);
    theme.surface = rgb888ToRgb565(surface);
    theme.surfaceAlt = rgb888ToRgb565(blendRgb(surface, background, 22));
    theme.accent = rgb888ToRgb565(accent);
    theme.accentSoft = rgb888ToRgb565(blendRgb(surface, accent, 28));
    theme.text = rgb888ToRgb565(text);
    theme.muted = rgb888ToRgb565(blendRgb(text, background, 52));
    theme.muted = readableDerivedColor(theme.muted, theme.background);
    theme.positive = readableDerivedColor(base.positive, theme.background);
    theme.negative = readableDerivedColor(base.negative, theme.background);
    theme.warning = readableDerivedColor(base.warning, theme.background);
    return theme;
}

void hashThemeConfig(uint32_t &hash) {
    ThemeSelection selection = effectiveThemeSelection();
    hashValue(hash, selection.theme);
    hashValue(hash, selection.customThemeEnabled);
    if (!selection.customThemeEnabled) {
        return;
    }

    hashCString(hash, selection.background);
    hashCString(hash, selection.surface);
    hashCString(hash, selection.accent);
    hashCString(hash, selection.text);
}



const ThemePalette& activeTheme() {
    static ThemePalette customTheme;
    static uint32_t customThemeHash = 0;
    static bool customThemeValid = false;
    ThemeSelection selection = effectiveThemeSelection();
    uint8_t themeIndex = constrain(selection.theme, 0, DASHBOARD_THEME_COUNT - 1);
    if (!selection.customThemeEnabled) {
        return kThemes[themeIndex];
    }

    uint32_t hash = kFnvOffset;
    hashThemeConfig(hash);
    if (customThemeValid && hash == customThemeHash) return customTheme;

    customTheme = buildCustomTheme(kThemes[themeIndex],
                                   selection.background,
                                   selection.surface,
                                   selection.accent,
                                   selection.text);
    customThemeHash = hash;
    customThemeValid = true;
    return customTheme;
}

String safeCString(const char *value) {
    return (value != nullptr) ? String(value) : String("");
}

bool hasTimeSync() {
    return dashboardCurrentEpoch() != 0;
}

String trimTrailingZeros(float value, uint8_t precision = 2) {
    String result(value, precision);
    while (result.indexOf('.') >= 0 && result.endsWith("0")) {
        result.remove(result.length() - 1);
    }
    if (result.endsWith(".")) {
        result.remove(result.length() - 1);
    }
    return result;
}

String groupThousands(const String &value) {
    int decimalIndex = value.indexOf('.');
    if (decimalIndex < 0) {
        decimalIndex = value.length();
    }

    int startIndex = value.startsWith("-") ? 1 : 0;
    if (decimalIndex - startIndex <= 3) {
        return value;
    }

    String grouped;
    grouped.reserve(value.length() + ((decimalIndex - startIndex - 1) / 3));
    if (startIndex == 1) {
        grouped += '-';
    }

    for (int index = startIndex; index < static_cast<int>(value.length()); ++index) {
        grouped += value.charAt(index);

        if (index < decimalIndex - 1) {
            int remainingDigits = decimalIndex - index - 1;
            if (remainingDigits > 0 && (remainingDigits % 3) == 0) {
                grouped += ',';
            }
        }
    }

    return grouped;
}

String formatTimeForTm(const tm &timeInfo, bool showSeconds, bool use24Hour, bool *isPm = nullptr) {
    char buffer[16];
    const char *format = nullptr;

    if (use24Hour) {
        format = showSeconds ? "%H:%M:%S" : "%H:%M";
    } else {
        format = showSeconds ? "%I:%M:%S" : "%I:%M";
        if (isPm != nullptr) {
            *isPm = timeInfo.tm_hour >= 12;
        }
    }

    strftime(buffer, sizeof(buffer), format, &timeInfo);
    return String(buffer);
}

String formatLocalTime(bool showSeconds, bool use24Hour, bool *isPm = nullptr) {
    if (!hasTimeSync()) {
        if (isPm != nullptr) {
            *isPm = false;
        }
        return showSeconds ? "--:--:--" : "--:--";
    }

    time_t now = time(nullptr);
    tm localTimeInfo;
    localtime_r(&now, &localTimeInfo);
    return formatTimeForTm(localTimeInfo, showSeconds, use24Hour, isPm);
}

String formatLocalDate() {
    if (!hasTimeSync()) {
        return "Waiting for time";
    }

    char buffer[32];
    time_t now = time(nullptr);
    tm localTimeInfo;
    localtime_r(&now, &localTimeInfo);
    strftime(buffer, sizeof(buffer), "%a %d %b %Y", &localTimeInfo);
    return String(buffer);
}

String formatWorldTime(long offsetSeconds) {
    if (!hasTimeSync()) {
        return "--:--";
    }

    time_t now = time(nullptr) + offsetSeconds;
    tm utcTimeInfo;
    gmtime_r(&now, &utcTimeInfo);
    return formatTimeForTm(utcTimeInfo, false, dashboardConfig.use24Hour);
}

String formatDuration(int32_t totalSeconds) {
    int32_t boundedSeconds = (totalSeconds > 0) ? totalSeconds : 0;
    int32_t hours = boundedSeconds / 3600;
    int32_t minutes = (boundedSeconds % 3600) / 60;
    int32_t seconds = boundedSeconds % 60;

    char buffer[16];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%ld:%02ld", static_cast<long>(hours), static_cast<long>(minutes));
    } else {
        snprintf(buffer, sizeof(buffer), "%02ld:%02ld", static_cast<long>(minutes), static_cast<long>(seconds));
    }
    return String(buffer);
}

int focusProgressWidth(int32_t remainingSeconds) {
    uint32_t totalSeconds = static_cast<uint32_t>(dashboardData.focus.durationMinutes) * 60UL;
    if (totalSeconds == 0) return 0;
    uint32_t remaining = remainingSeconds > 0 ? static_cast<uint32_t>(remainingSeconds) : 0U;
    uint32_t elapsed = remaining < totalSeconds ? totalSeconds - remaining : 0U;
    return static_cast<int>((elapsed * 184UL) / totalSeconds);
}

String formatRelativeCountdown(int32_t totalSeconds) {
    int32_t boundedSeconds = (totalSeconds > 0) ? totalSeconds : 0;
    if (boundedSeconds <= 0) {
        return "Now";
    }

    int32_t hours = boundedSeconds / 3600;
    int32_t minutes = (boundedSeconds % 3600) / 60;

    if (hours > 0) {
        return "in " + String(hours) + "h " + String(minutes) + "m";
    }

    return "in " + String((minutes > 1) ? minutes : 1) + "m";
}

String formatUtcOffset(long offsetSeconds) {
    long absoluteSeconds = labs(offsetSeconds);
    long hours = absoluteSeconds / 3600;
    long minutes = (absoluteSeconds % 3600) / 60;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "UTC%c%02ld:%02ld",
             offsetSeconds >= 0 ? '+' : '-',
             hours,
             minutes);
    return String(buffer);
}

String formatClockMetaLine(bool isPm) {
    String metaLine = formatLocalDate();
    if (!dashboardConfig.use24Hour && hasTimeSync()) {
        metaLine += isPm ? "  PM" : "  AM";
    }
    return metaLine;
}

void updateClockMetaCache(const String &metaLine) {
    strncpy(clockMetaCache.metaLine, metaLine.c_str(), sizeof(clockMetaCache.metaLine) - 1);
    clockMetaCache.metaLine[sizeof(clockMetaCache.metaLine) - 1] = '\0';
    clockMetaCache.valid = true;
}

bool fontSupportsText(int font, const String &text) {
    if (font != FONT_TITLE && font != FONT_HUGE) {
        return true;
    }
    for (size_t index = 0; index < text.length(); ++index) {
        char character = text[index];
        if ((character >= '0' && character <= '9') ||
            character == ' ' || character == ':' || character == '.' || character == '-') {
            continue;
        }
        if (font == FONT_TITLE &&
            (character == 'a' || character == 'p' || character == 'm' || character == '|')) {
            continue;
        }
        return false;
    }
    return true;
}

template <typename TCanvas>
int chooseCanvasFittingFont(TCanvas &canvas,
                            const String &text,
                            int maxWidth,
                            const int *fontCandidates,
                            size_t fontCandidateCount,
                            int maxHeight = DISPLAY_HEIGHT) {
    if (fontCandidates == nullptr || fontCandidateCount == 0) {
        return FONT_INFO;
    }

    for (size_t index = 0; index < fontCandidateCount; ++index) {
        int font = fontCandidates[index];
        if (fontSupportsText(font, text) && canvas.textWidth(text, font) <= maxWidth &&
            canvas.fontHeight(font) <= maxHeight) {
            return font;
        }
    }

    return FONT_INFO;
}

template <typename TCanvas>
void drawClockTime(TCanvas &canvas,
                   const String &timeText,
                   bool showSeconds,
                   uint16_t primaryColor,
                   uint16_t secondaryColor,
                   uint16_t backgroundColor,
                   int centerX,
                   int topY) {
    (void)secondaryColor;
    const int timeFonts[] = {FONT_HUGE, FONT_TITLE, FONT_BODY};
    int maxWidth = max(0, (centerX * 2) - (showSeconds ? 8 : 20));
    int timeFont = chooseCanvasFittingFont(canvas,
                                           timeText,
                                           maxWidth,
                                           timeFonts,
                                           sizeof(timeFonts) / sizeof(timeFonts[0]));

    canvas.setTextDatum(TL_DATUM);
    canvas.setTextFont(FONT_HUGE);
    int referenceHeight = canvas.fontHeight();

    canvas.setTextColor(primaryColor, backgroundColor);
    canvas.setTextFont(timeFont);
    int textWidth = canvas.textWidth(timeText, timeFont);
    int textHeight = canvas.fontHeight();
    int adjustedTopY = topY + max(0, (referenceHeight - textHeight) / 2);
    int startX = centerX - (textWidth / 2);
    canvas.drawString(timeText, startX, adjustedTopY, timeFont);
}

String headerSubtitle() {
    if (displayState.apMode && displayState.ipInfo[0] != '\0') {
        return safeCString(displayState.ipInfo);
    }

    if (!dashboardConfig.showIp) {
        return "";
    }

    if (displayState.ipInfo[0] != '\0') {
        return safeCString(displayState.ipInfo);
    }

    return "";
}

std::vector<String> wrapText(const String &text, int font, int maxWidth) {
    std::vector<String> lines;

    if (text.isEmpty()) {
        lines.push_back("");
        return lines;
    }

    tft.setTextFont(font);
    String currentLine;
    String currentWord;
    currentLine.reserve(text.length() + 4);
    currentWord.reserve(text.length() + 4);

    auto flushWord = [&]() {
        if (currentWord.isEmpty()) {
            return;
        }

        String candidate = currentLine;
        if (!candidate.isEmpty()) {
            candidate += " ";
        }
        candidate += currentWord;

        if (tft.textWidth(candidate) > maxWidth) {
            if (!currentLine.isEmpty()) {
                lines.push_back(currentLine);
            }
            currentLine = "";
            for (size_t index = 0; index < currentWord.length(); ++index) {
                char character = currentWord.charAt(index);
                if (!currentLine.isEmpty() && tft.textWidth(currentLine + character) > maxWidth) {
                    lines.push_back(currentLine);
                    currentLine = "";
                }
                currentLine += character;
            }
        } else {
            currentLine = candidate;
        }

        currentWord = "";
    };

    for (size_t index = 0; index < text.length(); ++index) {
        char character = text.charAt(index);

        if (character == '\n') {
            flushWord();
            lines.push_back(currentLine);
            currentLine = "";
        } else if (character == ' ') {
            flushWord();
        } else {
            currentWord += character;
        }
    }

    flushWord();
    if (!currentLine.isEmpty()) {
        lines.push_back(currentLine);
    }

    if (lines.empty()) {
        lines.push_back("");
    }

    return lines;
}

void drawRoundedPanel(int x, int y, int width, int height, uint16_t fillColor, uint16_t borderColor) {
    tft.fillRoundRect(x, y, width, height, 18, fillColor);
    tft.drawRoundRect(x, y, width, height, 18, borderColor);
}

void drawBadge(int x, int y, int width, int height, const String &label, uint16_t fillColor, uint16_t textColor) {
    tft.fillRoundRect(x, y, width, height, height / 2, fillColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(FONT_LABEL);
    tft.setTextColor(textColor, fillColor);
    tft.drawString(label, x + (width / 2), y + (height / 2), FONT_LABEL);
}

void drawWrappedCenteredText(const String &text,
                             int centerX,
                             int startY,
                             int maxWidth,
                             int font,
                             uint16_t textColor,
                             uint16_t backgroundColor,
                             int lineSpacing = 4,
                             int maxHeight = DISPLAY_HEIGHT) {
    std::vector<String> lines = wrapText(text, font, maxWidth);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(font);
    tft.setTextColor(textColor, backgroundColor);
    int fontHeight = tft.fontHeight();
    int lineHeight = fontHeight + lineSpacing;
    int availableHeight = min(maxHeight, DISPLAY_HEIGHT - startY);
    int lineCount = availableHeight < fontHeight ? 0 : 1 + (availableHeight - fontHeight) / lineHeight;
    size_t visibleLines = min(lines.size(), static_cast<size_t>(lineCount));
    if (visibleLines < lines.size() && visibleLines > 0) {
        String &lastLine = lines[visibleLines - 1];
        while (!lastLine.isEmpty() && tft.textWidth(lastLine + "...") > maxWidth) {
            lastLine.remove(lastLine.length() - 1);
        }
        lastLine += "...";
    }

    for (size_t lineIndex = 0; lineIndex < visibleLines; ++lineIndex) {
        tft.drawString(lines[lineIndex], centerX, startY + (lineIndex * lineHeight), font);
    }
}

int chooseFittingFont(const String &text, int maxWidth, const int *fontCandidates, size_t fontCandidateCount,
                      int maxHeight = DISPLAY_HEIGHT) {
    return chooseCanvasFittingFont(tft, text, maxWidth, fontCandidates, fontCandidateCount, maxHeight);
}

void drawAdaptiveText(const String &text,
                      int x,
                      int y,
                      int maxWidth,
                      uint8_t datum,
                      const int *fontCandidates,
                      size_t fontCandidateCount,
                      uint16_t textColor,
                      uint16_t backgroundColor) {
    int font = chooseFittingFont(text, maxWidth, fontCandidates, fontCandidateCount);
    tft.setTextDatum(datum);
    tft.setTextFont(font);
    tft.setTextColor(textColor, backgroundColor);
    tft.drawString(text, x, y, font);
}

String fitDisplayLabel(const String &text, int width, int font = FONT_LABEL) {
    String result = text;
    if (tft.textWidth(result, font) <= width) return result;
    while (!result.isEmpty() && tft.textWidth(result + "...", font) > width) {
        result.remove(result.length() - 1);
    }
    return result + "...";
}

void drawDisplayLabel(const String &text, int x, int y, int width, uint8_t datum,
                      uint16_t color, int font = FONT_LABEL) {
    tft.setTextDatum(datum);
    tft.setTextFont(font);
    tft.setTextColor(color, activeTheme().background);
    tft.drawString(fitDisplayLabel(text, width, font), x, y, font);
}

void drawHeroText(const String &text, int topY, int height, uint16_t color,
                  uint8_t maximumSize = 2) {
    int size = maximumSize;
    while (size > 1) {
        tft.setTextSize(size);
        if (tft.textWidth(text, FONT_BODY) <= 224 && tft.fontHeight(FONT_BODY) <= height) break;
        --size;
    }
    tft.setTextSize(size);
    String visible = fitDisplayLabel(text, 224, FONT_BODY);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_BODY);
    tft.setTextColor(color, activeTheme().background);
    tft.drawString(visible, 120, topY + max(0, (height - tft.fontHeight()) / 2), FONT_BODY);
    tft.setTextSize(1);
}

void drawItemPosition() {
    uint8_t count = dashboardItemCount(displayState.currentPage);
    if (count > 1) {
        drawDisplayLabel(String(selectedItemIndex + 1) + "/" + String(count),
                         232, 4, 48, TR_DATUM, activeTheme().muted);
    }
}

String footerClockText() {
    bool isPm = false;
    String text = formatLocalTime(false, dashboardConfig.use24Hour, &isPm);
    if (!dashboardConfig.use24Hour && hasTimeSync()) text += isPm ? " PM" : " AM";
    return text;
}

void updateFooterClock() {
    if (displayState.currentPage == DASHBOARD_PAGE_CLOCK && !temporaryMessageVisible()) return;
    String text = footerClockText();
    if (strcmp(footerClockCache, text.c_str()) == 0) return;
    const ThemePalette &theme = activeTheme();
    tft.setTextSize(1);
    const int fonts[] = {FONT_BODY, FONT_LABEL};
    int font = chooseFittingFont(text, 84, fonts, 2, 26);
    tft.fillRect(8, 212, 84, 28, theme.background);
    int y = 212 + max(0, (26 - tft.fontHeight(font)) / 2);
    drawDisplayLabel(text, 8, y, 84, TL_DATUM, theme.text, font);
    strncpy(footerClockCache, text.c_str(), sizeof(footerClockCache) - 1);
    footerClockCache[sizeof(footerClockCache) - 1] = '\0';
}

void drawScreenChrome(const String &title) {
    const ThemePalette &theme = activeTheme();
    tft.setTextSize(1);
    tft.fillScreen(theme.background);
    footerClockCache[0] = '\0';
    if (displayState.currentPage == DASHBOARD_PAGE_CLOCK && !temporaryMessageVisible()) {
        drawDisplayLabel(title, 8, 220, 84, TL_DATUM, theme.accent);
    } else {
        updateFooterClock();
    }
    String address = headerSubtitle();
    if (!address.isEmpty()) {
        drawDisplayLabel(address, 232, 220, 132, TR_DATUM, theme.muted);
    }
}

void drawPlaceholder(const String &title, const String &message) {
    const ThemePalette &theme = activeTheme();
    drawScreenChrome(title);
    drawWrappedCenteredText("Nothing here yet", 120, 48, 224, FONT_BODY,
                            theme.text, theme.background, 4, 60);
    drawWrappedCenteredText(message, 120, 126, 224, FONT_LABEL,
                            theme.muted, theme.background, 4, 70);
}

const WeatherData* effectiveWeatherData() {
    return feedsWeatherSource() == WEATHER_FEED_OPEN_METEO ? feedsWeatherData() : nullptr;
}

bool weatherWaitingForSync() {
    return feedsWeatherSource() == WEATHER_FEED_OPEN_METEO &&
           feedsWeatherConfigured() &&
           !feedsHasWeatherData();
}

char weatherUnitSymbol() {
    return feedsWeatherUsesFahrenheit() ? 'F' : 'C';
}

String formatWeatherTemperature(int value) {
    return String(value) + weatherUnitSymbol();
}

const MarketData* effectiveMarketData(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return nullptr;
    }

    uint8_t source = feedsMarketSource(index);
    return (source == MARKET_FEED_FINNHUB || source == MARKET_FEED_COINGECKO)
               ? feedsMarketData(index)
               : nullptr;
}

String marketCurrencyPrefix(uint8_t index) {
    if (index >= DASHBOARD_MARKET_COUNT) {
        return "$";
    }

    String currency = safeCString(feedConfig.markets[index].currency);
    currency.trim();
    currency.toLowerCase();
    if (currency.length() == 0) {
        currency = "usd";
    }

    if (currency == "usd") {
        return "$";
    }
    if (currency == "cad") {
        return "CA$";
    }
    if (currency == "aud") {
        return "AU$";
    }
    if (currency == "nzd") {
        return "NZ$";
    }
    if (currency == "sgd") {
        return "SG$";
    }
    if (currency == "hkd") {
        return "HK$";
    }

    currency.toUpperCase();
    return currency + " ";
}

String formatMarketPriceValue(float price) {
    float absolutePrice = price >= 0.0f ? price : -price;
    uint8_t precision = 2;

    if (absolutePrice >= 1000.0f) {
        precision = 0;
    } else if (absolutePrice >= 100.0f) {
        precision = 1;
    } else if (absolutePrice >= 1.0f) {
        precision = 2;
    } else if (absolutePrice >= 0.1f) {
        precision = 3;
    } else {
        precision = 4;
    }

    return groupThousands(trimTrailingZeros(price, precision));
}

String formatMarketPrice(uint8_t index, float price) {
    return marketCurrencyPrefix(index) + formatMarketPriceValue(price);
}

String marketChangeWindowLabel(uint8_t index) {
    return feedsMarketSource(index) == MARKET_FEED_COINGECKO ? "24h" : "1d";
}

bool marketWaitingForSync(uint8_t index) {
    uint8_t source = feedsMarketSource(index);
    return (source == MARKET_FEED_FINNHUB || source == MARKET_FEED_COINGECKO) &&
           feedsMarketConfigured(index) &&
           !feedsHasMarketData(index);
}

bool anyMarketsWaitingForSync() {
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        if (marketWaitingForSync(index)) {
            return true;
        }
    }

    return false;
}

String homeAssistantSlotLabel(uint8_t index) {
    if (index >= HOME_ASSISTANT_SLOT_COUNT) {
        return "Entity";
    }

    const HomeAssistantSlotConfig &slot = feedConfig.homeAssistant.slots[index];
    if (slot.label[0] != '\0') {
        return safeCString(slot.label);
    }

    const HomeAssistantSlotData *runtimeSlot = feedsHomeAssistantSlotData(index);
    if (runtimeSlot != nullptr && runtimeSlot->label[0] != '\0') {
        return runtimeSlot->label;
    }

    String entityId = safeCString(slot.entityId);
    int separatorIndex = entityId.lastIndexOf('.');
    if (separatorIndex >= 0 && separatorIndex < static_cast<int>(entityId.length()) - 1) {
        entityId = entityId.substring(separatorIndex + 1);
    }
    entityId.replace("_", " ");
    if (entityId.length() == 0) {
        return "Entity";
    }

    bool capitalizeNext = true;
    for (size_t charIndex = 0; charIndex < entityId.length(); ++charIndex) {
        char character = entityId.charAt(charIndex);
        if (capitalizeNext && character >= 'a' && character <= 'z') {
            entityId.setCharAt(charIndex, static_cast<char>(toupper(character)));
            capitalizeNext = false;
        } else if (character == ' ') {
            capitalizeNext = true;
        } else {
            capitalizeNext = false;
        }
    }

    return entityId;
}

String homeAssistantSlotUnit(uint8_t index) {
    if (index >= HOME_ASSISTANT_SLOT_COUNT) {
        return "";
    }

    const HomeAssistantSlotConfig &slot = feedConfig.homeAssistant.slots[index];
    if (slot.unit[0] != '\0') {
        return safeCString(slot.unit);
    }

    const HomeAssistantSlotData *runtimeSlot = feedsHomeAssistantSlotData(index);
    return runtimeSlot != nullptr ? safeCString(runtimeSlot->unit) : String("");
}

bool homeAssistantSlotConfigured(uint8_t index) {
    return index < HOME_ASSISTANT_SLOT_COUNT &&
           feedConfig.homeAssistant.slots[index].enabled &&
           feedConfig.homeAssistant.slots[index].entityId[0] != '\0';
}

bool homeAssistantWaitingForSync() {
    return feedsHomeAssistantConfigured() && !feedsHasHomeAssistantData();
}

bool hasWeatherContent() {
    const WeatherData *weather = effectiveWeatherData();
    return weather != nullptr &&
           (weather->location[0] != '\0' ||
            weather->condition[0] != '\0' ||
            feedsHasWeatherData());
}

bool hasFocusContent() {
    return dashboardData.focus.running ||
           dashboardData.focus.breakMode ||
           dashboardData.focus.label[0] != '\0' ||
           dashboardData.focus.remainingSeconds != static_cast<uint32_t>(dashboardData.focus.durationMinutes) * 60UL;
}

const char* activeClockMessage() {
    if (displayState.line2[0] == '\0') {
        return nullptr;
    }

    if (strcmp(displayState.line2, "Dashboard preview ready") == 0) {
        return nullptr;
    }

    return displayState.line2;
}

bool hasMarketContent() {
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        const MarketData *market = effectiveMarketData(index);
        if (market != nullptr &&
            market->symbol[0] != '\0' &&
            market->enabled) {
            return true;
        }
    }

    return false;
}

bool hasHomeAssistantContent() {
    if (!feedsHomeAssistantConfigured()) {
        return false;
    }

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (homeAssistantSlotConfigured(index)) {
            return true;
        }
    }

    return false;
}

bool hasWorldClockContent() {
    for (uint8_t index = 0; index < DASHBOARD_WORLD_CLOCK_COUNT; ++index) {
        if (dashboardData.worldClocks[index].enabled && dashboardData.worldClocks[index].label[0] != '\0') {
            return true;
        }
    }

    return false;
}

bool hasEventContent() {
    return dashboardData.event.title[0] != '\0' || dashboardData.event.subtitle[0] != '\0' || dashboardData.event.remainingSeconds > 0;
}

bool hasQuoteContent() {
    return dashboardData.quote.text[0] != '\0';
}

bool hasStatusContent() {
    return dashboardData.status.line1[0] != '\0' || dashboardData.status.line2[0] != '\0';
}

bool dashboardPageHasRenderableContent(uint8_t pageId) {
    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            return true;
        case DASHBOARD_PAGE_WEATHER:
            return hasWeatherContent() || weatherWaitingForSync();
        case DASHBOARD_PAGE_MARKETS:
            return hasMarketContent() || anyMarketsWaitingForSync();
        case DASHBOARD_PAGE_HOME:
            return hasHomeAssistantContent() || homeAssistantWaitingForSync();
        case DASHBOARD_PAGE_FOCUS:
            return hasFocusContent();
        case DASHBOARD_PAGE_WORLD:
            return hasWorldClockContent();
        case DASHBOARD_PAGE_EVENT:
            return hasEventContent();
        case DASHBOARD_PAGE_QUOTE:
            return hasQuoteContent();
        case DASHBOARD_PAGE_STATUS:
            return hasStatusContent();
        default:
            return false;
    }
}

bool dashboardPageAvailable(uint8_t pageId) {
    return dashboardPageEnabled(pageId) && dashboardPageHasRenderableContent(pageId);
}

uint8_t firstAvailableDashboardPage() {
    for (uint8_t pageId = 0; pageId < DASHBOARD_PAGE_COUNT; ++pageId) {
        if (dashboardPageAvailable(pageId)) {
            return pageId;
        }
    }

    return dashboardFirstEnabledPage();
}

uint8_t nextAvailableDashboardPage(uint8_t currentPage) {
    for (uint8_t offset = 1; offset <= DASHBOARD_PAGE_COUNT; ++offset) {
        uint8_t pageId = (currentPage + offset) % DASHBOARD_PAGE_COUNT;
        if (dashboardPageAvailable(pageId)) {
            return pageId;
        }
    }

    if (dashboardPageEnabled(currentPage)) {
        return currentPage;
    }

    return dashboardFirstEnabledPage();
}

bool dashboardItemEnabled(uint8_t pageId, uint8_t slot) {
    if (pageId == DASHBOARD_PAGE_MARKETS) {
        const MarketData *market = effectiveMarketData(slot);
        return market != nullptr && market->enabled && market->symbol[0] != '\0';
    }
    if (pageId == DASHBOARD_PAGE_HOME) return homeAssistantSlotConfigured(slot);
    if (pageId == DASHBOARD_PAGE_WORLD) {
        const WorldClockData &clock = dashboardData.worldClocks[slot];
        return clock.enabled && clock.label[0] != '\0';
    }
    return false;
}

uint8_t dashboardItemLimit(uint8_t pageId) {
    if (pageId == DASHBOARD_PAGE_MARKETS) return DASHBOARD_MARKET_COUNT;
    if (pageId == DASHBOARD_PAGE_HOME) return HOME_ASSISTANT_SLOT_COUNT;
    if (pageId == DASHBOARD_PAGE_WORLD) return DASHBOARD_WORLD_CLOCK_COUNT;
    return 0;
}

uint8_t dashboardItemCount(uint8_t pageId) {
    uint8_t count = 0;
    for (uint8_t slot = 0; slot < dashboardItemLimit(pageId); ++slot) {
        if (dashboardItemEnabled(pageId, slot)) ++count;
    }
    return count;
}

void normalizeDashboardItem() {
    if (selectedItemPage != displayState.currentPage) {
        selectedItemPage = displayState.currentPage;
        selectedItemIndex = 0;
    }
    uint8_t count = dashboardItemCount(displayState.currentPage);
    if (selectedItemIndex >= count) selectedItemIndex = 0;
}

uint8_t dashboardItemSlot(uint8_t pageId) {
    uint8_t item = 0;
    for (uint8_t slot = 0; slot < dashboardItemLimit(pageId); ++slot) {
        if (dashboardItemEnabled(pageId, slot)) {
            if (item++ == selectedItemIndex) return slot;
        }
    }
    return 0;
}

void hashWeatherData(uint32_t &hash) {
    hashValue(hash, feedsWeatherSource());
    hashValue(hash, feedsWeatherUsesFahrenheit());
    hashValue(hash, weatherWaitingForSync());

    const WeatherData *weather = effectiveWeatherData();
    bool hasData = weather != nullptr;
    hashValue(hash, hasData);
    if (!hasData) {
        return;
    }

    hashCString(hash, weather->location);
    hashCString(hash, weather->condition);
    hashValue(hash, weather->temperature);
    hashValue(hash, weather->high);
    hashValue(hash, weather->low);
    hashValue(hash, weather->rainChance);
}

void hashMarketData(uint32_t &hash) {
    uint8_t index = dashboardItemSlot(DASHBOARD_PAGE_MARKETS);
    hashValue(hash, feedsMarketSource(index));
    hashValue(hash, marketWaitingForSync(index));
    hashCString(hash, feedConfig.markets[index].currency);

    const MarketData *market = effectiveMarketData(index);
    bool hasData = market != nullptr;
    hashValue(hash, hasData);
    if (!hasData) {
        return;
    }

    hashValue(hash, market->enabled);
    hashCString(hash, market->symbol);
    hashCString(hash, market->label);
    hashValue(hash, market->price);
    hashValue(hash, market->change);
    hashValue(hash, market->changePercent);
}

void hashHomeAssistantData(uint32_t &hash) {
    hashValue(hash, feedsHomeAssistantConfigured());
    hashValue(hash, feedsHasHomeAssistantData());
    hashValue(hash, homeAssistantWaitingForSync());
    hashCString(hash, feedConfig.homeAssistant.baseUrl);
    hashValue(hash, feedConfig.homeAssistant.refreshMinutes);

    uint8_t index = dashboardItemSlot(DASHBOARD_PAGE_HOME);
    const HomeAssistantSlotConfig &slotConfig = feedConfig.homeAssistant.slots[index];
    hashValue(hash, slotConfig.enabled);
    hashCString(hash, slotConfig.entityId);
    hashCString(hash, slotConfig.label);
    hashCString(hash, slotConfig.unit);

    const HomeAssistantSlotData *slot = feedsHomeAssistantSlotData(index);
    bool hasData = slot != nullptr && slot->hasData;
    hashValue(hash, hasData);
    if (slot == nullptr) {
        return;
    }

    hashValue(hash, slot->numeric);
    hashValue(hash, slot->stale);
    hashCString(hash, slot->lastError);
    if (slot->stale && slot->lastSuccessMs != 0) {
        hashValue(hash, (millis() - slot->lastSuccessMs) / 60000UL);
    }
    hashCString(hash, slot->label);
    hashCString(hash, slot->state);
    hashCString(hash, slot->unit);
}

void hashWorldClockData(uint32_t &hash) {
    uint8_t index = dashboardItemSlot(DASHBOARD_PAGE_WORLD);
    const WorldClockData &clock = dashboardData.worldClocks[index];
    hashValue(hash, clock.enabled);
    hashCString(hash, clock.label);
    hashValue(hash, clock.offsetSeconds);
}

void hashCommonPageState(uint32_t &hash, uint8_t pageId) {
    hashValue(hash, pageId);
    hashThemeConfig(hash);
    hashValue(hash, dashboardConfig.showIp);
    hashCString(hash, displayState.ipInfo);
    hashValue(hash, selectedItemIndex);
    hashValue(hash, dashboardItemCount(pageId));
}

uint32_t dashboardStaticHash(uint8_t pageId) {
    uint32_t hash = kFnvOffset;
    hashCommonPageState(hash, pageId);

    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            hashValue(hash, dashboardConfig.use24Hour);
            hashValue(hash, dashboardConfig.showSeconds);
            hashValue(hash, hasWeatherContent());
            hashValue(hash, weatherWaitingForSync());
            hashValue(hash, activeClockMessage() != nullptr);
            if (hasWeatherContent()) {
                hashWeatherData(hash);
            } else if (activeClockMessage() != nullptr) {
                hashCString(hash, activeClockMessage());
            }
            break;
        case DASHBOARD_PAGE_WEATHER:
            hashWeatherData(hash);
            break;
        case DASHBOARD_PAGE_MARKETS:
            hashMarketData(hash);
            break;
        case DASHBOARD_PAGE_HOME:
            hashHomeAssistantData(hash);
            break;
        case DASHBOARD_PAGE_FOCUS:
            hashCString(hash, dashboardData.focus.label);
            hashValue(hash, dashboardData.focus.running);
            hashValue(hash, dashboardData.focus.breakMode);
            hashValue(hash, dashboardData.focus.durationMinutes);
            break;
        case DASHBOARD_PAGE_WORLD:
            hashValue(hash, dashboardConfig.use24Hour);
            hashWorldClockData(hash);
            break;
        case DASHBOARD_PAGE_EVENT:
            hashCString(hash, dashboardData.event.title);
            hashCString(hash, dashboardData.event.subtitle);
            break;
        case DASHBOARD_PAGE_QUOTE:
            hashCString(hash, dashboardData.quote.text);
            hashCString(hash, dashboardData.quote.author);
            break;
        case DASHBOARD_PAGE_STATUS:
            hashCString(hash, dashboardData.status.line1);
            hashCString(hash, dashboardData.status.line2);
            break;
        default:
            break;
    }

    return hash;
}

uint32_t dashboardDynamicHash(uint8_t pageId) {
    uint32_t hash = kFnvOffset;
    hashValue(hash, pageId);

    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK: {
            bool isPm = false;
            String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
            String metaLine = formatClockMetaLine(isPm);
            hashCString(hash, timeText.c_str());
            hashCString(hash, metaLine.c_str());
            hashValue(hash, isPm);
            break;
        }
        case DASHBOARD_PAGE_FOCUS: {
            int32_t remainingSeconds = dashboardFocusRemainingSeconds();
            String timeText = formatDuration(remainingSeconds);
            hashCString(hash, timeText.c_str());
            hashValue(hash, focusProgressWidth(remainingSeconds));
            break;
        }
        case DASHBOARD_PAGE_WORLD: {
            const WorldClockData &clock = dashboardData.worldClocks[dashboardItemSlot(pageId)];
            String timeText = formatWorldTime(clock.offsetSeconds);
            hashCString(hash, timeText.c_str());
            break;
        }
        case DASHBOARD_PAGE_EVENT: {
            String countdown = formatRelativeCountdown(dashboardEventRemainingSeconds());
            hashCString(hash, countdown.c_str());
            break;
        }
        default:
            break;
    }

    return hash;
}

uint32_t apScreenHash() {
    uint32_t hash = kFnvOffset;
    hashThemeConfig(hash);
    hashCString(hash, displayState.ipInfo);
    hashCString(hash, displayState.apSSID);
    hashCString(hash, displayState.apPassword);
    hashValue(hash, authCanRevealPassword());
    hashCString(hash, authProvisionedPassword());
    return hash;
}

uint32_t temporaryMessageHash() {
    uint32_t hash = kFnvOffset;
    hashThemeConfig(hash);
    hashValue(hash, displayState.apMode);
    hashValue(hash, dashboardConfig.showIp);
    hashCString(hash, displayState.ipInfo);
    hashCString(hash, temporaryMessage.message);
    return hash;
}

bool pageUsesDynamicRefresh(uint8_t pageId) {
    return pageId == DASHBOARD_PAGE_CLOCK ||
           pageId == DASHBOARD_PAGE_FOCUS ||
           pageId == DASHBOARD_PAGE_WORLD ||
           pageId == DASHBOARD_PAGE_EVENT;
}

bool ensureClockDynamicSprite() {
    if (!clockDynamicSpriteAllowed) {
        return false;
    }

    if (clockDynamicSpriteReady) {
        return true;
    }

    uint32_t now = millis();
    if (clockDynamicSpriteAttempted &&
        now - clockDynamicSpriteLastAttemptMs < kClockSpriteRetryIntervalMs) {
        return false;
    }

    clockDynamicSpriteAttempted = true;
    clockDynamicSpriteLastAttemptMs = now;
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < kClockSpriteMinFreeHeapBytes) {
        logPrintf(F("Clock sprite skipped, free heap too low: %u"), freeHeap);
        return false;
    }

    clockDynamicSprite.deleteSprite();
    clockDynamicSprite.setColorDepth(1);
    clockDynamicSpriteReady =
        clockDynamicSprite.createSprite(kClockDynamicRegionWidth, kClockDynamicRegionHeight) != nullptr;

    if (clockDynamicSpriteReady) {
        logPrintf(F("Clock sprite ready (%dx%d), free heap now %u"),
                  kClockDynamicRegionWidth,
                  kClockDynamicRegionHeight,
                  ESP.getFreeHeap());
    } else {
        logPrintf(F("Clock sprite allocation failed, free heap was %u"), freeHeap);
    }

    return clockDynamicSpriteReady;
}

void updateClockDynamicArea() {
    const ThemePalette &theme = activeTheme();
    bool isPm = false;
    String timeText = formatLocalTime(dashboardConfig.showSeconds, dashboardConfig.use24Hour, &isPm);
    String metaLine = formatClockMetaLine(isPm);
    uint16_t dynamicBackground = theme.background;
    int localTimeTopY = kClockTimeY - kClockDynamicRegionY;

    if (ensureClockDynamicSprite()) {
        clockDynamicSprite.setBitmapColor(theme.text, dynamicBackground);
        clockDynamicSprite.fillSprite(0);
        drawClockTime(clockDynamicSprite,
                      timeText,
                      dashboardConfig.showSeconds,
                      1,
                      1,
                      0,
                      kClockDynamicRegionWidth / 2,
                      localTimeTopY);
        clockDynamicSprite.pushSprite(kClockDynamicRegionX, kClockDynamicRegionY);
    } else {
        tft.fillRect(kClockDynamicRegionX,
                     kClockDynamicRegionY,
                     kClockDynamicRegionWidth,
                     kClockDynamicRegionHeight,
                     dynamicBackground);
        drawClockTime(tft,
                      timeText,
                      dashboardConfig.showSeconds,
                      theme.text,
                      theme.muted,
                      dynamicBackground,
                      120,
                      kClockTimeY);
    }

    if (!clockMetaCache.valid || strcmp(clockMetaCache.metaLine, metaLine.c_str()) != 0) {
        tft.setTextFont(FONT_LABEL);
        int metaWidth = tft.textWidth(metaLine) + 12;
        if (clockMetaCache.valid) {
            metaWidth = max(metaWidth, tft.textWidth(clockMetaCache.metaLine) + 12);
        }

        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(theme.muted, dynamicBackground);
        tft.setTextPadding(metaWidth);
        tft.drawString(metaLine, 120, kClockMetaY, FONT_LABEL);
        tft.setTextPadding(0);
        updateClockMetaCache(metaLine);
    }
}

void drawDynamicText(const String &text, int x, int y, int width, int height,
                     uint8_t datum, const int *fonts, size_t fontCount,
                     uint16_t foreground, uint16_t background, uint8_t maximumSize = 1) {
    int font = FONT_LABEL;
    uint8_t size = maximumSize;
    while (true) {
        tft.setTextSize(size);
        font = chooseCanvasFittingFont(tft, text, width, fonts, fontCount, height);
        if (size == 1 || (font != FONT_INFO && tft.textWidth(text, font) <= width && tft.fontHeight(font) <= height)) break;
        --size;
    }
    String visible = fitDisplayLabel(text, width, font);
    int textX = datum == TC_DATUM ? width / 2 : 0;
    if (ensureClockDynamicSprite()) {
        clockDynamicSprite.setBitmapColor(foreground, background);
        clockDynamicSprite.fillSprite(0);
        clockDynamicSprite.setTextSize(size);
        clockDynamicSprite.setTextDatum(datum);
        clockDynamicSprite.setTextFont(font);
        clockDynamicSprite.setTextColor(1, 0);
        clockDynamicSprite.drawString(visible, textX, 0, font);
        clockDynamicSprite.pushSprite(x, y, 0, 0, width, height);
        clockDynamicSprite.setTextSize(1);
    } else {
        tft.fillRect(x, y, width, height, background);
        tft.setTextDatum(datum);
        tft.setTextFont(font);
        tft.setTextColor(foreground, background);
        tft.drawString(visible, x + textX, y, font);
    }
    tft.setTextSize(1);
}

void updateFocusDynamicArea() {
    const ThemePalette &theme = activeTheme();
    int32_t remainingSeconds = dashboardFocusRemainingSeconds();
    const int timeFonts[] = {FONT_BODY, FONT_TITLE, FONT_LABEL};
    drawDynamicText(formatDuration(remainingSeconds), 12, 92, 216, 64, TC_DATUM,
                    timeFonts, sizeof(timeFonts) / sizeof(timeFonts[0]),
                    theme.text, theme.background, 2);
    tft.fillRoundRect(28, 166, 184, 10, 5, theme.surfaceAlt);
    int filledWidth = focusProgressWidth(remainingSeconds);
    if (filledWidth > 0) tft.fillRoundRect(28, 166, filledWidth, 10, 5, theme.accent);
}

void updateWorldDynamicArea() {
    const ThemePalette &theme = activeTheme();
    const WorldClockData &clock = dashboardData.worldClocks[dashboardItemSlot(DASHBOARD_PAGE_WORLD)];
    const int timeFonts[] = {FONT_HUGE, FONT_TITLE, FONT_BODY};
    drawDynamicText(formatWorldTime(clock.offsetSeconds), 12, 80, 216, 64, TC_DATUM,
                    timeFonts, sizeof(timeFonts) / sizeof(timeFonts[0]), theme.text, theme.background);
}

void updateEventDynamicArea() {
    const ThemePalette &theme = activeTheme();
    const int countdownFonts[] = {FONT_BODY, FONT_LABEL};
    drawDynamicText(formatRelativeCountdown(dashboardEventRemainingSeconds()),
                    12, 98, 216, 64, TC_DATUM, countdownFonts,
                    sizeof(countdownFonts) / sizeof(countdownFonts[0]),
                    theme.accent, theme.background, 2);
}

void renderClockPage() {
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Clock");
    clockMetaCache.valid = false;
    updateClockDynamicArea();
    const char *message = activeClockMessage();
    if (hasWeatherContent()) {
        const WeatherData *weather = effectiveWeatherData();
        drawHeroText(formatWeatherTemperature(weather->temperature), 132, 58, theme.accent);
        drawDisplayLabel(weather->condition[0] != '\0' ? weather->condition : "Weather",
                         120, 194, 224, TC_DATUM, theme.text);
    } else if (weatherWaitingForSync()) {
        drawDisplayLabel("Syncing weather...", 120, 166, 224, TC_DATUM, theme.muted);
    } else if (message != nullptr) {
        drawWrappedCenteredText(message, 120, 136, 224, FONT_BODY,
                                theme.text, theme.background, 4, 68);
    }
}

void renderWeatherPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasWeatherContent()) {
        drawPlaceholder("Weather", weatherWaitingForSync() ? "Syncing live data." : "Add weather data.");
        return;
    }
    const WeatherData *weather = effectiveWeatherData();
    drawScreenChrome("Weather");
    drawDisplayLabel(weather->location[0] != '\0' ? weather->location : "Weather",
                     120, 8, 224, TC_DATUM, theme.text);
    drawHeroText(formatWeatherTemperature(weather->temperature), 42, 96, theme.text, 3);
    drawDisplayLabel(weather->condition[0] != '\0' ? weather->condition : "Updated",
                     120, 144, 224, TC_DATUM, theme.accent);
    const int valueFonts[] = {FONT_BODY, FONT_LABEL};
    const String values[] = {formatWeatherTemperature(weather->high),
                             formatWeatherTemperature(weather->low), String(weather->rainChance) + "%"};
    const char *labels[] = {"High", "Low", "Rain"};
    for (uint8_t index = 0; index < 3; ++index) {
        int centerX = 40 + index * 80;
        drawDisplayLabel(labels[index], centerX, 166, 72, TC_DATUM, theme.muted);
        drawAdaptiveText(values[index], centerX, 184, 72, TC_DATUM,
                         valueFonts, sizeof(valueFonts) / sizeof(valueFonts[0]), theme.text, theme.background);
    }
}

void renderMarketsPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasMarketContent()) {
        drawPlaceholder("Markets", anyMarketsWaitingForSync() ? "Syncing live data." : "Add ticker data.");
        return;
    }
    uint8_t index = dashboardItemSlot(DASHBOARD_PAGE_MARKETS);
    const MarketData *market = effectiveMarketData(index);
    drawScreenChrome("Markets");
    drawItemPosition();
    drawDisplayLabel(market->symbol, 8, 8, 168, TL_DATUM, theme.accent, FONT_BODY);
    drawDisplayLabel(market->label[0] != '\0' ? market->label : "Live feed",
                     120, 40, 224, TC_DATUM, theme.muted);
    drawHeroText(formatMarketPrice(index, market->price), 64, 68, theme.text);
    String change = (market->changePercent >= 0.0f ? "+" : "") + trimTrailingZeros(market->changePercent,
                       fabsf(market->changePercent) >= 10.0f ? 1 : 2) + "%";
    drawHeroText(change, 136, 64, market->changePercent >= 0.0f ? theme.positive : theme.negative);
    drawDisplayLabel(marketChangeWindowLabel(index) + " change", 120, 196, 224, TC_DATUM, theme.muted);
}

void drawHomeAssistantValue(uint8_t slotIndex) {
    const ThemePalette &theme = activeTheme();
    const HomeAssistantSlotData *slot = feedsHomeAssistantSlotData(slotIndex);
    drawWrappedCenteredText(homeAssistantSlotLabel(slotIndex), 120, 28, 224,
                            FONT_BODY, theme.text, theme.background, 4, 58);
    String state = "Waiting";
    uint16_t stateColor = theme.text;
    if (slot != nullptr && slot->hasData) {
        state = safeCString(slot->state);
        stateColor = slot->numeric ? theme.text : theme.accent;
    } else if (slot != nullptr && slot->lastError[0] != '\0') {
        state = "Error";
        stateColor = theme.warning;
    }
    drawHeroText(state, 94, 64, stateColor);
    drawDisplayLabel(homeAssistantSlotUnit(slotIndex), 120, 164, 224, TC_DATUM, theme.accent, FONT_BODY);
    if (slot != nullptr && slot->stale) {
        String status = slot->hasData ? "Stale" : "Error";
        if (slot->lastSuccessMs != 0) status += " " + String((millis() - slot->lastSuccessMs) / 60000UL) + "m";
        drawDisplayLabel(status, 120, 196, 224, TC_DATUM, theme.warning);
    }
}

void renderHomePage() {
    if (!hasHomeAssistantContent()) {
        drawPlaceholder("Home", homeAssistantWaitingForSync() ? "Syncing Home Assistant." : "Add Home Assistant entities.");
        return;
    }
    drawScreenChrome("Home");
    drawItemPosition();
    uint8_t slot = dashboardItemSlot(DASHBOARD_PAGE_HOME);
    drawHomeAssistantValue(slot);
}

void renderFocusPage() {
    if (!hasFocusContent()) { drawPlaceholder("Focus", "Set a timer when you need it."); return; }
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Focus");
    drawDisplayLabel(dashboardData.focus.label[0] != '\0' ? dashboardData.focus.label : "Session",
                     120, 12, 224, TC_DATUM, theme.text, FONT_BODY);
    drawDisplayLabel(dashboardData.focus.breakMode ? "Break" : "Focus", 8, 48, 104, TL_DATUM, theme.accent);
    drawDisplayLabel(dashboardData.focus.running ? "Active" : "Ready", 232, 48, 104, TR_DATUM, theme.muted);
    updateFocusDynamicArea();
    drawDisplayLabel("Duration " + String(dashboardData.focus.durationMinutes) + " min",
                     120, 192, 224, TC_DATUM, theme.muted);
}

void renderWorldPage() {
    if (!hasWorldClockContent()) { drawPlaceholder("World", "Add world clocks."); return; }
    const ThemePalette &theme = activeTheme();
    const WorldClockData &clock = dashboardData.worldClocks[dashboardItemSlot(DASHBOARD_PAGE_WORLD)];
    drawScreenChrome("World");
    drawItemPosition();
    drawDisplayLabel(clock.label, 120, 28, 224, TC_DATUM, theme.text, FONT_BODY);
    updateWorldDynamicArea();
    drawDisplayLabel(formatUtcOffset(clock.offsetSeconds), 120, 170, 224, TC_DATUM, theme.accent, FONT_BODY);
}

void renderEventPage() {
    if (!hasEventContent()) { drawPlaceholder("Event", "Add a countdown."); return; }
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Event");
    drawWrappedCenteredText(dashboardData.event.title[0] != '\0' ? dashboardData.event.title : "Upcoming",
                            120, 16, 224, FONT_BODY, theme.text, theme.background, 4, 60);
    updateEventDynamicArea();
    if (dashboardData.event.subtitle[0] != '\0') {
        drawWrappedCenteredText(dashboardData.event.subtitle, 120, 174, 224, FONT_LABEL,
                                theme.muted, theme.background, 4, 36);
    }
}

void renderQuotePage() {
    if (!hasQuoteContent()) { drawPlaceholder("Quote", "Add a quote."); return; }
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Quote");
    drawWrappedCenteredText(safeCString(dashboardData.quote.text), 120, 20, 224, FONT_BODY,
                            theme.text, theme.background, 5, 158);
    if (dashboardData.quote.author[0] != '\0') {
        drawDisplayLabel(dashboardData.quote.author, 120, 190, 224, TC_DATUM, theme.accent);
    }
}

void renderStatusPage() {
    if (!hasStatusContent()) { drawPlaceholder("Status", "Add status lines."); return; }
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Status");
    drawWrappedCenteredText(dashboardData.status.line1[0] != '\0' ? dashboardData.status.line1 : "Status line 1",
                            120, 30, 224, FONT_BODY, theme.text, theme.background, 4, 76);
    drawWrappedCenteredText(dashboardData.status.line2[0] != '\0' ? dashboardData.status.line2 : "Status line 2",
                            120, 130, 224, FONT_BODY, theme.accent, theme.background, 4, 76);
}

void renderDashboardPage() {
    if (!dashboardPageAvailable(displayState.currentPage)) {
        displayState.currentPage = firstAvailableDashboardPage();
    }

    switch (displayState.currentPage) {
        case DASHBOARD_PAGE_CLOCK:
            renderClockPage();
            break;
        case DASHBOARD_PAGE_WEATHER:
            renderWeatherPage();
            break;
        case DASHBOARD_PAGE_MARKETS:
            renderMarketsPage();
            break;
        case DASHBOARD_PAGE_HOME:
            renderHomePage();
            break;
        case DASHBOARD_PAGE_FOCUS:
            renderFocusPage();
            break;
        case DASHBOARD_PAGE_WORLD:
            renderWorldPage();
            break;
        case DASHBOARD_PAGE_EVENT:
            renderEventPage();
            break;
        case DASHBOARD_PAGE_QUOTE:
            renderQuotePage();
            break;
        case DASHBOARD_PAGE_STATUS:
            renderStatusPage();
            break;
        default:
            renderClockPage();
            break;
    }
}

void updateDashboardDynamicArea(uint8_t pageId) {
    switch (pageId) {
        case DASHBOARD_PAGE_CLOCK:
            updateClockDynamicArea();
            break;
        case DASHBOARD_PAGE_FOCUS:
            updateFocusDynamicArea();
            break;
        case DASHBOARD_PAGE_WORLD:
            updateWorldDynamicArea();
            break;
        case DASHBOARD_PAGE_EVENT:
            updateEventDynamicArea();
            break;
        default:
            break;
    }
}

void renderDashboardPageCached() {
    if (!dashboardPageAvailable(displayState.currentPage)) {
        displayState.currentPage = firstAvailableDashboardPage();
    }

    normalizeDashboardItem();
    uint8_t pageId = displayState.currentPage;
    uint8_t themeId = activeThemeIdValue();
    uint32_t staticHash = dashboardStaticHash(pageId);
    uint32_t dynamicHash = dashboardDynamicHash(pageId);

    bool requiresFullRender = !renderCache.valid ||
                              renderCache.mode != DISPLAY_MODE_DASHBOARD ||
                              renderCache.page != pageId ||
                              renderCache.theme != themeId ||
                              renderCache.staticHash != staticHash;

    if (requiresFullRender) {
        renderDashboardPage();
        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_DASHBOARD;
        renderCache.page = pageId;
        renderCache.theme = themeId;
        renderCache.staticHash = staticHash;
        renderCache.dynamicHash = dynamicHash;
        return;
    }

    if (pageUsesDynamicRefresh(pageId) && renderCache.dynamicHash != dynamicHash) {
        updateDashboardDynamicArea(pageId);
        renderCache.dynamicHash = dynamicHash;
    }
    updateFooterClock();
}

}  // namespace

void setBacklightLevel(int brightness, bool rememberPreference, bool shouldLog = true) {
    int boundedBrightness = constrain(brightness, 0, 100);
    if (rememberPreference) {
        savedBrightness = boundedBrightness;
    }

    if (appliedBrightness == boundedBrightness) {
        backlightOn = boundedBrightness > 0;
        return;
    }

    int pwmValue = 0;
    if (boundedBrightness == 0) {
        pwmValue = 1023;
    } else if (boundedBrightness == 100) {
        pwmValue = 0;
    } else {
        pwmValue = map(boundedBrightness, 0, 100, 1023, 0);
    }

    analogWrite(PIN_BACKLIGHT, pwmValue);
    appliedBrightness = boundedBrightness;
    backlightOn = boundedBrightness > 0;
    if (shouldLog) {
        logPrintf(F("Brightness: %d%%, PWM: %d"), boundedBrightness, pwmValue);
    }
}

void animateBacklightRamp(int fromBrightness,
                          int toBrightness,
                          uint8_t steps,
                          uint16_t stepDelayMs) {
    if (steps == 0) {
        setBacklightLevel(toBrightness, false, false);
        return;
    }

    int start = constrain(fromBrightness, 0, 100);
    int target = constrain(toBrightness, 0, 100);
    if (start == target) {
        return;
    }

    for (uint8_t step = 1; step <= steps; ++step) {
        int interpolated = start + ((target - start) * static_cast<int>(step)) / static_cast<int>(steps);
        setBacklightLevel(interpolated, false, false);
        if (stepDelayMs > 0) {
            delay(stepDelayMs);
            yield();
        }
    }
}

int beginPageTransition(int targetBrightness) {
    int startBrightness = constrain(targetBrightness, 0, 100);
    if (startBrightness < kPageTransitionMinimumBrightness) {
        return startBrightness;
    }

    int dimBrightness = max(static_cast<int>(kPageTransitionMinimumBrightness),
                            (startBrightness * static_cast<int>(kPageTransitionDimPercent)) / 100);
    if (dimBrightness >= startBrightness) {
        return startBrightness;
    }

    animateBacklightRamp(startBrightness,
                         dimBrightness,
                         kPageTransitionFadeDownSteps,
                         kPageTransitionFadeStepDelayMs);
    return dimBrightness;
}

void endPageTransition(int transitionBrightness, int targetBrightness) {
    int startBrightness = constrain(transitionBrightness, 0, 100);
    int endBrightness = constrain(targetBrightness, 0, 100);
    if (startBrightness == endBrightness) {
        return;
    }

    delay(8);
    yield();
    animateBacklightRamp(startBrightness,
                         endBrightness,
                         kPageTransitionFadeUpSteps,
                         kPageTransitionFadeStepDelayMs);
}

void displayInit() {
    logPrint(F("Display init..."));

    tft.init();
    tft.setRotation(0);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);


    displayState.line2[0] = '\0';
    displayState.ipInfo[0] = '\0';
    displayState.apMode = false;
    displayState.apSSID[0] = '\0';
    displayState.apPassword[0] = '\0';
    displayState.currentPage = DASHBOARD_PAGE_CLOCK;
    clearTemporaryMessage();

    clockDynamicSprite.deleteSprite();
    clockDynamicSpriteReady = false;
    clockDynamicSpriteAttempted = false;
    clockDynamicSpriteAllowed = false;

    pinMode(PIN_BACKLIGHT, OUTPUT);
    analogWriteFreq(1000);
    analogWriteRange(1023);
    displaySetBrightness(100);

    logPrint(F("Display init complete"));
}

void displaySetBrightness(int brightness) {
    setBacklightLevel(brightness, true);
}

void displayApplyBrightness(int brightness) {
    setBacklightLevel(brightness, false);
}

void displaySetClockSpriteAllowed(bool allowed) {
    if (clockDynamicSpriteAllowed == allowed &&
        (!clockDynamicSpriteReady || allowed)) {
        return;
    }

    clockDynamicSpriteAllowed = allowed;
    clockDynamicSpriteAttempted = false;

    if (!allowed) {
        clockDynamicSprite.deleteSprite();
        clockDynamicSpriteReady = false;
    }
}

void displaySuspendDynamicResources() {
    if (dynamicResourceSuspendDepth == 0) {
        resumeClockSpriteAfterDynamicSuspend = clockDynamicSpriteAllowed;
        if (clockDynamicSpriteReady || clockDynamicSpriteAllowed) {
            clockDynamicSprite.deleteSprite();
            clockDynamicSpriteReady = false;
            clockDynamicSpriteAttempted = false;
            clockDynamicSpriteAllowed = false;
            logPrintf(F("Display resources suspended, free heap now %u"), ESP.getFreeHeap());
        }
    }

    if (dynamicResourceSuspendDepth < 255) {
        ++dynamicResourceSuspendDepth;
    }
}

void displayResumeDynamicResources() {
    if (dynamicResourceSuspendDepth == 0) {
        return;
    }

    --dynamicResourceSuspendDepth;
    if (dynamicResourceSuspendDepth != 0) {
        return;
    }

    if (resumeClockSpriteAfterDynamicSuspend) {
        clockDynamicSpriteAllowed = true;
        clockDynamicSpriteAttempted = false;
        resumeClockSpriteAfterDynamicSuspend = false;
    }
}

void displayUpdate() {
    if (temporaryMessageVisible()) {
        uint32_t currentHash = temporaryMessageHash();
        uint8_t themeId = activeThemeIdValue();
        if (renderCache.valid &&
            renderCache.mode == DISPLAY_MODE_TEMP_MESSAGE &&
            renderCache.theme == themeId &&
            renderCache.staticHash == currentHash) {
            updateFooterClock();
            return;
        }

        const ThemePalette &theme = activeTheme();
        drawScreenChrome("SmartClock");
        drawWrappedCenteredText(temporaryMessage.message, 120, 60, 224, FONT_BODY, theme.text, theme.background, 4, 144);

        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_TEMP_MESSAGE;
        renderCache.page = 0;
        renderCache.theme = themeId;
        renderCache.staticHash = currentHash;
        renderCache.dynamicHash = 0;
        return;
    }

    if (temporaryMessageExpired()) {
        clearTemporaryMessage();
        invalidateRenderCache();
    }



    if (displayState.apMode) {
        uint32_t currentHash = apScreenHash();
        uint8_t themeId = activeThemeIdValue();
        if (renderCache.valid &&
            renderCache.mode == DISPLAY_MODE_AP &&
            renderCache.theme == themeId &&
            renderCache.staticHash == currentHash) {
            return;
        }

        displayRenderAPMode();
        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_AP;
        renderCache.page = 0;
        renderCache.theme = themeId;
        renderCache.staticHash = currentHash;
        renderCache.dynamicHash = 0;
        return;
    }

    renderDashboardPageCached();
}

void displayRenderClock() {
    renderClockPage();
}

void displayRenderAPMode() {
    const ThemePalette &theme = activeTheme();
    bool canRevealAdminPassword = authCanRevealPassword();
    const char *adminPassword = authProvisionedPassword();
    String setupIp = displayState.ipInfo[0] != '\0' ? safeCString(displayState.ipInfo) : String("192.168.4.1");
    const int kWideFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int kIpFonts[] = {FONT_INFO};
    const int kCompactFonts[] = {FONT_LABEL, FONT_INFO};

    tft.fillScreen(theme.background);

    tft.fillRoundRect(10, 10, 220, 24, 12, theme.surfaceAlt);
    tft.fillCircle(24, 22, 4, theme.accent);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.text, theme.surfaceAlt);
    tft.drawString("Setup mode", 36, 16, FONT_INFO);

    drawRoundedPanel(10, 42, 220, 60, theme.accentSoft, theme.accentSoft);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.accent, theme.accentSoft);
    tft.drawString("SETUP URL", 120, 52, FONT_INFO);
    drawAdaptiveText(setupIp,
                     120,
                     68,
                     192,
                     TC_DATUM,
                     kIpFonts,
                     sizeof(kIpFonts) / sizeof(kIpFonts[0]),
                     theme.text,
                     theme.accentSoft);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.accentSoft);
    tft.drawString("Open this in browser", 120, 86, FONT_INFO);

    drawRoundedPanel(10, 108, 134, 50, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString("WI-FI", 20, 118, FONT_INFO);
    drawAdaptiveText(safeCString(displayState.apSSID),
                     77,
                     136,
                     106,
                     TC_DATUM,
                     kCompactFonts,
                     sizeof(kCompactFonts) / sizeof(kCompactFonts[0]),
                     theme.text,
                     theme.surface);

    drawRoundedPanel(150, 108, 80, 50, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString("PASS", 190, 118, FONT_INFO);
    drawAdaptiveText(safeCString(displayState.apPassword),
                     190,
                     136,
                     58,
                     TC_DATUM,
                     kWideFonts,
                     sizeof(kWideFonts) / sizeof(kWideFonts[0]),
                     theme.text,
                     theme.surface);

    drawRoundedPanel(10, 168, 220, 62, theme.surfaceAlt, theme.surfaceAlt);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(canRevealAdminPassword ? theme.accent : theme.muted, theme.surfaceAlt);
    tft.drawString("ADMIN LOGIN", 20, 178, FONT_INFO);

    if (canRevealAdminPassword && adminPassword != nullptr && adminPassword[0] != '\0') {
        drawBadge(20, 193, 48, 18, "admin", theme.accentSoft, theme.accent);
        drawAdaptiveText(safeCString(adminPassword),
                         220,
                         191,
                         138,
                         TR_DATUM,
                         kWideFonts,
                         sizeof(kWideFonts) / sizeof(kWideFonts[0]),
                         theme.text,
                         theme.surfaceAlt);

        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.surfaceAlt);
        tft.drawString("Use this password on the dashboard.", 20, 212, FONT_INFO);
        return;
    }

    drawWrappedCenteredText("Use your saved admin password after Wi-Fi setup.",
                            120,
                            194,
                            192,
                            FONT_INFO,
                            theme.muted,
                            theme.surfaceAlt,
                            2, 34);
}

void displayShowDashboard() {
    clearTemporaryMessage();
    displayState.apMode = false;
    invalidateRenderCache();
    displayUpdate();
}



void displayServiceTemporaryMessage() {
    if (temporaryMessageExpired()) {
        displayUpdate();
    }
}





void displayShowMessage(const String &msg) {
    clearTemporaryMessage();
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("SmartClock");
    drawWrappedCenteredText(msg, 120, 60, 224, FONT_BODY, theme.text, theme.background, 4, 144);
    invalidateRenderCache();
}

void displayShowTemporaryMessage(const String &msg, uint32_t durationMs) {
    strncpy(temporaryMessage.message, msg.c_str(), sizeof(temporaryMessage.message) - 1);
    temporaryMessage.message[sizeof(temporaryMessage.message) - 1] = '\0';
    temporaryMessage.active = temporaryMessage.message[0] != '\0';
    temporaryMessage.expiresAtMs = millis() + (durationMs > 0 ? durationMs : 1000UL);
    invalidateRenderCache();
    displayUpdate();
}

void displayShowAPScreen(const char *ssid, const char *password, const char *ip) {
    displayState.apMode = true;
    strncpy(displayState.apSSID, ssid, sizeof(displayState.apSSID) - 1);
    displayState.apSSID[sizeof(displayState.apSSID) - 1] = '\0';
    strncpy(displayState.apPassword, password, sizeof(displayState.apPassword) - 1);
    displayState.apPassword[sizeof(displayState.apPassword) - 1] = '\0';
    strncpy(displayState.ipInfo, ip, sizeof(displayState.ipInfo) - 1);
    displayState.ipInfo[sizeof(displayState.ipInfo) - 1] = '\0';
    invalidateRenderCache();
    displayUpdate();
}

void displayCycleNextPage(bool smoothTransition) {
    if (temporaryMessageVisible()) {
        logPrint(F("Page cycling disabled while temporary message is active"));
        return;
    }

    if (displayState.apMode) {
        logPrint(F("Page cycling disabled in AP mode"));
        return;
    }



    normalizeDashboardItem();
    uint8_t previousItem = selectedItemIndex;
    uint8_t previousPage = displayState.currentPage;
    if (!dashboardPageAvailable(displayState.currentPage)) {
        displayState.currentPage = firstAvailableDashboardPage();
    } else if (selectedItemIndex + 1 < dashboardItemCount(displayState.currentPage)) {
        ++selectedItemIndex;
    } else {
        selectedItemIndex = 0;
        displayState.currentPage = nextAvailableDashboardPage(displayState.currentPage);
    }
    normalizeDashboardItem();

    if (displayState.currentPage == previousPage && selectedItemIndex == previousItem) {
        return;
    }

    int targetBrightness = appliedBrightness;
    int transitionBrightness = targetBrightness;
    if (smoothTransition) {
        transitionBrightness = beginPageTransition(targetBrightness);
    }

    invalidateRenderCache();
    displayUpdate();
    if (smoothTransition) {
        endPageTransition(transitionBrightness, targetBrightness);
    }
}

void displayToggleBacklight() {
    if (backlightOn) {
        logPrint(F("Backlight OFF"));
        displayApplyBrightness(0);
    } else {
        int resumeBrightness = dashboardEffectiveBrightness(savedBrightness > 0 ? savedBrightness : 100);
        logPrintf(F("Backlight ON (brightness: %d)"), resumeBrightness);
        displayApplyBrightness(resumeBrightness);
    }
}
