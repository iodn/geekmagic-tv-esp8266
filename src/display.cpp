#include "display.h"

#include "auth.h"
#include "config.h"
#include "feeds.h"
#include "logger.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <time.h>
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
constexpr int kClockMetaY = 118;
constexpr int kClockFooterY = 148;
constexpr int kClockFooterHeight = 78;
// The 1 bpp buffer takes about 1.7 KB; keep at least 8 KB for request handling.
constexpr uint32_t kClockSpriteMinFreeHeapBytes = 10000UL;
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

const ThemePalette kThemes[DASHBOARD_THEME_COUNT] = {
    {
        rgb565(15, 18, 27),
        rgb565(29, 35, 47),
        rgb565(39, 46, 60),
        rgb565(168, 199, 250),
        rgb565(44, 66, 104),
        rgb565(236, 241, 251),
        rgb565(173, 182, 197),
        rgb565(123, 214, 167),
        rgb565(255, 180, 171),
        rgb565(255, 210, 128),
    },
    {
        rgb565(33, 16, 11),
        rgb565(61, 34, 26),
        rgb565(74, 43, 32),
        rgb565(255, 183, 77),
        rgb565(118, 75, 35),
        rgb565(255, 239, 232),
        rgb565(224, 189, 173),
        rgb565(151, 221, 168),
        rgb565(255, 172, 162),
        rgb565(255, 214, 120),
    },
    {
        rgb565(9, 18, 14),
        rgb565(19, 36, 28),
        rgb565(26, 47, 37),
        rgb565(165, 230, 174),
        rgb565(47, 83, 60),
        rgb565(232, 247, 236),
        rgb565(160, 188, 170),
        rgb565(137, 230, 164),
        rgb565(255, 169, 169),
        rgb565(233, 241, 129),
    },
};

int savedBrightness = 100;
int appliedBrightness = -1;
bool backlightOn = true;

enum DisplayRenderMode : uint8_t {
    DISPLAY_MODE_DASHBOARD = 0,
    DISPLAY_MODE_TEMP_MESSAGE,
    DISPLAY_MODE_AP,
    DISPLAY_MODE_IMAGE
};

struct RenderCache {
    bool valid;
    uint8_t mode;
    uint8_t page;
    uint8_t theme;
    uint32_t staticHash;
    uint32_t dynamicHash;
    char imagePath[DISPLAY_PATH_BUFFER_SIZE];
};

RenderCache renderCache = {false, DISPLAY_MODE_DASHBOARD, 0, 0, 0, 0, {0}};

struct ClockMetaCache {
    bool valid;
    char metaLine[40];
};

ClockMetaCache clockMetaCache = {false, {0}};
bool clockDynamicSpriteReady = false;
bool clockDynamicSpriteAttempted = false;
bool clockDynamicSpriteAllowed = false;
bool resumeClockSpriteAfterDynamicSuspend = false;
uint8_t dynamicResourceSuspendDepth = 0;

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
    renderCache.imagePath[0] = '\0';
    clockMetaCache.valid = false;
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
    theme.positive = base.positive;
    theme.negative = base.negative;
    theme.warning = base.warning;
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

bool tftOutput(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap) {
    if (y >= tft.height()) {
        return false;
    }

    tft.pushImage(x, y, width, height, bitmap);
    return true;
}

const ThemePalette& activeTheme() {
    static ThemePalette customTheme;
    ThemeSelection selection = effectiveThemeSelection();
    uint8_t themeIndex = constrain(selection.theme, 0, DASHBOARD_THEME_COUNT - 1);
    if (!selection.customThemeEnabled) {
        return kThemes[themeIndex];
    }

    customTheme = buildCustomTheme(kThemes[themeIndex],
                                   selection.background,
                                   selection.surface,
                                   selection.accent,
                                   selection.text);
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

    auto flushWord = [&](bool forceLineBreak) {
        if (currentWord.isEmpty()) {
            return;
        }

        String candidate = currentLine;
        if (!candidate.isEmpty()) {
            candidate += " ";
        }
        candidate += currentWord;

        if (forceLineBreak || tft.textWidth(candidate) > maxWidth) {
            if (!currentLine.isEmpty()) {
                lines.push_back(currentLine);
                currentLine = currentWord;
            } else {
                lines.push_back(currentWord);
                currentLine = "";
            }
        } else {
            currentLine = candidate;
        }

        currentWord = "";
    };

    for (size_t index = 0; index < text.length(); ++index) {
        char character = text.charAt(index);

        if (character == '\n') {
            flushWord(false);
            lines.push_back(currentLine);
            currentLine = "";
        } else if (character == ' ') {
            flushWord(false);
        } else {
            currentWord += character;
        }
    }

    flushWord(false);
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
                             int lineSpacing = 4) {
    std::vector<String> lines = wrapText(text, font, maxWidth);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(font);
    tft.setTextColor(textColor, backgroundColor);
    int lineHeight = tft.fontHeight() + lineSpacing;

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        tft.drawString(lines[lineIndex], centerX, startY + (lineIndex * lineHeight), font);
    }
}

int chooseFittingFont(const String &text, int maxWidth, const int *fontCandidates, size_t fontCandidateCount,
                      int maxHeight = DISPLAY_HEIGHT) {
    return chooseCanvasFittingFont(tft, text, maxWidth, fontCandidates, fontCandidateCount, maxHeight);
}

void drawAdaptiveBadge(int x,
                       int y,
                       int width,
                       int height,
                       const String &label,
                       const int *fontCandidates,
                       size_t fontCandidateCount,
                       uint16_t fillColor,
                       uint16_t textColor) {
    tft.fillRoundRect(x, y, width, height, height / 2, fillColor);
    tft.setTextDatum(MC_DATUM);

    int font = chooseFittingFont(label, max(0, width - 12), fontCandidates, fontCandidateCount);
    tft.setTextFont(font);
    tft.setTextColor(textColor, fillColor);
    tft.drawString(label, x + (width / 2), y + (height / 2), font);
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

void drawDividerLine(int x, int y, int width, uint16_t color) {
    tft.drawFastHLine(x, y, width, color);
}

void drawDividerColumn(int x, int y, int height, uint16_t color) {
    tft.drawFastVLine(x, y, height, color);
}

void drawMetricColumn(int centerX,
                      int topY,
                      const String &label,
                      const String &value,
                      uint16_t labelColor,
                      uint16_t valueColor,
                      uint16_t backgroundColor) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(labelColor, backgroundColor);
    tft.drawString(label, centerX, topY, FONT_INFO);

    const int valueFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    drawAdaptiveText(value,
                     centerX,
                     topY + 16,
                     58,
                     TC_DATUM,
                     valueFonts,
                     sizeof(valueFonts) / sizeof(valueFonts[0]),
                     valueColor,
                     backgroundColor);
}

void drawScreenChrome(const String &title) {
    const ThemePalette &theme = activeTheme();
    const int chromeX = 10;
    const int chromeY = 10;
    const int chromeWidth = 220;
    const int chromeHeight = 26;

    tft.fillScreen(theme.background);
    tft.fillRoundRect(chromeX, chromeY, chromeWidth, chromeHeight, 12, theme.surfaceAlt);
    tft.fillCircle(chromeX + 14, chromeY + (chromeHeight / 2), 4, theme.accent);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(FONT_LABEL);
    tft.setTextColor(theme.text, theme.surfaceAlt);
    tft.drawString(title, chromeX + 26, chromeY + 5, FONT_LABEL);

    String subtitle = headerSubtitle();
    if (!subtitle.isEmpty()) {
        int chipWidth = min(94, tft.textWidth(subtitle, FONT_INFO) + 16);
        int chipHeight = 18;
        int chipX = chromeX + chromeWidth - chipWidth - 6;
        int chipY = chromeY + 4;

        tft.fillRoundRect(chipX, chipY, chipWidth, chipHeight, chipHeight / 2, theme.background);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.background);
        tft.drawString(subtitle, chipX + (chipWidth / 2), chipY + (chipHeight / 2), FONT_INFO);
    }
}

void drawPlaceholder(const String &title, const String &message) {
    const ThemePalette &theme = activeTheme();
    drawScreenChrome(title);

    drawRoundedPanel(10, 42, 220, 178, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TC_DATUM);
    tft.fillRoundRect(98, 68, 44, 44, 22, theme.accentSoft);
    tft.setTextColor(theme.accent, theme.accentSoft);
    tft.setTextFont(FONT_BODY);
    tft.drawString("?", 120, 78, FONT_BODY);

    tft.setTextColor(theme.text, theme.surface);
    tft.setTextFont(FONT_BODY);
    tft.drawString("Nothing here yet", 120, 128, FONT_BODY);
    drawWrappedCenteredText(message, 120, 154, 184, FONT_INFO, theme.muted, theme.surface, 2);
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

String formatMarketChangeSummary(uint8_t index, float changePercent) {
    float absoluteChange = changePercent >= 0.0f ? changePercent : -changePercent;
    uint8_t precision = absoluteChange >= 10.0f ? 1 : 2;
    return marketChangeWindowLabel(index) +
           " " +
           (changePercent >= 0.0f ? "+" : "") +
           trimTrailingZeros(changePercent, precision) +
           "%";
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
    for (uint8_t index = 0; index < DASHBOARD_MARKET_COUNT; ++index) {
        hashValue(hash, feedsMarketSource(index));
        hashValue(hash, marketWaitingForSync(index));
        hashCString(hash, feedConfig.markets[index].currency);

        const MarketData *market = effectiveMarketData(index);
        bool hasData = market != nullptr;
        hashValue(hash, hasData);
        if (!hasData) {
            continue;
        }

        hashValue(hash, market->enabled);
        hashCString(hash, market->symbol);
        hashCString(hash, market->label);
        hashValue(hash, market->price);
        hashValue(hash, market->change);
        hashValue(hash, market->changePercent);
    }
}

void hashHomeAssistantData(uint32_t &hash) {
    hashValue(hash, feedsHomeAssistantConfigured());
    hashValue(hash, feedsHasHomeAssistantData());
    hashValue(hash, homeAssistantWaitingForSync());
    hashCString(hash, feedConfig.homeAssistant.baseUrl);
    hashValue(hash, feedConfig.homeAssistant.refreshMinutes);

    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        const HomeAssistantSlotConfig &slotConfig = feedConfig.homeAssistant.slots[index];
        hashValue(hash, slotConfig.enabled);
        hashCString(hash, slotConfig.entityId);
        hashCString(hash, slotConfig.label);
        hashCString(hash, slotConfig.unit);

        const HomeAssistantSlotData *slot = feedsHomeAssistantSlotData(index);
        bool hasData = slot != nullptr && slot->hasData;
        hashValue(hash, hasData);
        if (slot == nullptr) {
            continue;
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
}

void hashWorldClockData(uint32_t &hash) {
    for (uint8_t index = 0; index < DASHBOARD_WORLD_CLOCK_COUNT; ++index) {
        const WorldClockData &clock = dashboardData.worldClocks[index];
        hashValue(hash, clock.enabled);
        hashCString(hash, clock.label);
        hashValue(hash, clock.offsetSeconds);
    }
}

void hashCommonPageState(uint32_t &hash, uint8_t pageId) {
    hashValue(hash, pageId);
    hashThemeConfig(hash);
    hashValue(hash, dashboardConfig.showIp);
    hashCString(hash, displayState.ipInfo);
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
            hashValue(hash, remainingSeconds);
            break;
        }
        case DASHBOARD_PAGE_WORLD: {
            for (uint8_t index = 0; index < DASHBOARD_WORLD_CLOCK_COUNT; ++index) {
                const WorldClockData &clock = dashboardData.worldClocks[index];
                if (!clock.enabled || clock.label[0] == '\0') {
                    continue;
                }

                String timeText = formatWorldTime(clock.offsetSeconds);
                hashCString(hash, timeText.c_str());
            }
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

    if (clockDynamicSpriteAttempted) {
        return false;
    }

    clockDynamicSpriteAttempted = true;
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
    uint16_t dynamicBackground = theme.surface;
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
        tft.setTextFont(FONT_BODY);
        int metaWidth = tft.textWidth(metaLine) + 12;
        if (clockMetaCache.valid) {
            metaWidth = max(metaWidth, tft.textWidth(clockMetaCache.metaLine) + 12);
        }

        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(theme.muted, dynamicBackground);
        tft.setTextPadding(metaWidth);
        tft.drawString(metaLine, 120, kClockMetaY, FONT_BODY);
        tft.setTextPadding(0);
        updateClockMetaCache(metaLine);
    }
}

void drawDynamicText(const String &text, int x, int y, int width, int height,
                     uint8_t datum, const int *fonts, size_t fontCount,
                     uint16_t foreground, uint16_t background) {
    int font = chooseCanvasFittingFont(tft, text, width, fonts, fontCount, height);
    int textX = datum == TC_DATUM ? width / 2 : 0;
    if (ensureClockDynamicSprite()) {
        // Share the existing 1 bpp buffer; only push the text region, preserving nearby labels.
        clockDynamicSprite.setBitmapColor(foreground, background);
        clockDynamicSprite.fillSprite(0);
        clockDynamicSprite.setTextDatum(datum);
        clockDynamicSprite.setTextFont(font);
        clockDynamicSprite.setTextColor(1, 0);
        clockDynamicSprite.drawString(text, textX, 0, font);
        clockDynamicSprite.pushSprite(x, y, 0, 0, width, height);
    } else {
        tft.fillRect(x, y, width, height, background);
        tft.setTextDatum(datum);
        tft.setTextFont(font);
        tft.setTextColor(foreground, background);
        tft.drawString(text, x + textX, y, font);
    }
}

void updateFocusDynamicArea() {
    const ThemePalette &theme = activeTheme();
    int32_t remainingSeconds = dashboardFocusRemainingSeconds();

    const int timeFonts[] = {FONT_HUGE, FONT_TITLE, FONT_BODY};
    drawDynamicText(formatDuration(remainingSeconds), 20, 106, 200, 48, TC_DATUM,
                    timeFonts, sizeof(timeFonts) / sizeof(timeFonts[0]), theme.text, theme.surface);

    int progressWidth = 184;
    int progressHeight = 8;
    int progressX = 28;
    int progressY = 178;
    tft.fillRoundRect(progressX, progressY, progressWidth, progressHeight, 4, theme.surfaceAlt);
    if (dashboardData.focus.durationMinutes > 0) {
        uint32_t totalSeconds = static_cast<uint32_t>(dashboardData.focus.durationMinutes) * 60UL;
        uint32_t boundedRemaining = remainingSeconds > 0 ? static_cast<uint32_t>(remainingSeconds) : 0U;
        uint32_t elapsedSeconds = (boundedRemaining < totalSeconds) ? (totalSeconds - boundedRemaining) : 0U;
        uint32_t safeTotal = totalSeconds > 0 ? totalSeconds : 1U;
        int filledWidth = map(elapsedSeconds, 0, safeTotal, 0, progressWidth);
        filledWidth = constrain(filledWidth, 0, progressWidth);
        if (filledWidth > 0) {
            tft.fillRoundRect(progressX, progressY, filledWidth, progressHeight, 4, theme.accent);
        }
    }

}

void updateWorldDynamicArea() {
    const ThemePalette &theme = activeTheme();
    int y = 70;
    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        const WorldClockData &clock = dashboardData.worldClocks[clockIndex];
        if (!clock.enabled || clock.label[0] == '\0') {
            continue;
        }

        const int timeFonts[] = {FONT_TITLE, FONT_BODY, FONT_LABEL};
        drawDynamicText(formatWorldTime(clock.offsetSeconds), 20, y, 118, 48, TL_DATUM,
                        timeFonts, sizeof(timeFonts) / sizeof(timeFonts[0]), theme.text, theme.surface);
        y += 72;
    }
}

void updateEventDynamicArea() {
    const ThemePalette &theme = activeTheme();
    const int countdownFonts[] = {FONT_HUGE, FONT_TITLE, FONT_BODY, FONT_LABEL, FONT_INFO};
    drawDynamicText(formatRelativeCountdown(dashboardEventRemainingSeconds()),
                    24, 100, 192, 48, TC_DATUM, countdownFonts,
                    sizeof(countdownFonts) / sizeof(countdownFonts[0]), theme.accent, theme.surface);
}

void renderClockPage() {
    const ThemePalette &theme = activeTheme();
    const int compactFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int footerConditionFonts[] = {FONT_LABEL, FONT_INFO};
    const int footerTemperatureFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    drawScreenChrome("Clock");
    drawRoundedPanel(8, 40, 224, 106, theme.surface, theme.surfaceAlt);
    clockMetaCache.valid = false;
    updateClockDynamicArea();

    const char *clockMessage = activeClockMessage();
    bool showBottomPanel = hasWeatherContent() || weatherWaitingForSync() || clockMessage != nullptr;
    if (!showBottomPanel) {
        return;
    }

    drawRoundedPanel(8, 154, 224, 66, theme.surfaceAlt, theme.surfaceAlt);
    if (hasWeatherContent()) {
        const WeatherData *weather = effectiveWeatherData();
        int footerLeft = 18;
        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.surfaceAlt);
        String location = (weather != nullptr && weather->location[0] != '\0') ? weather->location : "Weather";
        tft.drawString(location, footerLeft, 166, FONT_INFO);

        drawAdaptiveBadge(158,
                          160,
                          62,
                          24,
                          formatWeatherTemperature(weather->temperature),
                          footerTemperatureFonts,
                          sizeof(footerTemperatureFonts) / sizeof(footerTemperatureFonts[0]),
                          theme.accentSoft,
                          theme.accent);

        tft.setTextColor(theme.text, theme.surfaceAlt);
        String condition = weather->condition[0] != '\0' ? weather->condition : "Ready";
        drawAdaptiveText(condition,
                         footerLeft,
                         188,
                         132,
                         TL_DATUM,
                         footerConditionFonts,
                         sizeof(footerConditionFonts) / sizeof(footerConditionFonts[0]),
                         theme.text,
                         theme.surfaceAlt);

        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.surfaceAlt);
        String summary = "H " + formatWeatherTemperature(weather->high) +
                         "  L " + formatWeatherTemperature(weather->low) +
                         "  Rain " + String(weather->rainChance) + "%";
        drawAdaptiveText(summary,
                         footerLeft,
                         202,
                         188,
                         TL_DATUM,
                         compactFonts,
                         sizeof(compactFonts) / sizeof(compactFonts[0]),
                         theme.muted,
                         theme.surfaceAlt);
    } else if (weatherWaitingForSync()) {
        drawWrappedCenteredText("Syncing weather...", 120, 182, 192, FONT_INFO, theme.muted, theme.surfaceAlt, 2);
    } else if (clockMessage != nullptr) {
        drawWrappedCenteredText(clockMessage, 120, 178, 192, FONT_LABEL, theme.text, theme.surfaceAlt, 2);
    }
}

void renderWeatherPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasWeatherContent()) {
        if (weatherWaitingForSync()) {
            drawPlaceholder("Weather", "Syncing live data.");
            return;
        }
        drawPlaceholder("Weather", "Add weather data.");
        return;
    }

    const WeatherData *weather = effectiveWeatherData();
    const int heroFonts[] = {FONT_HUGE, FONT_TITLE, FONT_BODY};
    const int compactFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    drawScreenChrome("Weather");
    drawRoundedPanel(8, 36, 224, 110, theme.surface, theme.surfaceAlt);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString(weather->location[0] != '\0' ? weather->location : "Weather",
                   120, 50, FONT_INFO);

    drawAdaptiveText(formatWeatherTemperature(weather->temperature),
                     120,
                     74,
                     180,
                     TC_DATUM,
                     heroFonts,
                     sizeof(heroFonts) / sizeof(heroFonts[0]),
                     theme.text,
                     theme.surface);

    drawAdaptiveText(weather->condition[0] != '\0' ? weather->condition : "Updated",
                     120,
                     118,
                     180,
                     TC_DATUM,
                     compactFonts,
                     sizeof(compactFonts) / sizeof(compactFonts[0]),
                     theme.accent,
                     theme.surface);

    drawRoundedPanel(8, 156, 224, 64, theme.surfaceAlt, theme.surfaceAlt);
    drawDividerColumn(82, 168, 40, theme.background);
    drawDividerColumn(156, 168, 40, theme.background);
    drawMetricColumn(45, 170, "High", formatWeatherTemperature(weather->high), theme.muted, theme.text, theme.surfaceAlt);
    drawMetricColumn(119, 170, "Low", formatWeatherTemperature(weather->low), theme.muted, theme.text, theme.surfaceAlt);
    drawMetricColumn(193, 170, "Rain", String(weather->rainChance) + "%", theme.muted, theme.text, theme.surfaceAlt);
}

void renderMarketsPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasMarketContent()) {
        if (anyMarketsWaitingForSync()) {
            drawPlaceholder("Markets", "Syncing live data.");
            return;
        }
        drawPlaceholder("Markets", "Add ticker data.");
        return;
    }

    drawScreenChrome("Markets");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    int y = 50;
    bool firstRow = true;
    const int standardPriceFonts[] = {FONT_TITLE, FONT_BODY, FONT_LABEL};
    const int compactPriceFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int symbolFonts[] = {FONT_BODY, FONT_LABEL};
    const int labelFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int deltaFonts[] = {FONT_INFO};
    for (uint8_t marketIndex = 0; marketIndex < DASHBOARD_MARKET_COUNT; ++marketIndex) {
        const MarketData *market = effectiveMarketData(marketIndex);
        if (market == nullptr ||
            market->symbol[0] == '\0' ||
            !market->enabled) {
            continue;
        }

        if (!firstRow) {
            drawDividerLine(20, y - 8, 192, theme.surfaceAlt);
        }

        tft.setTextDatum(TL_DATUM);
        String marketLabel = market->label[0] != '\0' ? market->label : "Live feed";
        drawAdaptiveText(market->symbol,
                         20,
                         y,
                         72,
                         TL_DATUM,
                         symbolFonts,
                         sizeof(symbolFonts) / sizeof(symbolFonts[0]),
                         theme.accent,
                         theme.surface);
        drawAdaptiveText(marketLabel,
                         20,
                         y + 20,
                         104,
                         TL_DATUM,
                         labelFonts,
                         sizeof(labelFonts) / sizeof(labelFonts[0]),
                         theme.text,
                         theme.surface);

        tft.setTextDatum(TR_DATUM);
        String priceText = formatMarketPrice(marketIndex, market->price);
        const int *priceFonts = standardPriceFonts;
        size_t priceFontCount = sizeof(standardPriceFonts) / sizeof(standardPriceFonts[0]);
        if (priceText.length() >= 8 || tft.textWidth(priceText, FONT_TITLE) > 126) {
            priceFonts = compactPriceFonts;
            priceFontCount = sizeof(compactPriceFonts) / sizeof(compactPriceFonts[0]);
        }

        int priceFont = chooseFittingFont(priceText, 134, priceFonts, priceFontCount);
        tft.setTextFont(priceFont);
        tft.setTextColor(theme.text, theme.surface);
        tft.drawString(priceText, 220, y + 2, priceFont);

        int deltaY = y + 2 + tft.fontHeight() + 4;

        uint16_t deltaColor = market->changePercent >= 0.0f ? theme.positive : theme.negative;
        drawAdaptiveText(formatMarketChangeSummary(marketIndex, market->changePercent),
                         220,
                         deltaY,
                         134,
                         TR_DATUM,
                         deltaFonts,
                         sizeof(deltaFonts) / sizeof(deltaFonts[0]),
                         deltaColor,
                         theme.surface);

        y += 58;
        firstRow = false;
    }
}

void drawHomeAssistantCard(int x,
                           int y,
                           int width,
                           int height,
                           uint8_t slotIndex,
                           uint16_t fillColor,
                           uint16_t borderColor) {
    const ThemePalette &theme = activeTheme();
    drawRoundedPanel(x, y, width, height, fillColor, borderColor);

    tft.fillRoundRect(x + 10, y + 10, 26, 4, 2, theme.accentSoft);

    const int labelFonts[] = {FONT_LABEL, FONT_INFO};
    // Selection checks glyph coverage and the available card height.
    const int numericValueFonts[] = {FONT_TITLE, FONT_BODY, FONT_LABEL, FONT_INFO};
    const int textValueFonts[] = {FONT_BODY, FONT_LABEL, FONT_INFO};
    const int unitFonts[] = {FONT_INFO};
    const HomeAssistantSlotData *slot = feedsHomeAssistantSlotData(slotIndex);
    int labelY = y + 18;
    int stateY = height >= 120 ? y + 74 : (height >= 96 ? y + 50 : y + 40);
    int unitY = y + height - tft.fontHeight(FONT_INFO) - 4;

    String label = homeAssistantSlotLabel(slotIndex);
    drawAdaptiveText(label,
                     x + 12,
                     labelY,
                     width - 24,
                     TL_DATUM,
                     labelFonts,
                     sizeof(labelFonts) / sizeof(labelFonts[0]),
                     theme.muted,
                     fillColor);

    String unit = homeAssistantSlotUnit(slotIndex);
    if (slot != nullptr && slot->hasData && unit.length() > 0) {
        drawAdaptiveBadge(x + width - 52,
                          y + 10,
                          40,
                          18,
                          unit,
                          unitFonts,
                          sizeof(unitFonts) / sizeof(unitFonts[0]),
                          theme.accentSoft,
                          theme.accent);
    }

    String stateText = "Waiting";
    uint16_t stateColor = theme.text;
    if (slot != nullptr && slot->hasData) {
        stateText = safeCString(slot->state);
        stateColor = slot->numeric ? theme.text : theme.accent;
    } else if (slot != nullptr && slot->lastError[0] != '\0') {
        stateText = "Error";
        stateColor = theme.warning;
    } else if (!feedsHomeAssistantConfigured()) {
        stateText = "Add";
        stateColor = theme.muted;
    } else if (!homeAssistantSlotConfigured(slotIndex)) {
        stateText = "Off";
        stateColor = theme.muted;
    } else if (!homeAssistantWaitingForSync()) {
        stateText = "No data";
        stateColor = theme.muted;
    }

    const bool numericValue = slot != nullptr && slot->hasData && slot->numeric;
    const int *valueFonts = numericValue ? numericValueFonts : textValueFonts;
    const size_t valueFontCount = numericValue
        ? sizeof(numericValueFonts) / sizeof(numericValueFonts[0])
        : sizeof(textValueFonts) / sizeof(textValueFonts[0]);

    int valueFont = chooseFittingFont(stateText, width - 20, valueFonts, valueFontCount,
                                      max(0, unitY - stateY - 2));
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(valueFont);
    tft.setTextColor(stateColor, fillColor);
    tft.drawString(stateText, x + (width / 2), stateY, valueFont);

    String footer = unit;
    if (slot != nullptr && slot->stale) {
        footer = slot->hasData ? "Stale" : "Error";
        if (slot->lastSuccessMs != 0) {
            footer += " " + String((millis() - slot->lastSuccessMs) / 60000UL) + "m";
        }
    }
    if (footer.length() > 0) {
        drawAdaptiveText(footer, x + (width / 2), unitY, width - 22, TC_DATUM,
                         unitFonts, sizeof(unitFonts) / sizeof(unitFonts[0]), theme.muted, fillColor);
    }

}

void renderHomePage() {
    const ThemePalette &theme = activeTheme();
    if (!hasHomeAssistantContent()) {
        if (homeAssistantWaitingForSync()) {
            drawPlaceholder("Home", "Syncing Home Assistant.");
            return;
        }
        drawPlaceholder("Home", "Add Home Assistant entities.");
        return;
    }

    drawScreenChrome("Home");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    uint8_t slotIndexes[HOME_ASSISTANT_SLOT_COUNT] = {0};
    uint8_t slotCount = 0;
    for (uint8_t index = 0; index < HOME_ASSISTANT_SLOT_COUNT; ++index) {
        if (homeAssistantSlotConfigured(index)) {
            slotIndexes[slotCount++] = index;
        }
    }

    if (slotCount == 0) {
        drawWrappedCenteredText("Add Home Assistant entities.", 120, 114, 188, FONT_LABEL, theme.muted, theme.surface, 2);
        return;
    }

    if (slotCount == 1) {
        drawHomeAssistantCard(16, 48, 208, 160, slotIndexes[0], theme.surfaceAlt, theme.surfaceAlt);
        return;
    }

    if (slotCount == 2) {
        drawHomeAssistantCard(16, 48, 208, 70, slotIndexes[0], theme.surfaceAlt, theme.surfaceAlt);
        drawHomeAssistantCard(16, 134, 208, 70, slotIndexes[1], theme.surfaceAlt, theme.surfaceAlt);
        return;
    }

    const int cardWidth = 100;
    const int cardHeight = 80;
    const int leftColumnX = 16;
    const int rightColumnX = 124;
    const int topRowY = 48;
    const int bottomRowY = 136;

    for (uint8_t displayIndex = 0; displayIndex < slotCount; ++displayIndex) {
        int x = (displayIndex % 2 == 0) ? leftColumnX : rightColumnX;
        int y = (displayIndex < 2) ? topRowY : bottomRowY;
        drawHomeAssistantCard(x,
                              y,
                              cardWidth,
                              cardHeight,
                              slotIndexes[displayIndex],
                              theme.surfaceAlt,
                              theme.surfaceAlt);
    }
}

void renderFocusPage() {
    if (!hasFocusContent()) {
        drawPlaceholder("Focus", "Set a timer when you need it.");
        return;
    }

    const ThemePalette &theme = activeTheme();
    drawScreenChrome("Focus");

    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);
    drawBadge(18, 48, 64, 20, dashboardData.focus.breakMode ? "Break" : "Focus", theme.accentSoft, theme.accent);

    if (dashboardData.focus.running) {
        drawBadge(166, 48, 54, 20, "Active", theme.positive, theme.background);
    } else {
        drawBadge(170, 48, 50, 20, "Ready", theme.surfaceAlt, theme.text);
    }

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString(dashboardData.focus.label[0] != '\0' ? dashboardData.focus.label : "Session",
                   120, 76, FONT_INFO);

    updateFocusDynamicArea();
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_INFO);
    tft.setTextColor(theme.muted, theme.surface);
    tft.drawString("Duration " + String(dashboardData.focus.durationMinutes) + " min", 120, 196, FONT_INFO);
}

void renderWorldPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasWorldClockContent()) {
        drawPlaceholder("World", "Add world clocks.");
        return;
    }

    drawScreenChrome("World");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    int y = 52;
    bool firstRow = true;
    const int offsetFonts[] = {FONT_INFO};
    for (uint8_t clockIndex = 0; clockIndex < DASHBOARD_WORLD_CLOCK_COUNT; ++clockIndex) {
        const WorldClockData &clock = dashboardData.worldClocks[clockIndex];
        if (!clock.enabled || clock.label[0] == '\0') {
            continue;
        }

        if (!firstRow) {
            drawDividerLine(20, y - 10, 184, theme.surfaceAlt);
        }

        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(FONT_INFO);
        tft.setTextColor(theme.muted, theme.surface);
        tft.drawString(clock.label, 20, y, FONT_INFO);

        drawAdaptiveText(formatUtcOffset(clock.offsetSeconds),
                         220,
                         y,
                         88,
                         TR_DATUM,
                         offsetFonts,
                         sizeof(offsetFonts) / sizeof(offsetFonts[0]),
                         theme.accent,
                         theme.surface);
        y += 72;
        firstRow = false;
    }

    updateWorldDynamicArea();
}

void renderEventPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasEventContent()) {
        drawPlaceholder("Event", "Add a countdown.");
        return;
    }

    drawScreenChrome("Event");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    drawWrappedCenteredText(dashboardData.event.title[0] != '\0' ? dashboardData.event.title : "Upcoming",
                            120, 56, 186, FONT_BODY, theme.text, theme.surface, 2);
    tft.fillRoundRect(104, 82, 32, 4, 2, theme.accentSoft);

    updateEventDynamicArea();

    if (dashboardData.event.subtitle[0] != '\0') {
        drawWrappedCenteredText(dashboardData.event.subtitle,
                                120,
                                178,
                                184,
                                FONT_INFO,
                                theme.muted,
                                theme.surface,
                                2);
    }
}

void renderQuotePage() {
    const ThemePalette &theme = activeTheme();
    if (!hasQuoteContent()) {
        drawPlaceholder("Quote", "Add a quote.");
        return;
    }

    drawScreenChrome("Quote");
    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);

    tft.setTextDatum(TC_DATUM);
    tft.fillRoundRect(100, 52, 40, 40, 20, theme.accentSoft);
    tft.setTextFont(FONT_BODY);
    tft.setTextColor(theme.accent, theme.accentSoft);
    tft.drawString("\"", 120, 60, FONT_BODY);

    drawWrappedCenteredText(safeCString(dashboardData.quote.text),
                            120, 104, 186, FONT_LABEL, theme.text, theme.surface, 4);

    if (dashboardData.quote.author[0] != '\0') {
        int chipWidth = min(160, tft.textWidth(dashboardData.quote.author, FONT_INFO) + 20);
        int chipX = 120 - (chipWidth / 2);
        tft.fillRoundRect(chipX, 186, chipWidth, 20, 10, theme.accentSoft);
        drawAdaptiveText(safeCString(dashboardData.quote.author),
                         120,
                         191,
                         chipWidth - 14,
                         MC_DATUM,
                         nullptr,
                         0,
                         theme.accent,
                         theme.accentSoft);
    }
}

void renderStatusPage() {
    const ThemePalette &theme = activeTheme();
    if (!hasStatusContent()) {
        drawPlaceholder("Status", "Add status lines.");
        return;
    }

    drawScreenChrome("Status");

    drawRoundedPanel(8, 36, 224, 184, theme.surface, theme.surfaceAlt);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(FONT_TITLE);
    tft.setTextColor(theme.text, theme.surface);
    drawWrappedCenteredText(dashboardData.status.line1[0] != '\0' ? dashboardData.status.line1 : "Status line 1",
                            120, 74, 184, FONT_BODY, theme.text, theme.surface, 2);

    drawDividerLine(28, 132, 184, theme.surfaceAlt);
    drawWrappedCenteredText(dashboardData.status.line2[0] != '\0' ? dashboardData.status.line2 : "Status line 2",
                            120, 150, 184, FONT_LABEL, theme.muted, theme.surface, 2);
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
        renderCache.imagePath[0] = '\0';
        return;
    }

    if (pageUsesDynamicRefresh(pageId) && renderCache.dynamicHash != dynamicHash) {
        updateDashboardDynamicArea(pageId);
        renderCache.dynamicHash = dynamicHash;
    }
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

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tftOutput);

    displayState.line2[0] = '\0';
    displayState.ipInfo[0] = '\0';
    displayState.showImage = false;
    displayState.imagePath[0] = '\0';
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
            return;
        }

        const ThemePalette &theme = activeTheme();
        drawScreenChrome("SmartClock");
        drawRoundedPanel(12, 48, DISPLAY_WIDTH - 24, 176, theme.surface, theme.surfaceAlt);
        drawWrappedCenteredText(temporaryMessage.message, 120, 92, 192, FONT_BODY, theme.text, theme.surface, 4);

        renderCache.valid = true;
        renderCache.mode = DISPLAY_MODE_TEMP_MESSAGE;
        renderCache.page = 0;
        renderCache.theme = themeId;
        renderCache.staticHash = currentHash;
        renderCache.dynamicHash = 0;
        renderCache.imagePath[0] = '\0';
        return;
    }

    if (temporaryMessageExpired()) {
        clearTemporaryMessage();
        invalidateRenderCache();
    }

    if (displayState.showImage && displayState.imagePath[0] != '\0') {
        if (renderCache.valid &&
            renderCache.mode == DISPLAY_MODE_IMAGE &&
            strcmp(renderCache.imagePath, displayState.imagePath) == 0) {
            return;
        }

        displayRenderImage(displayState.imagePath);
        if (LittleFS.exists(displayState.imagePath)) {
            renderCache.valid = true;
            renderCache.mode = DISPLAY_MODE_IMAGE;
            renderCache.page = 0;
            renderCache.theme = activeThemeIdValue();
            renderCache.staticHash = 0;
            renderCache.dynamicHash = 0;
            strncpy(renderCache.imagePath, displayState.imagePath, sizeof(renderCache.imagePath) - 1);
            renderCache.imagePath[sizeof(renderCache.imagePath) - 1] = '\0';
        }
        return;
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
        renderCache.imagePath[0] = '\0';
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
                            2);
}

void displayBlankScreen() {
    clearTemporaryMessage();
    tft.fillScreen(TFT_BLACK);
    invalidateRenderCache();
    logPrint(F("Display blanked to black."));
}

void displayInvalidateImage(const char *path) {
    if (path != nullptr && strcmp(displayState.imagePath, path) == 0) {
        invalidateRenderCache();
    }
}

void displayServiceTemporaryMessage() {
    if (temporaryMessageExpired()) {
        displayUpdate();
    }
}

bool displayValidateImage(const char *path) {
    if (path == nullptr || !LittleFS.exists(path)) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    bool complete = file.size() >= 4 && file.read() == 0xFF && file.read() == 0xD8 &&
                    file.seek(file.size() - 2) && file.read() == 0xFF && file.read() == 0xD9;
    file.close();
    if (!complete) {
        return false;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    if (TJpgDec.getFsJpgSize(&width, &height, path, LittleFS) != JDR_OK || width == 0 || height == 0) {
        return false;
    }

    struct CallbackRestorer {
        ~CallbackRestorer() { TJpgDec.setCallback(tftOutput); }
    } restoreCallback;
    TJpgDec.setCallback([](int16_t, int16_t, uint16_t, uint16_t, uint16_t *) {
        yield();
        return true;
    });
    return TJpgDec.drawFsJpg(0, 0, path, LittleFS) == JDR_OK;
}

void displayRenderImage(const char *path) {
    if (!LittleFS.exists(path)) {
        displayShowMessage(F("Image not found"));
        return;
    }

    File jpgFile = LittleFS.open(path, "r");
    if (!jpgFile) {
        displayShowMessage(String(F("Failed to open\n")) + path);
        return;
    }

    tft.startWrite();
    JRESULT result = TJpgDec.drawFsJpg(0, 0, jpgFile);
    tft.endWrite();
    jpgFile.close();

    if (result != JDR_OK) {
        displayShowMessage(String(F("JPEG error\n")) + String(result));
    }
}

void displayShowMessage(const String &msg) {
    clearTemporaryMessage();
    const ThemePalette &theme = activeTheme();
    drawScreenChrome("SmartClock");
    drawRoundedPanel(18, 54, 204, 144, theme.surface, theme.surfaceAlt);
    drawWrappedCenteredText(msg, 120, 96, 170, FONT_BODY, theme.text, theme.surface, 4);
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
    displayState.showImage = false;
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

    if (displayState.showImage) {
        displayState.showImage = false;
        displayState.currentPage = firstAvailableDashboardPage();
        invalidateRenderCache();
        displayUpdate();
        return;
    }

    uint8_t previousPage = displayState.currentPage;
    if (!dashboardPageAvailable(displayState.currentPage)) {
        displayState.currentPage = firstAvailableDashboardPage();
    } else {
        displayState.currentPage = nextAvailableDashboardPage(displayState.currentPage);
    }

    if (displayState.currentPage == previousPage) {
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
        logPrintf(F("Backlight ON (brightness: %d)"), savedBrightness);
        displayApplyBrightness(savedBrightness > 0 ? savedBrightness : 100);
    }
}
