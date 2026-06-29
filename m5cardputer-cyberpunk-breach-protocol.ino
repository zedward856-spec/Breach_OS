#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include "M5Cardputer.h"
#include <Preferences.h>
#include <WiFi.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <AudioOutput.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceSPIFFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceBuffer.h>

struct RealFile {
    String name;
    String sizeStr;
    bool isDir;
};

class AudioOutputM5Speaker : public AudioOutput {
  public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) { 
        _m5sound = m5sound; 
        _virtual_ch = virtual_sound_channel; 
    }
    virtual ~AudioOutputM5Speaker(void) {};
    virtual bool begin(void) override { return true; }
    virtual bool ConsumeSample(int16_t sample[2]) override {
      if (_tri_buffer_index < tri_buf_size) {
        _tri_buffer[_tri_index][_tri_buffer_index] = sample[0]; 
        _tri_buffer[_tri_index][_tri_buffer_index+1] = sample[1]; 
        _tri_buffer_index += 2; 
        return true;
      }
      flush(); 
      return false;
    }
    virtual void flush(void) override {
      if (_tri_buffer_index) { 
        _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch); 
        _tri_index = _tri_index < 2 ? _tri_index + 1 : 0; 
        _tri_buffer_index = 0; 
      }
    }
    virtual bool stop(void) override { 
        flush(); 
        _m5sound->stop(_virtual_ch); 
        return true; 
    }
  protected:
    m5::Speaker_Class* _m5sound; 
    uint8_t _virtual_ch; 
    static constexpr size_t tri_buf_size = 2048;
    int16_t _tri_buffer[3][tri_buf_size]; 
    size_t _tri_buffer_index = 0; 
    size_t _tri_index = 0;
};

void bootToFactory() {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (partition == NULL) {
        partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    }
    if (partition != NULL) {
        esp_ota_set_boot_partition(partition);
    }
    ESP.restart();
}

enum SortField {
    SORT_FIELD_NAME,
    SORT_FIELD_TYPE
};
enum SortOrder {
    SORT_ORDER_ASC,
    SORT_ORDER_DESC
};
SortField currentSortField = SORT_FIELD_NAME;
SortOrder currentSortOrder = SORT_ORDER_ASC;

WiFiClientSecure secureClient;
bool secureClientInit = false;
bool otaInit = false;

#define API_URL "https://m5cardputer-cyberpunk-breach-protoc.vercel.app/api"

M5Canvas canvas(&M5Cardputer.Display);

// --- AUDIO PLACEHOLDERS ---
#include "sounds.h"

int globalVolume = 100;

const unsigned char* sound_hover = nullptr;
size_t sound_hover_size = 0;
const unsigned char* sound_select = button_wav;
size_t sound_select_size = button_wav_len;
const unsigned char* sound_buffer = buffer_wav;
size_t sound_buffer_size = buffer_wav_len;
const unsigned char* sound_success = leaderboard_wav;
size_t sound_success_size = leaderboard_wav_len;
const unsigned char* sound_fail = error_wav;
size_t sound_fail_size = error_wav_len;

void playSound(const unsigned char* soundData, size_t soundSize) {
    if (soundData != nullptr && soundSize > 0) {
        M5Cardputer.Speaker.playWav(soundData, soundSize);
    }
}
// --------------------------

// Cyberpunk Colors in RGB565
#define CP_YELLOW canvas.color565(220, 244, 27)
#define CP_CYAN canvas.color565(56, 190, 201)
#define CP_RED canvas.color565(255, 0, 60)
#define CP_GREEN canvas.color565(0, 255, 75)
#define CP_BG canvas.color565(14, 17, 21)
#define CP_PANEL canvas.color565(14, 17, 21)
#define CP_ACTIVE_LINE canvas.color565(44, 53, 71)
#define CP_DIM canvas.color565(88, 97, 10)

String hexCodes[7] = {"1C", "55", "BD", "E9", "FF", "7A", "42"};

int gridSize = 5;
int targetSize = 4;

String matrix[5][5];
String targetSeq[6];
String buffer[6];
int bufferIndex = 0;

int activeRow = 0;
int activeCol = 0;
bool isRowActive = true; 
int cursorIdx = 0;

int maxTime = 3000;
int timeLeft = 3000;
unsigned long lastUpdate = 0;
bool gameOver = false;
bool hackSuccess = false;

unsigned long animStartTime = 0;
bool isAnimating = false;
bool blinkState = false;
unsigned long lastBlink = 0;

Preferences prefs;
int highScore = 0;
int currentScore = 0;

enum AppState {
    STATE_SPLASH,
    STATE_AUTH_MENU,
    STATE_WIFI_SCAN,
    STATE_WIFI_PASS,
    STATE_MAIN_MENU,
    STATE_LEADERBOARD,
    STATE_ACCOUNT,
    STATE_GRID_SELECT,
    STATE_PHASE_TRANSITION,
    STATE_FAILED_SCREEN,
    STATE_PLAYING,
    STATE_CONTROLS,
    STATE_CREDITS,
    STATE_HARDWARE_MENU,
    STATE_FILE_MANAGER,
    STATE_FILE_LOADING,
    STATE_HARDWARE_SETTINGS,
    STATE_FILE_ACTIONS_MENU,
    STATE_FILE_RENAME_INPUT
};
AppState appState = STATE_SPLASH;

bool isGuest = false;
int insaneMode = 1;
bool lastBreachFailed = false;
String authUser = "";
String authPass = "";
int authFocus = 0; 
bool rememberMe = false;
String savedSSID = "";
String savedWifiPass = "";

std::vector<String> wifiList;
int wifiSelection = 0;
String wifiPass = "";

struct LeaderboardEntry {
    String username;
    int score;
    int grid;
    int phase;
    String date;
};
std::vector<LeaderboardEntry> globalLeaderboard;
int totalLeaderboardSize = 0;
int leaderboardCursor = 0;
int leaderboardScrollOffset = 0;
int mainMenuFocus = 0; // 0: PLAY, 1: LEADERBOARD, 2: ACCOUNT

int accountFocus = 0;
String newAccountName = "";
String newAccountPass = "";
int accountHighScore = 0;
int accountRank = 0;
int accountHighGrid = 0;
int accountHighPhase = 0;
bool accountStatsFetched = false;

int currentPhase = 1;
int accumulatedScore = 0;
int phaseTimes[] = {34000, 21000, 13000, 8000, 5000, 3000, 2000, 1000};
int selectedGridSize = 5;
int phaseMenuFocus = 0;
int gridMenuFocus = 0;
float currentGridScroll = 0;
float targetGridScroll = 0;
float currentMenuScroll = 0;
float targetMenuScroll = 0;
bool showMenuDesc = false;
float descAnimWidth = 0.0;
bool showVolumePopup = false;
unsigned long lastVolumeChangeTime = 0;
bool showBrightnessPopup = false;
unsigned long lastBrightnessChangeTime = 0;
int globalBrightness = 80;

int lastPhaseScore = 0;
float lastTimeRatio = 0.0;

// Forward declarations
void initGame(bool keepDiff = false);
void drawScreen();
void drawSplash();
void drawAuthMenu();
void drawWifiScan();
void drawWifiPass();
void drawMainMenu();
void enterMainMenu();
void drawLeaderboard();
void drawAccountMenu();
void drawGridSelect();
void drawPhaseTransition();
void drawGameOverFailed();
void fetchLeaderboard(int offset = 0, int limit = 10);
uint16_t blendColor(uint16_t c1, uint16_t c2, uint8_t alpha);
void drawVolumeOverlay();
void drawBrightnessOverlay();
void pushCanvas();
void drawCurrentScreen();
void drawProgressBar(int progress, String statusText, uint16_t color = CP_CYAN);
void drawControlsScreen();
void handleControlsInput(Keyboard_Class::KeysState status);
void drawCreditsScreen();
void handleCreditsInput(Keyboard_Class::KeysState status);
void drawHardwareMenu();
void handleHardwareMenuInput(Keyboard_Class::KeysState status);
void drawFileManager(bool push = true);
void handleFileManagerInput(Keyboard_Class::KeysState status);
void drawFileLoading();
void drawHardwareSettings();
void handleHardwareSettingsInput(Keyboard_Class::KeysState status);
void drawFileActionsMenu();
void handleFileActionsMenuInput(Keyboard_Class::KeysState status);
void drawFileRenameInput();
void handleFileRenameInput(Keyboard_Class::KeysState status);
void startMp3(String fileName);
void stopMp3();

void drawMessage(String msg, String line2 = "");
void drawGlitchText(String text, int x, int y, int size, uint16_t color, bool center = true, bool forceGlitch = false) {
    bool canGlitch = (insaneMode == 2) || (insaneMode == 1 && forceGlitch);
    if (!canGlitch) {
        canvas.setTextSize(size);
        canvas.setTextColor(color);
        if (center) canvas.drawCenterString(text, x, y);
        else canvas.drawString(text, x, y);
        return;
    }
    
    unsigned long timeChunk = millis() / (insaneMode == 2 ? 80 : 300);
    uint32_t seed = timeChunk + text.length();
    for (unsigned int i = 0; i < text.length(); i++) {
        seed = (seed * 33) + text[i];
    }
    
    auto localRand = [&seed](int minVal, int maxVal) -> int {
        seed = (seed * 1103515245 + 12345);
        int range = maxVal - minVal;
        if (range <= 0) return minVal;
        return minVal + ((seed / 65536) % range);
    };
    
    bool glitch = (localRand(0, 100) < 60); // 60% chance to glitch
    canvas.setTextSize(size);
    
    if (!glitch) {
        canvas.setTextColor(color);
        if (center) canvas.drawCenterString(text, x, y);
        else canvas.drawString(text, x, y);
        return;
    }
    
    // Jitter
    int jx = x + localRand(-4, 5);
    int jy = y + localRand(-2, 3);
    
    // Color separation
    if (localRand(0, 2) == 0) {
        canvas.setTextColor(TFT_MAGENTA);
        if (center) canvas.drawCenterString(text, jx + localRand(2, 5), jy);
        else canvas.drawString(text, jx + localRand(2, 5), jy);
    }
    
    canvas.setTextColor(localRand(0, 2) == 0 ? WHITE : color);
    if (center) canvas.drawCenterString(text, jx, jy);
    else canvas.drawString(text, jx, jy);
    
    // Slice (erase horizontal lines)
    int numSlices = localRand(1, 4);
    for (int i=0; i<numSlices; i++) {
        int sy = jy + localRand(0, size * 8);
        canvas.drawLine(jx - 100, sy, jx + 100, sy, CP_BG);
        if (localRand(0, 2) == 0) canvas.drawLine(jx - 100, sy+1, jx + 100, sy+1, CP_BG);
    }
    
    // Random artifact lines
    if (localRand(0, 3) == 0) {
        int ax = jx + localRand(-50, 50);
        int ay = jy + localRand(0, size * 8);
        canvas.drawLine(ax, ay, ax + localRand(10, 30), ay, color);
    }
}

void drawMessage(String msg, String line2) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(2);
    if (line2 == "") {
        canvas.drawCenterString(msg, 120, 60);
    } else {
        canvas.drawCenterString(msg, 120, 45);
        canvas.drawCenterString(line2, 120, 70);
    }
    pushCanvas();
}
uint16_t blendColor(uint16_t c1, uint16_t c2, uint8_t alpha) {
    if (alpha >= 255) return c1;
    if (alpha <= 0) return c2;
    uint8_t r1 = (c1 >> 11) & 0x1F;
    uint8_t g1 = (c1 >> 5) & 0x3F;
    uint8_t b1 = c1 & 0x1F;
    uint8_t r2 = (c2 >> 11) & 0x1F;
    uint8_t g2 = (c2 >> 5) & 0x3F;
    uint8_t b2 = c2 & 0x1F;
    uint8_t r = (r1 * alpha + r2 * (255 - alpha)) / 255;
    uint8_t g = (g1 * alpha + g2 * (255 - alpha)) / 255;
    uint8_t b = (b1 * alpha + b2 * (255 - alpha)) / 255;
    return (r << 11) | (g << 5) | b;
}

void drawVolumeOverlay() {
    int x = 5;
    int y = 52;
    int w = 75;
    int h = 32;
    
    unsigned long elapsed = millis() - lastVolumeChangeTime;
    float t = 0.0;
    if (elapsed > 1000) {
        t = (float)(elapsed - 1000) / 300.0;
        if (t > 1.0) t = 1.0;
    }
    
    uint8_t alpha = 255 * (1.0 - t);
    if (alpha == 0) return;
    
    M5Canvas tSpr(&canvas);
    tSpr.createSprite(w, h);
    
    uint16_t transColor = canvas.color565(255, 0, 128);
    tSpr.fillSprite(transColor);
    
    tSpr.fillRect(0, 0, w, h - 8, CP_PANEL);
    tSpr.fillRect(0, h - 8, w - 8, 8, CP_PANEL);
    
    int chip = 8;
    tSpr.drawLine(0, 0, w - 1, 0, CP_YELLOW);
    tSpr.drawLine(0, 0, 0, h - 1, CP_YELLOW);
    tSpr.drawLine(0, h - 1, w - 1 - chip, h - 1, CP_YELLOW);
    tSpr.drawLine(w - 1, 0, w - 1, h - 1 - chip, CP_YELLOW);
    tSpr.drawLine(w - 1, h - 1 - chip, w - 1 - chip, h - 1, CP_YELLOW);
    
    int volPct = globalVolume;
    tSpr.setTextColor(WHITE);
    tSpr.setTextSize(1);
    tSpr.setCursor(8, 6);
    tSpr.print("VOL: " + String(volPct) + "%");
    
    tSpr.drawRect(8, 18, 59, 6, CP_YELLOW);
    int barW = (57 * globalVolume) / 100;
    if (barW > 0) {
        tSpr.fillRect(9, 19, barW, 4, CP_YELLOW);
    }
    
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            uint16_t pColor = tSpr.readPixel(dx, dy);
            if (pColor != transColor) {
                uint16_t bgColor = canvas.readPixel(x + dx, y + dy);
                uint16_t blendedColor = blendColor(pColor, bgColor, alpha);
                canvas.drawPixel(x + dx, y + dy, blendedColor);
            }
        }
    }
    
    tSpr.deleteSprite();
}

void drawBrightnessOverlay() {
    int x = 5;
    int y = 88;
    int w = 75;
    int h = 32;
    
    unsigned long elapsed = millis() - lastBrightnessChangeTime;
    float t = 0.0;
    if (elapsed > 1000) {
        t = (float)(elapsed - 1000) / 300.0;
        if (t > 1.0) t = 1.0;
    }
    
    uint8_t alpha = 255 * (1.0 - t);
    if (alpha == 0) return;
    
    M5Canvas tSpr(&canvas);
    tSpr.createSprite(w, h);
    
    uint16_t transColor = canvas.color565(255, 0, 128);
    tSpr.fillSprite(transColor);
    
    tSpr.fillRect(0, 0, w, h - 8, CP_PANEL);
    tSpr.fillRect(0, h - 8, w - 8, 8, CP_PANEL);
    
    int chip = 8;
    tSpr.drawLine(0, 0, w - 1, 0, CP_YELLOW);
    tSpr.drawLine(0, 0, 0, h - 1, CP_YELLOW);
    tSpr.drawLine(0, h - 1, w - 1 - chip, h - 1, CP_YELLOW);
    tSpr.drawLine(w - 1, 0, w - 1, h - 1 - chip, CP_YELLOW);
    tSpr.drawLine(w - 1, h - 1 - chip, w - 1 - chip, h - 1, CP_YELLOW);
    
    tSpr.setTextColor(WHITE);
    tSpr.setTextSize(1);
    tSpr.setCursor(8, 6);
    tSpr.print("BRT: " + String(globalBrightness) + "%");
    
    tSpr.drawRect(8, 18, 59, 6, CP_YELLOW);
    int barW = (57 * globalBrightness) / 100;
    if (barW > 0) {
        tSpr.fillRect(9, 19, barW, 4, CP_YELLOW);
    }
    
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            uint16_t pColor = tSpr.readPixel(dx, dy);
            if (pColor != transColor) {
                uint16_t bgColor = canvas.readPixel(x + dx, y + dy);
                uint16_t blendedColor = blendColor(pColor, bgColor, alpha);
                canvas.drawPixel(x + dx, y + dy, blendedColor);
            }
        }
    }
    
    tSpr.deleteSprite();
}

void pushCanvas() {
    if (showVolumePopup) {
        drawVolumeOverlay();
    }
    if (showBrightnessPopup) {
        drawBrightnessOverlay();
    }
    canvas.pushSprite(0, 0);
    canvas.endWrite();
}

void drawCurrentScreen() {
    switch (appState) {
        case STATE_SPLASH: drawSplash(); break;
        case STATE_AUTH_MENU: drawAuthMenu(); break;
        case STATE_WIFI_SCAN: drawWifiScan(); break;
        case STATE_WIFI_PASS: drawWifiPass(); break;
        case STATE_MAIN_MENU: drawMainMenu(); break;
        case STATE_LEADERBOARD: drawLeaderboard(); break;
        case STATE_ACCOUNT: drawAccountMenu(); break;
        case STATE_GRID_SELECT: drawGridSelect(); break;
        case STATE_PHASE_TRANSITION: drawPhaseTransition(); break;
        case STATE_FAILED_SCREEN: drawGameOverFailed(); break;
        case STATE_PLAYING: drawScreen(); break;
        case STATE_CONTROLS: drawControlsScreen(); break;
        case STATE_CREDITS: drawCreditsScreen(); break;
        case STATE_HARDWARE_MENU: drawHardwareMenu(); break;
        case STATE_FILE_MANAGER: drawFileManager(); break;
        case STATE_FILE_LOADING: drawFileLoading(); break;
        case STATE_HARDWARE_SETTINGS: drawHardwareSettings(); break;
        case STATE_FILE_ACTIONS_MENU: drawFileActionsMenu(); break;
        case STATE_FILE_RENAME_INPUT: drawFileRenameInput(); break;
    }
}

void drawProgressBar(int progress, String statusText, uint16_t color) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw Cyberpunk framed container
    canvas.drawRect(20, 20, 200, 95, color);
    canvas.drawRect(22, 22, 196, 91, CP_DIM);
    
    // Header
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- CYBERDECK LINK SYSTEM ---", 120, 28);
    canvas.drawLine(25, 38, 215, 38, color);
    
    // Status text
    canvas.setTextColor(color);
    canvas.setTextSize(1);
    canvas.drawCenterString(statusText, 120, 48);
    
    // Progress bar frame
    canvas.drawRect(35, 68, 170, 16, color);
    
    // Progress fill
    int fillW = (166 * progress) / 100;
    if (fillW > 0) {
        canvas.fillRect(37, 70, fillW, 12, color);
    }
    
    // Percentage text
    canvas.setTextColor(WHITE);
    canvas.setTextSize(1);
    canvas.drawCenterString(String(progress) + "%", 120, 92);
    
    pushCanvas();
}

void drawControlsScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- CYBERDECK INPUT SCHEMATICS ---", 120, 11);
    canvas.drawLine(10, 22, 230, 22, CP_CYAN);
    
    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(15, 28); canvas.print("NAVIGATE / SCROLL  :");
    canvas.setCursor(15, 40); canvas.print("SELECT / CONFIRM   :");
    canvas.setCursor(15, 52); canvas.print("CANCEL / GO BACK   :");
    canvas.setCursor(15, 64); canvas.print("VOLUME CONTROL     :");
    canvas.setCursor(15, 76); canvas.print("BRIGHTNESS CONTROL :");
    canvas.setCursor(15, 88); canvas.print("TEXT DELETE        :");
    canvas.setCursor(15, 100); canvas.print("MATRIX GRID SELECT :");
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(130, 28); canvas.print("ARROW_KEYS");
    canvas.setCursor(130, 40); canvas.print("ENTER");
    canvas.setCursor(130, 52); canvas.print("ESC");
    canvas.setCursor(130, 64); canvas.print("- / +");
    canvas.setCursor(130, 76); canvas.print("[ / ]");
    canvas.setCursor(130, 88); canvas.print("BACKSPACE");
    canvas.setCursor(130, 100); canvas.print("ARROW_KEYS");
    
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("PRESS ENTER TO EXIT", 120, 115);
    
    pushCanvas();
}

void handleControlsInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    if (status.enter || hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_MAIN_MENU;
        drawMainMenu();
    }
}

void drawCreditsScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Outer cyberpunk borders
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    // Header
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- CYBERDECK CREDITS SCHEMA ---", 120, 12);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);
    
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString("OPERATIVE FIRMWARE DEV:", 120, 34);
    canvas.setTextColor(WHITE);
    canvas.drawCenterString("sl01220", 120, 46);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("(15-year-old developer)", 120, 56);
    
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString("ORIGINAL CORE DESIGN:", 120, 74);
    canvas.setTextColor(WHITE);
    canvas.drawCenterString("CD PROJEKT RED (CDPR)", 120, 86);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("sl01220 ported &", 120, 94);
    canvas.drawCenterString("developed the firmware", 120, 103);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("PRESS ENTER TO EXIT", 120, 116);
    
    pushCanvas();
}

void handleCreditsInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    if (status.enter || hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_MAIN_MENU;
        drawMainMenu();
    }
}

float currentHardwareScroll = 0;
float targetHardwareScroll = 0;
int hardwareMenuFocus = 0;
bool showHardwareDesc = false;
float hardwareDescAnimWidth = 0.0;

bool isSDCardManager = false;
int fileManagerSelected = 0;
bool showFileContent = false;
bool isSDFallback = false;
bool isFlashFallback = false;
int fileManagerScrollOffset = 0;
int loadingProgress = 0;
String fileManagerCurrentPath = "/";
String clipboardSourcePath = "";
String renameInputText = "";
int fileActionsMenuSelected = 0;
int settingsFocus = 0;
bool showSystemFiles = false;
unsigned long lastFileSelectionTime = 0;
int marqueeScrollOffset = 0;
unsigned long lastMarqueeUpdate = 0;
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *fileSD = nullptr;
AudioFileSourceSPIFFS *fileSPIFFS = nullptr;
AudioFileSourceBuffer *audioBuffer = nullptr;
AudioOutputM5Speaker *audioOut = nullptr;
bool isMp3Playing = false;
String mp3Filename = "";
unsigned long mp3StartTime = 0;
unsigned long mp3PausedTime = 0;
bool mp3IsPaused = false;
int mp3DurationSeconds = 180;
bool showImage = false;
String openedImageName = "";
float imageScale = 1.0f;
bool showBootMenu = false;
bool showSplashBootMenu = false;
int splashBootFocus = 0;

std::vector<String> dummyLogs = {
    "[ OK ] Init SPI flash layout...",
    "[ OK ] Mounted APP Partition.",
    "Mounting Secure File System...",
    "[ OK ] Mounted Secure FS.",
    "[ OK ] Init GPIO controllers.",
    "Starting Audio Initialization...",
    "[ OK ] Started Sound Card (I2S).",
    "[ OK ] Setting volume envelope.",
    "Starting Network Stack...",
    "[ OK ] IPv4/IPv6 Stack Active.",
    "[ OK ] Init Keyboard Matrix...",
    "[ OK ] Started Graphics Core.",
    "Bypassing Subnet Gateway...",
    "[ OK ] Bypass Successful.",
    "Allocating buffer memory...",
    "[ OK ] Memory allocated.",
    "[ OK ] Started Breach Protocol.",
    "Establishing secure tunnel...",
    "[ OK ] Tunnel established.",
    "[ OK ] System Boot Complete."
};
int logOffset = 0;
unsigned long lastLogUpdate = 0;

std::vector<RealFile> loadedFiles;
String fsStatusMessage = "";
std::vector<String> openedFileContent;
String openedFileName = "";

bool isSystemFile(String name) {
    String lower = name;
    lower.toLowerCase();
    if (lower == "system volume information") return true;
    if (lower.startsWith(".")) return true;
    return false;
}

bool deleteRecursive(String path) {
    if (isSDCardManager) {
        if (isSDFallback) return true;
        File dir = SD.open(path);
        if (!dir) return false;
        
        if (!dir.isDirectory()) {
            dir.close();
            return SD.remove(path);
        }
        
        std::vector<String> childPaths;
        std::vector<bool> childIsDir;
        
        File file = dir.openNextFile();
        while (file) {
            String childName = String(file.name());
            int lastSlash = childName.lastIndexOf('/');
            if (lastSlash >= 0) childName = childName.substring(lastSlash + 1);
            
            childPaths.push_back(path + (path.endsWith("/") ? "" : "/") + childName);
            childIsDir.push_back(file.isDirectory());
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
        
        for (size_t i = 0; i < childPaths.size(); i++) {
            if (childIsDir[i]) {
                deleteRecursive(childPaths[i]);
            } else {
                SD.remove(childPaths[i]);
            }
        }
        return SD.rmdir(path);
    } else {
        if (isFlashFallback) return true;
        File root = SPIFFS.open("/");
        if (!root) return false;
        
        std::vector<String> filesToDelete;
        File file = root.openNextFile();
        while (file) {
            String name = String(file.name());
            String matchPrefix = path;
            if (!matchPrefix.endsWith("/")) matchPrefix += "/";
            
            if (name.startsWith(matchPrefix) || name == path) {
                filesToDelete.push_back(name);
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        
        for (const String& f : filesToDelete) {
            SPIFFS.remove(f);
        }
        return true;
    }
}

bool compareFiles(const RealFile& a, const RealFile& b) {
    String aName = a.name;
    String bName = b.name;
    aName.toLowerCase();
    bName.toLowerCase();
    
    if (currentSortField == SORT_FIELD_NAME) {
        if (currentSortOrder == SORT_ORDER_ASC) {
            return aName < bName;
        } else {
            return aName > bName;
        }
    } else { // SORT_FIELD_TYPE
        if (a.isDir != b.isDir) {
            if (currentSortOrder == SORT_ORDER_ASC) {
                return a.isDir > b.isDir; // Directory first
            } else {
                return a.isDir < b.isDir; // Files first
            }
        }
        return aName < bName;
    }
}

void populateFileList() {
    loadedFiles.clear();
    fsStatusMessage = "";
    isSDFallback = false;
    isFlashFallback = false;
    
    // Insert parent directory ".." if we are in a subdirectory
    if (fileManagerCurrentPath != "/") {
        RealFile parentDir = {"..", "DIR", true};
        loadedFiles.push_back(parentDir);
    }
    
    if (isSDCardManager) {
        SPI.begin(40, 39, 14, 12);
        bool mountSuccess = SD.begin(12, SPI, 20000000);
        File root;
        if (mountSuccess) {
            root = SD.open(fileManagerCurrentPath);
        }
        
        if (!mountSuccess || !root || !root.isDirectory()) {
            isSDFallback = true;
            fsStatusMessage = "SD MOUNT FAIL (DEMO ACTIVE)";
            
            RealFile f1 = {"credentials.txt", "0.1 KB", false};
            RealFile f2 = {"subnet_node.sh", "0.1 KB", false};
            RealFile f3 = {"payload.bin", "1.2 KB", false};
            loadedFiles.push_back(f1);
            loadedFiles.push_back(f2);
            loadedFiles.push_back(f3);
            if (root) root.close();
        } else {
            File file = root.openNextFile();
            while (file && loadedFiles.size() < 100) {
                RealFile rf;
                rf.name = String(file.name());
                int lastSlashIdx = rf.name.lastIndexOf('/');
                if (lastSlashIdx >= 0) {
                    rf.name = rf.name.substring(lastSlashIdx + 1);
                }
                
                if (!showSystemFiles && isSystemFile(rf.name)) {
                    file = root.openNextFile();
                    continue;
                }
                
                rf.isDir = file.isDirectory();
                if (rf.isDir) {
                    rf.sizeStr = "DIR";
                } else {
                    rf.sizeStr = String(file.size() / 1024.0, 1) + " KB";
                }
                loadedFiles.push_back(rf);
                file = root.openNextFile();
            }
            root.close();
            // If the folder is empty (excluding ".." if present)
            int minSize = (fileManagerCurrentPath != "/") ? 1 : 0;
            if ((int)loadedFiles.size() <= minSize) {
                isSDFallback = true;
                fsStatusMessage = "SD CARD EMPTY (DEMO ACTIVE)";
                RealFile f1 = {"credentials.txt", "0.1 KB", false};
                RealFile f2 = {"subnet_node.sh", "0.1 KB", false};
                RealFile f3 = {"payload.bin", "1.2 KB", false};
                loadedFiles.push_back(f1);
                loadedFiles.push_back(f2);
                loadedFiles.push_back(f3);
            }
        }
    } else {
        bool mountSuccess = SPIFFS.begin(true);
        File root;
        if (mountSuccess) {
            root = SPIFFS.open("/");
        }
        
        if (!mountSuccess || !root) {
            isFlashFallback = true;
            fsStatusMessage = "FLASH MOUNT FAIL (DEMO ACTIVE)";
            RealFile f1 = {"deck_config.json", "0.1 KB", false};
            RealFile f2 = {"breach_log.txt", "0.1 KB", false};
            RealFile f3 = {"system.ini", "0.1 KB", false};
            loadedFiles.push_back(f1);
            loadedFiles.push_back(f2);
            loadedFiles.push_back(f3);
            if (root) root.close();
        } else {
            File file = root.openNextFile();
            while (file && loadedFiles.size() < 100) {
                RealFile rf;
                rf.name = String(file.name());
                if (rf.name.startsWith("/")) rf.name.remove(0, 1);
                
                if (!showSystemFiles && isSystemFile(rf.name)) {
                    file = root.openNextFile();
                    continue;
                }
                
                rf.isDir = file.isDirectory();
                if (rf.isDir) {
                    rf.sizeStr = "DIR";
                } else {
                    rf.sizeStr = String(file.size() / 1024.0, 1) + " KB";
                }
                loadedFiles.push_back(rf);
                file = root.openNextFile();
            }
            root.close();
            int minSize = (fileManagerCurrentPath != "/") ? 1 : 0;
            if ((int)loadedFiles.size() <= minSize) {
                isFlashFallback = true;
                fsStatusMessage = "FLASH EMPTY (DEMO ACTIVE)";
                RealFile f1 = {"deck_config.json", "0.1 KB", false};
                RealFile f2 = {"breach_log.txt", "0.1 KB", false};
                RealFile f3 = {"system.ini", "0.1 KB", false};
                loadedFiles.push_back(f1);
                loadedFiles.push_back(f2);
                loadedFiles.push_back(f3);
            }
        }
    }
    
    // Sort files based on settings (keeping ".." at the top if present)
    if (!loadedFiles.empty()) {
        int sortStart = (fileManagerCurrentPath != "/") ? 1 : 0;
        if ((int)loadedFiles.size() > sortStart) {
            std::sort(loadedFiles.begin() + sortStart, loadedFiles.end(), compareFiles);
        }
    }
    
    lastFileSelectionTime = millis();
    marqueeScrollOffset = 0;
}

void readSelectedFileContent(String fileName) {
    openedFileContent.clear();
    openedFileName = fileName;
    String path = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + fileName;
    
    if (isSDCardManager) {
        if (isSDFallback) {
            if (fileName == "credentials.txt") {
                openedFileContent.push_back("[system_credentials]");
                openedFileContent.push_back("admin:cYbErPuNk2077");
                openedFileContent.push_back("net_gate:ice_breaker");
            } else if (fileName == "subnet_node.sh") {
                openedFileContent.push_back("#!/bin/bash");
                openedFileContent.push_back("nmap -sS -O 10.0.2.15");
                openedFileContent.push_back("bypass_firewall --level 3");
            } else {
                openedFileContent.push_back("0xDEADBEEF 0xCAFEBABE");
                openedFileContent.push_back("0x00FF00FF 0x12345678");
                openedFileContent.push_back("VULNERABILITY DETECTED");
            }
            return;
        }
        
        SPI.begin(40, 39, 14, 12);
        if (!SD.begin(12, SPI, 20000000)) {
            openedFileContent.push_back("SD Card Mount Error");
            return;
        }
        File f = SD.open(path);
        if (!f) {
            openedFileContent.push_back("Could not open file.");
            return;
        }
        if (f.isDirectory()) {
            openedFileContent.push_back("Cannot read directory.");
            f.close();
            return;
        }
        
        int lineCount = 0;
        while (f.available() && lineCount < 4) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            if (line.length() > 25) line = line.substring(0, 25) + "...";
            openedFileContent.push_back(line);
            lineCount++;
        }
        f.close();
    } else {
        if (isFlashFallback) {
            if (fileName == "deck_config.json") {
                openedFileContent.push_back("{");
                openedFileContent.push_back("  \"deck_id\": \"CYBER_D_01\",");
                openedFileContent.push_back("  \"os\": \"cyber_os_7.0\"");
                openedFileContent.push_back("}");
            } else if (fileName == "breach_log.txt") {
                openedFileContent.push_back("BREACH ACCESS LOG:");
                openedFileContent.push_back("OP: sl01220");
                openedFileContent.push_back("STATUS: COMPLETED");
            } else {
                openedFileContent.push_back("[system]");
                openedFileContent.push_back("node=BP_X1");
                openedFileContent.push_back("security=HIGH");
            }
            return;
        }
        
        if (!SPIFFS.begin(true)) {
            openedFileContent.push_back("Flash Mount Error");
            return;
        }
        File f = SPIFFS.open(path);
        if (!f) {
            openedFileContent.push_back("Could not open file.");
            return;
        }
        if (f.isDirectory()) {
            openedFileContent.push_back("Cannot read directory.");
            f.close();
            return;
        }
        
        int lineCount = 0;
        while (f.available() && lineCount < 4) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            if (line.length() > 25) line = line.substring(0, 25) + "...";
            openedFileContent.push_back(line);
            lineCount++;
        }
        f.close();
    }
}

void drawHardwareMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw title
    drawGlitchText("HARDWARE NODE", 135, 12, 2, CP_CYAN, true, true);
    drawGlitchText("OPERATIVE: " + (isGuest ? String("GUEST") : authUser), 135, 34, 1, CP_DIM);
    
    // Draw rotating wheel arc
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    int totalItems = 4;
    std::vector<String> labels = {"FLASH MEMORY", "SD CARD", "SETTINGS", "BACK"};
    
    for (int i = 0; i < totalItems; i++) {
        float rawOffset = i - currentHardwareScroll;
        float offset = fmod(rawOffset, (float)totalItems);
        float halfItems = (float)totalItems / 2.0;
        if (offset > halfItems) offset -= (float)totalItems;
        if (offset < -halfItems) offset += (float)totalItems;
        
        if (offset < -0.5) continue;
        
        float angle = offset * 0.391;
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == hardwareMenuFocus);
        uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;
        
        float tickEndX = -80 + cos(angle) * (isSelected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (isSelected ? 117 : 115);
        
        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
        if (isSelected) {
            canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);
        }
        
        float scale = 1.0 - abs(offset) * 0.3333;
        if (scale < 0.1) scale = 0.1;
        float h = 30.0 * scale;
        float y = tickY - h / 2.0;
        float w = 195.0 * scale;
        float x = tickX + 10;
        
        int textSize = isSelected ? 2 : 1;
        uint16_t color = isSelected ? CP_YELLOW : CP_DIM;
        
        drawChippedButton(x, y, w, h, color);
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
    }
    
    if (hardwareDescAnimWidth >= 10.0) {
        int x = 40;
        int y = 52;
        int h = 30;
        canvas.fillRect(x, y, (int)hardwareDescAnimWidth, h, CP_BG);
        drawChippedButton(x, y, (int)hardwareDescAnimWidth, h, CP_YELLOW);
        
        if (hardwareDescAnimWidth > 160.0) {
            canvas.setTextColor(CP_YELLOW);
            String label = labels[hardwareMenuFocus];
            String line1 = "";
            String line2 = "";
            if (label == "FLASH MEMORY") {
                line1 = "Flash";
                line2 = "memory";
            } else if (label == "SD CARD") {
                line1 = "SD card";
                line2 = "storage";
            } else if (label == "SETTINGS") {
                line1 = "Sorting";
                line2 = "preferences";
            } else if (label == "BACK") {
                line1 = "Return to";
                line2 = "terminal";
            }
            
            if (line1 != "") {
                canvas.setTextSize(2);
                canvas.setCursor(x + 10, y + 0);
                canvas.print(line1);
                canvas.setCursor(x + 10, y + 14);
                canvas.print(line2);
            }
        }
    }
    
    pushCanvas();
}

void handleHardwareMenuInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    bool hasRight = false;
    bool hasLeft = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '/') hasRight = true;
        if (c == ',') hasLeft = true;
    }
    
    if (showHardwareDesc) {
        if (hasLeft || hasUp || hasDown) {
            playSound(sound_select, sound_select_size);
            showHardwareDesc = false;
            return;
        }
    } else {
        if (hasRight && (hardwareMenuFocus == 0 || hardwareMenuFocus == 1 || hardwareMenuFocus == 2)) {
            playSound(sound_select, sound_select_size);
            showHardwareDesc = true;
            return;
        }
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        showHardwareDesc = false;
        hardwareDescAnimWidth = 0.0;
        
        if (hardwareMenuFocus == 0) {
            isSDCardManager = false;
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            showFileContent = false;
        } else if (hardwareMenuFocus == 1) {
            isSDCardManager = true;
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            showFileContent = false;
        } else if (hardwareMenuFocus == 2) {
            appState = STATE_HARDWARE_SETTINGS;
            drawHardwareSettings();
        } else if (hardwareMenuFocus == 3) {
            appState = STATE_SPLASH;
            drawSplash();
        }
        return;
    }
    
    if (!showHardwareDesc) {
        int maxFocus = 3;
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            hardwareMenuFocus--;
            if (hardwareMenuFocus < 0) hardwareMenuFocus = maxFocus;
            targetHardwareScroll -= 1.0;
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            hardwareMenuFocus++;
            if (hardwareMenuFocus > maxFocus) hardwareMenuFocus = 0;
            targetHardwareScroll += 1.0;
        }
    }
}

void drawHardwareSettings() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- SORT SETTINGS SCHEMA ---", 120, 10);
    canvas.drawLine(10, 20, 230, 20, CP_CYAN);
    
    // Row 0: Sort Field
    bool focusField = (settingsFocus == 0);
    uint16_t fieldBorderColor = focusField ? CP_YELLOW : CP_DIM;
    canvas.fillRect(15, 24, 210, 15, focusField ? canvas.color565(30, 30, 30) : CP_BG);
    canvas.drawRect(15, 24, 210, 15, fieldBorderColor);
    
    canvas.setTextColor(focusField ? CP_YELLOW : WHITE);
    canvas.setCursor(22, 28);
    canvas.print("SORT BY:");
    
    canvas.setTextColor(focusField ? WHITE : CP_DIM);
    canvas.setCursor(120, 28);
    canvas.print(currentSortField == SORT_FIELD_NAME ? "< NAME >" : "< TYPE >");
    
    // Row 1: Sort Order
    bool focusOrder = (settingsFocus == 1);
    uint16_t orderBorderColor = focusOrder ? CP_YELLOW : CP_DIM;
    canvas.fillRect(15, 41, 210, 15, focusOrder ? canvas.color565(30, 30, 30) : CP_BG);
    canvas.drawRect(15, 41, 210, 15, orderBorderColor);
    
    canvas.setTextColor(focusOrder ? CP_YELLOW : WHITE);
    canvas.setCursor(22, 45);
    canvas.print("ORDER:");
    
    canvas.setTextColor(focusOrder ? WHITE : CP_DIM);
    canvas.setCursor(120, 45);
    canvas.print(currentSortOrder == SORT_ORDER_ASC ? "< ASCENDING >" : "< DESCENDING >");
    
    // Row 2: System Files
    bool focusSys = (settingsFocus == 2);
    uint16_t sysBorderColor = focusSys ? CP_YELLOW : CP_DIM;
    canvas.fillRect(15, 58, 210, 15, focusSys ? canvas.color565(30, 30, 30) : CP_BG);
    canvas.drawRect(15, 58, 210, 15, sysBorderColor);
    
    canvas.setTextColor(focusSys ? CP_YELLOW : WHITE);
    canvas.setCursor(22, 62);
    canvas.print("SYS FILES:");
    
    canvas.setTextColor(focusSys ? WHITE : CP_DIM);
    canvas.setCursor(120, 62);
    canvas.print(showSystemFiles ? "< SHOW >" : "< HIDE >");

    // Row 3: Glitch Text
    bool focusGlitch = (settingsFocus == 3);
    uint16_t glitchBorderColor = focusGlitch ? CP_YELLOW : CP_DIM;
    canvas.fillRect(15, 75, 210, 15, focusGlitch ? canvas.color565(30, 30, 30) : CP_BG);
    canvas.drawRect(15, 75, 210, 15, glitchBorderColor);
    
    canvas.setTextColor(focusGlitch ? CP_YELLOW : WHITE);
    canvas.setCursor(22, 79);
    canvas.print("GLITCH TEXT:");
    
    canvas.setTextColor(focusGlitch ? WHITE : CP_DIM);
    canvas.setCursor(120, 79);
    String glitchLabel = "";
    if (insaneMode == 0) glitchLabel = "< OFF >";
    else if (insaneMode == 1) glitchLabel = "< ON >";
    else glitchLabel = "< INSANE >";
    canvas.print(glitchLabel);

    // Row 4: Boot Option
    bool focusBoot = (settingsFocus == 4);
    uint16_t bootBorderColor = focusBoot ? CP_YELLOW : CP_DIM;
    canvas.fillRect(15, 92, 210, 15, focusBoot ? canvas.color565(30, 30, 30) : CP_BG);
    canvas.drawRect(15, 92, 210, 15, bootBorderColor);
    
    canvas.setTextColor(focusBoot ? CP_YELLOW : WHITE);
    canvas.setCursor(22, 96);
    canvas.print("BOOT OPTION:");
    
    canvas.setTextColor(focusBoot ? WHITE : CP_DIM);
    canvas.setCursor(120, 96);
    canvas.print("< LAUNCH MENU >");
    
    // Footer hints
    if (settingsFocus == 4) {
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ENTER TO BOOT SYSTEM", 120, 114);
    } else {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("UP/DN: SELECT ROW  |  LF/RT: CHANGE", 120, 110);
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("ENTER: APPLY  |  ESC/COMMA: BACK", 120, 120);
    }
    
    pushCanvas();
}

void handleHardwareSettingsInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_HARDWARE_MENU;
        drawHardwareMenu();
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (settingsFocus == 4) {
            appState = STATE_SPLASH;
            showSplashBootMenu = true;
            splashBootFocus = 0;
            logOffset = 0;
            drawSplash();
            return;
        }
        prefs.putInt("insane", insaneMode);
        populateFileList(); // Reload files with the new system files toggle filter!
        fileManagerSelected = 0;
        fileManagerScrollOffset = 0;
        appState = STATE_HARDWARE_MENU;
        drawHardwareMenu();
        return;
    }
    
    bool hasUp = false, hasDown = false;
    bool hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        settingsFocus = (settingsFocus - 1 + 5) % 5;
        drawHardwareSettings();
    }
    if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        settingsFocus = (settingsFocus + 1) % 5;
        drawHardwareSettings();
    }
    
    if (hasLeft || hasRight) {
        playSound(sound_hover, sound_hover_size);
        if (settingsFocus == 0) {
            // Toggle Sort Field
            currentSortField = (currentSortField == SORT_FIELD_NAME) ? SORT_FIELD_TYPE : SORT_FIELD_NAME;
        } else if (settingsFocus == 1) {
            // Toggle Sort Order
            currentSortOrder = (currentSortOrder == SORT_ORDER_ASC) ? SORT_ORDER_DESC : SORT_ORDER_ASC;
        } else if (settingsFocus == 2) {
            // Toggle System Files visibility
            showSystemFiles = !showSystemFiles;
        } else if (settingsFocus == 3) {
            // Cycle Glitch Text mode
            if (hasLeft) {
                insaneMode = (insaneMode - 1 + 3) % 3;
            } else {
                insaneMode = (insaneMode + 1) % 3;
            }
        }
        drawHardwareSettings();
    }
}

void drawFileManager(bool push) {
    if (showImage) {
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        
        canvas.drawRect(5, 5, 230, 125, CP_CYAN);
        canvas.drawRect(7, 7, 226, 121, CP_DIM);
        
        String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + openedImageName;
        
        if (isSDCardManager) {
            canvas.drawJpgFile(SD, fullPath.c_str(), 8, 8, 224, 105, 0, 0, imageScale, imageScale);
        } else {
            canvas.drawJpgFile(SPIFFS, fullPath.c_str(), 8, 8, 224, 105, 0, 0, imageScale, imageScale);
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        char scaleStr[48];
        sprintf(scaleStr, "ZOOM: %.2fx | UP/DOWN: ZOOM | ESC: EXIT", imageScale);
        canvas.drawCenterString(scaleStr, 120, 115);
        
        if (push) {
            pushCanvas();
        }
        return;
    }
    
    if (isMp3Playing) {
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        
        canvas.drawRect(5, 5, 230, 125, CP_CYAN);
        canvas.drawRect(7, 7, 226, 121, CP_DIM);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        canvas.drawCenterString("--- NEURAL MUSIC LINK ---", 120, 12);
        canvas.drawLine(10, 24, 230, 24, CP_CYAN);
        
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString("NOW DECODING:", 120, 32);
        
        canvas.setTextColor(WHITE);
        String nameDisp = mp3Filename;
        if (nameDisp.length() > 22) {
            nameDisp = nameDisp.substring(0, 19) + "...";
        }
        canvas.drawCenterString(nameDisp, 120, 44);
        
        // Progress Bar
        canvas.drawRect(40, 56, 160, 4, CP_DIM);
        uint32_t elapsed = (millis() - mp3StartTime) / 1000;
        uint32_t duration = mp3DurationSeconds;
        int progressW = (elapsed * 160) / duration;
        if (progressW > 160) progressW = 160;
        canvas.fillRect(40, 56, progressW, 4, CP_CYAN);
        
        // Time Label
        char timeStr[32];
        sprintf(timeStr, "%02d:%02d / %02d:%02d", 
                (int)(elapsed / 60), (int)(elapsed % 60), 
                (int)(duration / 60), (int)(duration % 60));
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString(timeStr, 120, 64);
        
        // Graphic Equalizer
        int startX = 60;
        for (int i = 0; i < 10; i++) {
            int h = random(3, 24);
            canvas.fillRect(startX + i * 11, 102 - h, 7, h, CP_CYAN);
            canvas.drawRect(startX + i * 11, 102 - h, 7, h, CP_YELLOW);
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ANY KEY TO STOP", 120, 114);
        
        if (push) {
            pushCanvas();
        }
        return;
    }
    
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw title and outer frames
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    if (isSDCardManager) {
        String title = "SD:" + fileManagerCurrentPath;
        if (title.length() > 30) title = "SD:..." + title.substring(title.length() - 25);
        canvas.drawCenterString("--- " + title + " ---", 120, 12);
    } else {
        String title = "FLASH:" + fileManagerCurrentPath;
        if (title.length() > 30) title = "FLASH:..." + title.substring(title.length() - 25);
        canvas.drawCenterString("--- " + title + " ---", 120, 12);
    }
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);
    
    if (!showFileContent) {
        if (fsStatusMessage != "" && loadedFiles.empty()) {
            canvas.setTextColor(CP_RED);
            canvas.drawCenterString(fsStatusMessage, 120, 65);
        } else {
            if (fsStatusMessage != "") {
                canvas.setTextColor(CP_YELLOW);
                canvas.setCursor(15, 27);
                canvas.print(fsStatusMessage);
            }
            
            // Draw file list with scroll viewport paging
            int startY = fsStatusMessage != "" ? 42 : 32;
            int maxShow = min(5, (int)loadedFiles.size() - fileManagerScrollOffset);
            for (int i = 0; i < maxShow; i++) {
                int fileIdx = fileManagerScrollOffset + i;
                bool isSel = (fileIdx == fileManagerSelected);
                uint16_t color = isSel ? CP_YELLOW : WHITE;
                
                if (isSel) {
                    canvas.fillRect(10, startY - 2, 210, 14, canvas.color565(30, 30, 30));
                    canvas.drawRect(10, startY - 2, 210, 14, CP_CYAN);
                }
                
                canvas.setTextColor(color);
                canvas.setCursor(15, startY);
                
                // Truncate name if it's too long to fit with the scrollbar, or marquee scroll if selected
                String nameToPrint = loadedFiles[fileIdx].name;
                if (isSel && nameToPrint.length() > 18) {
                    if (millis() - lastFileSelectionTime > 1000) {
                        int maxScroll = nameToPrint.length() - 18;
                        if (marqueeScrollOffset <= maxScroll) {
                            nameToPrint = nameToPrint.substring(marqueeScrollOffset);
                        } else {
                            if (millis() - lastFileSelectionTime > 1000 + maxScroll * 250 + 1000) {
                                lastFileSelectionTime = millis();
                                marqueeScrollOffset = 0;
                            }
                            nameToPrint = nameToPrint.substring(maxScroll);
                        }
                    }
                    if (nameToPrint.length() > 18) {
                        nameToPrint = nameToPrint.substring(0, 18);
                    }
                } else if (nameToPrint.length() > 18) {
                    nameToPrint = nameToPrint.substring(0, 15) + "...";
                }
                canvas.print(nameToPrint);
                
                canvas.setCursor(155, startY);
                canvas.print(loadedFiles[fileIdx].sizeStr);
                
                startY += 14;
            }
            
            // Draw vertical scrollbar on the right side (x = 224)
            int totalFiles = loadedFiles.size();
            int trackY = fsStatusMessage != "" ? 42 : 32;
            int trackH = fsStatusMessage != "" ? 66 : 70;
            canvas.drawFastVLine(224, trackY, trackH, CP_DIM);
            if (totalFiles > 5) {
                int barH = (trackH * 5) / totalFiles;
                if (barH < 10) barH = 10;
                int scrollRange = totalFiles - 5;
                int trackRange = trackH - barH;
                int barY = trackY + (fileManagerScrollOffset * trackRange) / scrollRange;
                canvas.fillRect(223, barY, 3, barH, CP_CYAN);
            } else if (totalFiles > 0) {
                canvas.fillRect(223, trackY, 3, trackH, CP_CYAN);
            }
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("ENTER: OPEN  |  ESC/COMMA: BACK", 120, 114);
    } else {
        // Draw selected file content panel
        canvas.setTextColor(CP_CYAN);
        canvas.setCursor(15, 32);
        canvas.print("FILE: " + openedFileName);
        canvas.drawLine(12, 44, 228, 44, CP_CYAN);
        
        canvas.setTextColor(WHITE);
        int startY = 50;
        for (size_t i = 0; i < openedFileContent.size(); i++) {
            canvas.setCursor(15, startY);
            canvas.print(openedFileContent[i]);
            startY += 12;
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS COMMA OR ESC TO CLOSE", 120, 114);
    }
    
    if (push) {
        pushCanvas();
    }
}

void handleFileManagerInput(Keyboard_Class::KeysState status) {
    if (showImage) {
        bool hasZoomIn = false, hasZoomOut = false;
        bool hasExit = false;
        for (char c : status.word) {
            if (c == ';') hasZoomIn = true;
            if (c == '.') hasZoomOut = true;
            if (c == ',' || c == '`') hasExit = true;
        }
        if (status.enter || status.del) hasExit = true;
        
        if (hasZoomIn) {
            playSound(sound_hover, sound_hover_size);
            imageScale += 0.25f;
            if (imageScale > 4.0f) imageScale = 4.0f;
            drawFileManager();
        } else if (hasZoomOut) {
            playSound(sound_hover, sound_hover_size);
            imageScale -= 0.25f;
            if (imageScale < 0.25f) imageScale = 0.25f;
            drawFileManager();
        } else if (hasExit) {
            playSound(sound_select, sound_select_size);
            showImage = false;
            drawFileManager();
        }
        return;
    }
    
    if (isMp3Playing) {
        // Any keypress stops the MP3 playback
        playSound(sound_select, sound_select_size);
        stopMp3();
        return;
    }
    
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (showFileContent) {
        if (hasBack || status.enter) {
            playSound(sound_select, sound_select_size);
            showFileContent = false;
            drawFileManager();
        }
        return;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (fileManagerCurrentPath != "/") {
            int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
            if (lastSlash == 0) {
                fileManagerCurrentPath = "/";
            } else if (lastSlash > 0) {
                fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
            }
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            populateFileList();
            drawFileManager();
        } else {
            appState = STATE_HARDWARE_MENU;
            drawHardwareMenu();
        }
        return;
    }
    
    if (status.enter && !loadedFiles.empty()) {
        playSound(sound_select, sound_select_size);
        if (loadedFiles[fileManagerSelected].name == "..") {
            int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
            if (lastSlash == 0) {
                fileManagerCurrentPath = "/";
            } else if (lastSlash > 0) {
                fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
            }
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            populateFileList();
            drawFileManager();
        } else {
            appState = STATE_FILE_ACTIONS_MENU;
            fileActionsMenuSelected = 0;
            drawFileActionsMenu();
        }
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    int maxIdx = loadedFiles.empty() ? 0 : loadedFiles.size() - 1;
    
    if (hasUp && maxIdx > 0) {
        playSound(sound_hover, sound_hover_size);
        fileManagerSelected--;
        if (fileManagerSelected < 0) {
            fileManagerSelected = maxIdx;
            if (maxIdx > 4) {
                fileManagerScrollOffset = maxIdx - 4;
            } else {
                fileManagerScrollOffset = 0;
            }
        } else {
            if (fileManagerSelected < fileManagerScrollOffset) {
                fileManagerScrollOffset = fileManagerSelected;
            }
        }
        lastFileSelectionTime = millis();
        marqueeScrollOffset = 0;
        drawFileManager();
    }
    if (hasDown && maxIdx > 0) {
        playSound(sound_hover, sound_hover_size);
        fileManagerSelected++;
        if (fileManagerSelected > maxIdx) {
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
        } else {
            if (fileManagerSelected > fileManagerScrollOffset + 4) {
                fileManagerScrollOffset = fileManagerSelected - 4;
            }
        }
        lastFileSelectionTime = millis();
        marqueeScrollOffset = 0;
        drawFileManager();
    }
}

void stopMp3() {
    isMp3Playing = false;
    mp3IsPaused = false;
    if (mp3) {
        mp3->stop();
        delete mp3;
        mp3 = nullptr;
    }
    if (audioBuffer) {
        audioBuffer->close();
        delete audioBuffer;
        audioBuffer = nullptr;
    }
    if (fileSD) {
        fileSD->close();
        delete fileSD;
        fileSD = nullptr;
    }
    if (fileSPIFFS) {
        fileSPIFFS->close();
        delete fileSPIFFS;
        fileSPIFFS = nullptr;
    }
    if (audioOut) {
        audioOut->stop();
        delete audioOut;
        audioOut = nullptr;
    }
    appState = STATE_FILE_MANAGER;
    drawFileManager();
}

void startMp3(String fileName) {
    String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + fileName;
    
    audioOut = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);
    
    bool started = false;
    if (isSDCardManager) {
        fileSD = new AudioFileSourceSD(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSD, 8192);
        mp3 = new AudioGeneratorMP3();
        started = mp3->begin(audioBuffer, audioOut);
    } else {
        fileSPIFFS = new AudioFileSourceSPIFFS(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSPIFFS, 8192);
        mp3 = new AudioGeneratorMP3();
        started = mp3->begin(audioBuffer, audioOut);
    }
    
    if (started) {
        isMp3Playing = true;
        mp3Filename = fileName;
        mp3IsPaused = false;
        mp3StartTime = millis();
        mp3PausedTime = 0;
        mp3DurationSeconds = 180;
        appState = STATE_FILE_MANAGER;
        showFileContent = false;
    } else {
        if (mp3) { delete mp3; mp3 = nullptr; }
        if (audioBuffer) { delete audioBuffer; audioBuffer = nullptr; }
        if (fileSD) { delete fileSD; fileSD = nullptr; }
        if (fileSPIFFS) { delete fileSPIFFS; fileSPIFFS = nullptr; }
        if (audioOut) { delete audioOut; audioOut = nullptr; }
        
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("FILE IO ERROR", 120, 50);
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ANY KEY", 120, 80);
        pushCanvas();
        delay(1500);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
    }
}

void drawFileActionsMenu() {
    // First, draw the file manager background behind the pop-up menu!
    drawFileManager(false);
    
    // Draw pop-up overlay
    canvas.startWrite();
    
    // Pop-up border and box
    canvas.fillRect(35, 20, 170, 95, CP_BG);
    canvas.drawRect(35, 20, 170, 95, CP_CYAN);
    canvas.drawRect(37, 22, 166, 91, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- ACTION MENU ---", 120, 26);
    canvas.drawLine(40, 36, 200, 36, CP_CYAN);
    
    int startY = 41;
    bool hasPaste = (clipboardSourcePath != "");
    std::vector<String> options = {"1. OPEN", "2. RENAME", "3. DELETE", "4. COPY", "5. PASTE"};
    
    for (int i = 0; i < 5; i++) {
        bool isSel = (i == fileActionsMenuSelected);
        uint16_t color = isSel ? CP_YELLOW : WHITE;
        
        if (i == 4 && !hasPaste) { // PASTE is disabled
            color = CP_DIM;
        }
        
        if (isSel) {
            canvas.fillRect(42, startY - 2, 156, 12, canvas.color565(30, 30, 30));
            canvas.drawRect(42, startY - 2, 156, 12, CP_CYAN);
        }
        
        canvas.setTextColor(color);
        canvas.setCursor(48, startY);
        canvas.print(options[i]);
        
        if (i == 4 && !hasPaste) {
            canvas.setCursor(120, startY);
            canvas.print("[EMPTY]");
        }
        
        startY += 12;
    }
    
    pushCanvas();
}

void handleFileActionsMenuInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        fileActionsMenuSelected--;
        if (fileActionsMenuSelected < 0) fileActionsMenuSelected = 4;
        drawFileActionsMenu();
    }
    if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        fileActionsMenuSelected++;
        if (fileActionsMenuSelected > 4) fileActionsMenuSelected = 0;
        drawFileActionsMenu();
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        bool hasPaste = (clipboardSourcePath != "");
        
        if (fileActionsMenuSelected == 4 && !hasPaste) {
            // Paste is disabled
            return;
        }
        
        RealFile targetFile = loadedFiles[fileManagerSelected];
        String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + targetFile.name;
        
        if (fileActionsMenuSelected == 0) { // OPEN
            if (targetFile.name == "..") {
                // Navigate up
                int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
                if (lastSlash == 0) {
                    fileManagerCurrentPath = "/";
                } else if (lastSlash > 0) {
                    fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
                }
                fileManagerSelected = 0;
                fileManagerScrollOffset = 0;
                populateFileList();
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else if (targetFile.isDir) {
                // Navigate down
                if (fileManagerCurrentPath == "/") {
                    fileManagerCurrentPath = "/" + targetFile.name;
                } else {
                    fileManagerCurrentPath = fileManagerCurrentPath + "/" + targetFile.name;
                }
                fileManagerSelected = 0;
                fileManagerScrollOffset = 0;
                populateFileList();
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                String lowerName = targetFile.name;
                lowerName.toLowerCase();
                if (lowerName.endsWith(".mp3")) {
                    startMp3(targetFile.name);
                } else if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) {
                    playSound(sound_select, sound_select_size);
                    openedImageName = targetFile.name;
                    imageScale = 1.0f;
                    showImage = true;
                    appState = STATE_FILE_MANAGER;
                    drawFileManager();
                } else {
                    // Open file content
                    readSelectedFileContent(targetFile.name);
                    showFileContent = true;
                    appState = STATE_FILE_MANAGER;
                    drawFileManager();
                }
            }
        }
        else if (fileActionsMenuSelected == 1) { // RENAME
            if (targetFile.name != "..") {
                renameInputText = targetFile.name;
                appState = STATE_FILE_RENAME_INPUT;
                drawFileRenameInput();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 2) { // DELETE
            if (targetFile.name != "..") {
                if (isSDCardManager) {
                    if (isSDFallback) {
                        // Mock delete in demo mode
                        loadedFiles.erase(loadedFiles.begin() + fileManagerSelected);
                    } else {
                        // Physical delete
                        deleteRecursive(fullPath);
                        populateFileList();
                    }
                } else {
                    if (isFlashFallback) {
                        loadedFiles.erase(loadedFiles.begin() + fileManagerSelected);
                    } else {
                        deleteRecursive(fullPath);
                        populateFileList();
                    }
                }
                if (fileManagerSelected >= (int)loadedFiles.size()) {
                    fileManagerSelected = (int)loadedFiles.size() - 1;
                }
                if (fileManagerSelected < 0) {
                    fileManagerSelected = 0;
                }
                
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected < fileManagerScrollOffset) {
                        fileManagerScrollOffset = fileManagerSelected;
                    } else if (fileManagerSelected > fileManagerScrollOffset + 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    }
                    if (fileManagerScrollOffset + 5 > (int)loadedFiles.size()) {
                        fileManagerScrollOffset = (int)loadedFiles.size() - 5;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 3) { // COPY
            if (targetFile.name != "..") {
                clipboardSourcePath = fullPath;
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 4) { // PASTE
            String sourceFilename = clipboardSourcePath;
            int lastSlash = sourceFilename.lastIndexOf('/');
            if (lastSlash >= 0) sourceFilename = sourceFilename.substring(lastSlash + 1);
            
            String destPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + sourceFilename;
            
            if (isSDCardManager) {
                if (isSDFallback) {
                    RealFile pf = {sourceFilename, "1.0 KB", false};
                    loadedFiles.push_back(pf);
                } else {
                    // Physical paste
                    File src = SD.open(clipboardSourcePath, FILE_READ);
                    File dst = SD.open(destPath, FILE_WRITE);
                    if (src && dst) {
                        uint8_t buf[256];
                        while (src.available()) {
                            int len = src.read(buf, sizeof(buf));
                            dst.write(buf, len);
                        }
                    }
                    if (src) src.close();
                    if (dst) dst.close();
                    populateFileList();
                }
            } else {
                if (isFlashFallback) {
                    RealFile pf = {sourceFilename, "1.0 KB", false};
                    loadedFiles.push_back(pf);
                } else {
                    // Physical paste
                    File src = SPIFFS.open(clipboardSourcePath, FILE_READ);
                    File dst = SPIFFS.open(destPath, FILE_WRITE);
                    if (src && dst) {
                        uint8_t buf[256];
                        while (src.available()) {
                            int len = src.read(buf, sizeof(buf));
                            dst.write(buf, len);
                        }
                    }
                    if (src) src.close();
                    if (dst) dst.close();
                    populateFileList();
                }
            }
            int foundIdx = -1;
            for (size_t i = 0; i < loadedFiles.size(); i++) {
                if (loadedFiles[i].name == sourceFilename) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx >= 0) {
                fileManagerSelected = foundIdx;
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected > 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    } else {
                        fileManagerScrollOffset = 0;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
            }
            appState = STATE_FILE_MANAGER;
            drawFileManager();
        }
    }
}

void drawFileRenameInput() {
    // First, draw the file manager background behind the rename dialog!
    drawFileManager(false);
    
    // Draw dialog overlay
    canvas.startWrite();
    canvas.fillRect(25, 35, 190, 65, CP_BG);
    canvas.drawRect(25, 35, 190, 65, CP_CYAN);
    canvas.drawRect(27, 37, 186, 61, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- RENAME FILE ---", 120, 42);
    
    // Input box
    canvas.drawRect(35, 56, 170, 16, CP_CYAN);
    canvas.fillRect(36, 57, 168, 14, canvas.color565(30, 30, 30));
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(40, 60);
    // Draw typed name with a blink cursor
    String disp = renameInputText;
    if (disp.length() > 20) disp = disp.substring(disp.length() - 20);
    if (blinkState) disp += "_";
    canvas.print(disp);
    
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("ENTER: RENAME  |  ESC/COMMA: BACK", 120, 80);
    
    pushCanvas();
}

void handleFileRenameInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_FILE_ACTIONS_MENU;
        drawFileActionsMenu();
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (renameInputText.length() > 0) {
            RealFile targetFile = loadedFiles[fileManagerSelected];
            String oldPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + targetFile.name;
            String newPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + renameInputText;
            
            if (isSDCardManager) {
                if (isSDFallback) {
                    loadedFiles[fileManagerSelected].name = renameInputText;
                } else {
                    SD.rename(oldPath, newPath);
                    populateFileList();
                }
            } else {
                if (isFlashFallback) {
                    loadedFiles[fileManagerSelected].name = renameInputText;
                } else {
                    SPIFFS.rename(oldPath, newPath);
                    populateFileList();
                }
            }
            
            // Relocate renamed file in the sorted list
            int foundIdx = -1;
            for (size_t i = 0; i < loadedFiles.size(); i++) {
                if (loadedFiles[i].name == renameInputText) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx >= 0) {
                fileManagerSelected = foundIdx;
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected > 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    } else {
                        fileManagerScrollOffset = 0;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
            }
        }
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }
    
    if (status.del && renameInputText.length() > 0) {
        renameInputText.remove(renameInputText.length() - 1);
        drawFileRenameInput();
    }
    
    bool typed = false;
    for (char c : status.word) {
        if (c >= 32 && c <= 126 && renameInputText.length() < 24) {
            // Filter out backquote from being typed
            if (c != '`') {
                renameInputText += c;
                typed = true;
            }
        }
    }
    
    if (typed) {
        drawFileRenameInput();
    }
}

void drawFileLoading() {
    String statusText = isSDCardManager ? "READING SD CARD SCHEMA..." : "READING FLASH SCHEMA...";
    drawProgressBar(loadingProgress, statusText, CP_CYAN);
    
    loadingProgress += 4;
    if (loadingProgress >= 100) {
        loadingProgress = 100;
        populateFileList();
        fileManagerSelected = 0;
        fileManagerScrollOffset = 0;
        appState = STATE_FILE_MANAGER;
        drawFileManager();
    } else {
        delay(30);
    }
}






void drawChippedButton(int x, int y, int w, int h, uint16_t color) {
    int chip = (h > 25) ? 8 : 5;
    if (w <= chip) return;
    canvas.drawLine(x, y, x + w, y, color);
    canvas.drawLine(x, y, x, y + h, color);
    canvas.drawLine(x, y + h, x + w - chip, y + h, color);
    canvas.drawLine(x + w, y, x + w, y + h - chip, color);
    canvas.drawLine(x + w, y + h - chip, x + w - chip, y + h, color);
}

void drawSplash() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    drawGlitchText("Breach_Protocol", 120, 5, 2, CP_CYAN, true, true);
    
    int maxLogs = 7;
    int y = 35;
    canvas.setTextColor(CP_ACTIVE_LINE);
    canvas.setTextSize(1);
    for (int i = 0; i < maxLogs; i++) {
        int logIdx = (logOffset + i) % dummyLogs.size();
        canvas.setCursor(5, y);
        canvas.print(dummyLogs[logIdx]);
        y += 11;
    }

    canvas.setTextSize(1);
    canvas.setCursor(10, 24);
    if (WiFi.status() == WL_CONNECTED) {
        canvas.setTextColor(CP_YELLOW);
        canvas.print("WIFI: CONNECTED");
    } else {
        canvas.setTextColor(CP_DIM);
        canvas.print("WIFI: CONNECTING");
    }
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(116, 24);
    canvas.print("VERSION: v7.0reborn");
    
    canvas.setTextSize(1);
    canvas.setTextColor(WHITE);
    
    if (showSplashBootMenu) {
        canvas.fillRect(15, 33, 210, 94, canvas.color565(15, 15, 15));
        canvas.drawRect(15, 33, 210, 94, CP_CYAN);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("--- SELECT BOOT NODE ---", 120, 38);
        
        std::vector<String> options = {"HARDWARE NODE", "NETWORK NODE", "OFFLINE PLAY", "SYSTEM SETTINGS"};
        for (int i = 0; i < 4; i++) {
            bool isSelected = (i == splashBootFocus);
            canvas.setTextColor(isSelected ? CP_CYAN : CP_DIM);
            if (isSelected) {
                canvas.drawCenterString("> [ " + options[i] + " ] <", 120, 56 + i * 15);
            } else {
                canvas.drawCenterString(options[i], 120, 56 + i * 15);
            }
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("UP/DN: MOVE | ENTER: SELECT | ESC: BACK", 120, 115);
    } else {
        canvas.drawString("> Press ", 10, 115);
        int x1 = 10 + canvas.textWidth("> Press ");
        drawGlitchText("ENTER", x1, 115, 1, CP_YELLOW, false, true);
        int x2 = x1 + canvas.textWidth("ENTER");
        canvas.setTextColor(WHITE);
        canvas.drawString(" to Select Boot Target", x2, 115);
    }
    
    pushCanvas();
}

void handleSplashInput(Keyboard_Class::KeysState status) {
    if (!showSplashBootMenu) {
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            showSplashBootMenu = true;
            splashBootFocus = 0;
            drawSplash();
        }
        return;
    }
    
    bool hasUp = false, hasDown = false;
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '`') hasEsc = true;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        splashBootFocus = (splashBootFocus - 1 + 4) % 4;
        drawSplash();
    } else if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        splashBootFocus = (splashBootFocus + 1) % 4;
        drawSplash();
    } else if (hasEsc) {
        playSound(sound_select, sound_select_size);
        showSplashBootMenu = false;
        drawSplash();
    } else if (status.enter) {
        playSound(sound_select, sound_select_size);
        showSplashBootMenu = false;
        
        if (splashBootFocus == 0) {
            appState = STATE_HARDWARE_MENU;
            hardwareMenuFocus = 0;
            currentHardwareScroll = 0;
            targetHardwareScroll = 0;
            showHardwareDesc = false;
            hardwareDescAnimWidth = 0.0;
            drawHardwareMenu();
        } else if (splashBootFocus == 1) {
            if (WiFi.status() == WL_CONNECTED) {
                appState = STATE_AUTH_MENU;
                drawAuthMenu();
            } else {
                startWifiScan();
            }
        } else if (splashBootFocus == 2) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            isGuest = true;
            authUser = "GUEST";
            enterMainMenu();
        } else if (splashBootFocus == 3) {
            appState = STATE_HARDWARE_SETTINGS;
            settingsFocus = 0;
            drawHardwareSettings();
        }
    }
}

void startWifiScan() {
    if (savedSSID != "" && savedWifiPass != "") {
        WiFi.begin(savedSSID.c_str(), savedWifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40) {
            int progress = (attempts * 100) / 40;
            drawProgressBar(progress, "CONNECTING TO LINK...", CP_CYAN);
            delay(100);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            for (int p = ((attempts * 100) / 40); p <= 100; p += 10) {
                drawProgressBar(p, "LINK ONLINE!", CP_GREEN);
                delay(20);
            }
            drawProgressBar(100, "LINK ONLINE!", CP_GREEN);
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            delay(1000);
            enterMainMenu();
            return;
        }
    }

    drawMessage("SCANNING WIFI...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    wifiList.clear();
    wifiList.push_back("[PLAY OFFLINE]");
    for (int i = 0; i < n && i < 9; ++i) {
        wifiList.push_back(WiFi.SSID(i));
    }
    appState = STATE_WIFI_SCAN;
    wifiSelection = 0;
    drawWifiScan();
}

void submitScore(int scoreToSubmit) {
    String key = isGuest ? ("g_hs_" + String(selectedGridSize)) : (authUser + "_hs_" + String(selectedGridSize));
    int currentHs = prefs.getInt(key.c_str(), 0);
    if (scoreToSubmit > currentHs) {
        prefs.putInt(key.c_str(), scoreToSubmit);
    }
    
    if (WiFi.status() == WL_CONNECTED && !isGuest) {
        if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
        HTTPClient http;
        String url = String(API_URL) + "/leaderboard";
        if (url.startsWith("https")) http.begin(secureClient, url);
        else http.begin(url);
        
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"username\":\"" + authUser + "\",\"score\":" + String(scoreToSubmit) + ",\"grid\":" + String(selectedGridSize) + ",\"phase\":" + String(currentPhase) + "}";
        http.POST(payload);
        http.end();
    }
}

void fetchLeaderboard(int offset, int limit) {
    if (offset == 0) {
        globalLeaderboard.clear();
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
        HTTPClient http;
        String url = String(API_URL) + "/leaderboard?offset=" + String(offset) + "&limit=" + String(limit);
        if (url.startsWith("https")) http.begin(secureClient, url);
        else http.begin(url);
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                totalLeaderboardSize = doc["total"].as<int>();
                JsonArray array = doc["data"].as<JsonArray>();
                for (JsonVariant v : array) {
                    LeaderboardEntry entry;
                    entry.username = v["username"].as<String>();
                    entry.score = v["score"].as<int>();
                    entry.grid = v["grid"].as<int>();
                    entry.phase = v["phase"].as<int>();
                    entry.date = v["date"].as<String>();
                    globalLeaderboard.push_back(entry);
                }
            }
        }
        http.end();
    }
}

void drawAuthMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(10, 10);
    canvas.print("ACCOUNT NAME:");
    
    uint16_t colorUser = (authFocus == 0) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 25, 220, 20, colorUser);
    canvas.setTextColor(colorUser);
    canvas.setCursor(15, 30);
    canvas.print(authUser + ((authFocus == 0 && blinkState) ? "_" : ""));

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(10, 55);
    canvas.print("PASSWORD:");

    uint16_t colorPass = (authFocus == 1) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 70, 220, 20, colorPass);
    canvas.setTextColor(colorPass);
    canvas.setCursor(15, 75);
    String starPass = "";
    for(int i=0; i<authPass.length(); i++) starPass += "*";
    canvas.print(starPass + ((authFocus == 1 && blinkState) ? "_" : ""));

    uint16_t colorRem = (authFocus == 2) ? CP_YELLOW : WHITE;
    canvas.setTextColor(colorRem);
    canvas.setCursor(10, 95);
    canvas.print(rememberMe ? "[X] REMEMBER ME" : "[ ] REMEMBER ME");

    uint16_t colorBtn1 = (authFocus == 3) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 110, 100, 20, colorBtn1);
    canvas.setTextColor(colorBtn1);
    drawGlitchText("LOGIN", 60, 115, 1, colorBtn1);
    
    uint16_t colorBtn2 = (authFocus == 4) ? CP_YELLOW : WHITE;
    drawChippedButton(130, 110, 100, 20, colorBtn2);
    canvas.setTextColor(colorBtn2);
    drawGlitchText("GUEST", 180, 115, 1, colorBtn2);
    pushCanvas();
}

void handleAuthInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (authFocus == 2) {
            rememberMe = !rememberMe;
            drawAuthMenu();
            return;
        } else if (authFocus == 3) {
            if (authUser == "") return;
            
            for (int p = 0; p <= 75; p += 5) {
                drawProgressBar(p, "DECRYPTING LINK SYSTEM...", CP_CYAN);
                delay(20);
            }
            
            if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
            HTTPClient http;
            String url = String(API_URL) + "/auth";
            if (url.startsWith("https")) http.begin(secureClient, url);
            else http.begin(url);
            
            http.addHeader("Content-Type", "application/json");
            String payload = "{\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\"}";
            int httpCode = http.POST(payload);
            
            if (httpCode == 200) {
                String response = http.getString();
                JsonDocument doc;
                deserializeJson(doc, response);
                String action = doc["action"].as<String>();
                String msg = (action == "signup") ? "OPERATIVE REGISTERED!" : "LOGIN SUCCESSFUL!";
                
                for (int p = 75; p <= 100; p += 5) {
                    drawProgressBar(p, msg, CP_GREEN);
                    delay(25);
                }
                drawProgressBar(100, msg, CP_GREEN);
                playSound(wifi_finished_wav, wifi_finished_wav_len);
                delay(1000);
                http.end();
                
                if (rememberMe) {
                    prefs.putString("user", authUser);
                    prefs.putString("pass", authPass);
                } else {
                    prefs.putString("user", "");
                    prefs.putString("pass", "");
                }
                
                isGuest = false;
                enterMainMenu();
            } else {
                http.end();
                playSound(sound_fail, sound_fail_size);
                drawProgressBar(100, "ACCESS DENIED", CP_RED);
                delay(2000);
                drawAuthMenu();
            }
            return;
        } else if (authFocus == 4) {
            isGuest = true;
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            enterMainMenu();
            return;
        }
        authFocus++;
        if (authFocus > 4) authFocus = 0;
        return;
    }
    
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c >= 32 && c <= 126 && c != ';' && c != '.' && c != ',' && c != '/') {
            if (authFocus == 0 && authUser.length() < 16) authUser += c;
            if (authFocus == 1 && authPass.length() < 16) authPass += c;
        }
    }
    
    if (hasUp) { 
        playSound(sound_hover, sound_hover_size); 
        if (authFocus == 3 || authFocus == 4) authFocus = 2;
        else if (authFocus == 2) authFocus = 1;
        else if (authFocus == 1) authFocus = 0;
        else if (authFocus == 0) authFocus = 3; 
    }
    else if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        if (authFocus == 0) authFocus = 1;
        else if (authFocus == 1) authFocus = 2;
        else if (authFocus == 2) authFocus = 3;
        else if (authFocus == 3 || authFocus == 4) authFocus = 0;
    }
    else if (hasLeft || hasRight) {
        if (authFocus == 3) { authFocus = 4; playSound(sound_hover, sound_hover_size); }
        else if (authFocus == 4) { authFocus = 3; playSound(sound_hover, sound_hover_size); }
    }
    
    if (status.del) {
        if (authFocus == 0 && authUser.length() > 0) authUser.remove(authUser.length()-1);
        if (authFocus == 1 && authPass.length() > 0) authPass.remove(authPass.length()-1);
    }
}

void drawWifiScan() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawString("--- WIFI TRANSCEIVER MODULE SCAN ---", 10, 5);
    canvas.drawString("SELECT NODE FOR BREACH INTRUSION  ", 10, 15);
    canvas.drawString("-----------------------------------", 10, 25);
    
    for (int i = 0; i < wifiList.size(); i++) {
        int y = 35 + i * 11;
        if (i == wifiSelection) {
            canvas.fillRect(5, y - 1, 230, 10, CP_CYAN);
            canvas.setTextColor(BLACK, CP_CYAN);
        } else {
            canvas.setTextColor(CP_CYAN, CP_BG);
        }
        canvas.setCursor(10, y);
        canvas.print("> " + wifiList[i]);
    }
    pushCanvas();
}

void handleWifiScanInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp && wifiSelection > 0) {
        wifiSelection--;
        playSound(sound_hover, sound_hover_size);
    }
    if (hasDown && wifiSelection < wifiList.size() - 1) {
        wifiSelection++;
        playSound(sound_hover, sound_hover_size);
    }
    
    if (status.enter && wifiList.size() > 0) {
        playSound(sound_select, sound_select_size);
        if (wifiList[wifiSelection] == "[PLAY OFFLINE]") {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            isGuest = true;
            authUser = "GUEST";
            enterMainMenu();
            return;
        }
        appState = STATE_WIFI_PASS;
        if (wifiList[wifiSelection] == savedSSID) {
            wifiPass = savedWifiPass;
        } else {
            wifiPass = "";
        }
        drawWifiPass();
    }
}

void drawWifiPass() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(10, 10);
    canvas.print("NETWORK: " + wifiList[wifiSelection]);
    
    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(10, 40);
    canvas.print("ENTER PASSWORD:");
    
    drawChippedButton(10, 55, 220, 20, WHITE);
    canvas.setTextColor(WHITE);
    canvas.setCursor(15, 60);
    canvas.print(wifiPass + (blinkState ? "_" : ""));
    pushCanvas();
}

void handleWifiPassInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        WiFi.begin(wifiList[wifiSelection].c_str(), wifiPass.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 50) {
            int progress = (attempts * 100) / 50;
            drawProgressBar(progress, "CONNECTING TO LINK...", CP_CYAN);
            delay(100);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            for (int p = ((attempts * 100) / 50); p <= 100; p += 10) {
                drawProgressBar(p, "LINK ONLINE!", CP_GREEN);
                delay(20);
            }
            drawProgressBar(100, "LINK ONLINE!", CP_GREEN);
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            delay(1000);
            
            prefs.putString("wifi_ssid", wifiList[wifiSelection]);
            prefs.putString("wifi_pass", wifiPass);
            savedSSID = wifiList[wifiSelection];
            savedWifiPass = wifiPass;
            appState = STATE_AUTH_MENU;
            drawAuthMenu();
        } else {
            playSound(sound_fail, sound_fail_size);
            drawProgressBar(100, "LINK ERROR: OFFLINE", CP_RED);
            delay(2000);
            appState = STATE_WIFI_SCAN;
            drawWifiScan();
        }
        return;
    }
    
    if (status.del && wifiPass.length() > 0) wifiPass.remove(wifiPass.length()-1);
    
    for (char c : status.word) {
        if (c >= 32 && c <= 126 && wifiPass.length() < 32) wifiPass += c;
    }
}

void enterMainMenu() {
    appState = STATE_MAIN_MENU;
    mainMenuFocus = 0;
    currentMenuScroll = 0;
    targetMenuScroll = 0;
    showMenuDesc = false;
    descAnimWidth = 0.0;
    showBootMenu = false;
    drawMainMenu();
}

void drawMainMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw headers centered on the right side of the screen to avoid the scroll wheel
    drawGlitchText("NETWORK NODE", 135, 12, 2, CP_CYAN, true, true);
    drawGlitchText("OPERATIVE: " + (isGuest ? String("GUEST") : authUser), 135, 34, 1, CP_DIM);
    
    // Draw rotating wheel arc on the left
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    int totalItems = isGuest ? 4 : 6;
    std::vector<String> labels;
    if (isGuest) {
        labels = {"HACK", "CONTROLS", "CREDITS", "BACK"};
    } else {
        labels = {"HACK", "LEADERBOARD", "ACCOUNT", "CONTROLS", "CREDITS", "BACK"};
    }
    
    for (int i = 0; i < totalItems; i++) {
        // Calculate shortest wrapping distance for seamless infinite scroll
        float rawOffset = i - currentMenuScroll;
        float offset = fmod(rawOffset, (float)totalItems);
        float halfItems = (float)totalItems / 2.0;
        if (offset > halfItems) offset -= (float)totalItems;
        if (offset < -halfItems) offset += (float)totalItems;
        
        // Don't draw items too far off screen
        if (abs(offset) > 1.5) continue;
        
        if (offset < -0.5) continue;
        
        // Calculate tick position on the arc
        float angle = offset * 0.391; // ~22.4 degrees in radians
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == mainMenuFocus);
        
        uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;
        
        // Draw outward-pointing rotated ticks
        float tickEndX = -80 + cos(angle) * (isSelected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (isSelected ? 117 : 115);
        
        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
        if (isSelected) {
            canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);
        }
        
        // Button dynamic properties
        float scale = 1.0 - abs(offset) * 0.3333;
        if (scale < 0.1) scale = 0.1;
        float h = 30.0 * scale;
        float y = tickY - h / 2.0;
        float w = 195.0 * scale;
        float x = tickX + 10;
        
        int textSize = isSelected ? 2 : 1;
        uint16_t color = isSelected ? CP_YELLOW : CP_DIM;
        if (i == totalItems - 1 && lastBreachFailed) {
            color = CP_RED;
        }
        
        drawChippedButton(x, y, w, h, color);
        
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
    }
    
    if (descAnimWidth >= 10.0) {
        int x = 40;
        int y = 52; // selected button y (67 - 15)
        int h = 30;
        
        // Fill background to cover the original button
        canvas.fillRect(x, y, (int)descAnimWidth, h, CP_BG);
        
        // Draw the chipped button outline
        drawChippedButton(x, y, (int)descAnimWidth, h, CP_YELLOW);
        
        if (descAnimWidth > 160.0) {
            canvas.setTextColor(CP_YELLOW);
            String label = labels[mainMenuFocus];
            String line1 = "";
            String line2 = "";
            
            if (label == "HACK") {
                line1 = "Access subnet";
                line2 = "gateways";
            } else if (label == "LEADERBOARD") {
                line1 = "View global";
                line2 = "scores";
            } else if (label == "ACCOUNT") {
                line1 = "Operative";
                line2 = "profile";
            } else if (label == "CONTROLS") {
                line1 = "Keyboard";
                line2 = "bindings";
            } else if (label == "CREDITS") {
                line1 = "System";
                line2 = "developers";
            }
            
            if (line1 != "") {
                canvas.setTextSize(2);
                canvas.setCursor(x + 10, y + 0);
                canvas.print(line1);
                canvas.setCursor(x + 10, y + 14);
                canvas.print(line2);
            }
        }
    }
    
    pushCanvas();
}
 
void handleMainMenuInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    bool hasRight = false;
    bool hasLeft = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '/') hasRight = true;
        if (c == ',') hasLeft = true;
    }
    
    if (showMenuDesc) {
        if (hasLeft || hasUp || hasDown) {
            playSound(sound_select, sound_select_size);
            showMenuDesc = false;
            return;
        }
    } else {
        int limit = isGuest ? 3 : 5;
        if (hasRight && mainMenuFocus < limit) {
            playSound(sound_select, sound_select_size);
            showMenuDesc = true;
            return;
        }
    }
 
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        showMenuDesc = false;
        descAnimWidth = 0.0;
        
        std::vector<String> labels;
        if (isGuest) {
            labels = {"HACK", "CONTROLS", "CREDITS", "BACK"};
        } else {
            labels = {"HACK", "LEADERBOARD", "ACCOUNT", "CONTROLS", "CREDITS", "BACK"};
        }
        
        String selectedLabel = labels[mainMenuFocus];
        if (selectedLabel == "HACK") {
            appState = STATE_GRID_SELECT;
            gridMenuFocus = 0;
            currentGridScroll = 0;
            targetGridScroll = 0;
            drawGridSelect();
        } else if (selectedLabel == "LEADERBOARD") {
            appState = STATE_LEADERBOARD;
            drawMessage("FETCHING DATABANK...");
            fetchLeaderboard(0, 10);
            leaderboardCursor = 0;
            leaderboardScrollOffset = 0;
            drawLeaderboard();
        } else if (selectedLabel == "ACCOUNT") {
            appState = STATE_ACCOUNT;
            accountFocus = 0;
            accountStatsFetched = false;
        } else if (selectedLabel == "CONTROLS") {
            appState = STATE_CONTROLS;
            drawControlsScreen();
        } else if (selectedLabel == "CREDITS") {
            appState = STATE_CREDITS;
            drawCreditsScreen();
        } else if (selectedLabel == "BACK") {
            canvas.fillScreen(CP_BG);
            canvas.setTextColor(CP_RED);
            canvas.setTextSize(2);
            canvas.drawCenterString("REBOOTING...", 120, 50);
            pushCanvas();
            delay(500);
            ESP.restart();
        }
        return;
    }
    
    if (!showMenuDesc) {
        int maxFocus = isGuest ? 3 : 5;
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            mainMenuFocus--;
            if (mainMenuFocus < 0) mainMenuFocus = maxFocus;
            targetMenuScroll -= 1.0;
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            mainMenuFocus++;
            if (mainMenuFocus > maxFocus) mainMenuFocus = 0;
            targetMenuScroll += 1.0;
        }
    }
}

void drawRotatedText(String text, int cx, int cy, uint16_t color) {
    int w = text.length() * 6;
    int h = 8;
    M5Canvas tSpr(&canvas);
    tSpr.createSprite(w, h);
    tSpr.fillSprite(TFT_BLACK); 
    tSpr.setTextColor(color);
    tSpr.setTextSize(1);
    tSpr.drawString(text, 0, 0);
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = tSpr.readPixel(x, y);
            if (p != TFT_BLACK) {
                int dx = x - w/2;
                int dy = y - h/2;
                int nx = cx - dy;
                int ny = cy + dx;
                canvas.drawPixel(nx, ny, p);
            }
        }
    }
    tSpr.deleteSprite();
}

void drawGridSelect() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    String scoreStr = "HI_SCORE";
    if (gridMenuFocus < 3) {
        int grid = (gridMenuFocus == 0) ? 3 : ((gridMenuFocus == 1) ? 4 : 5);
        String key = isGuest ? ("g_hs_" + String(grid)) : (authUser + "_hs_" + String(grid));
        int hs = prefs.getInt(key.c_str(), 0);
        if (hs > 0) scoreStr = String(hs);
    }
    drawRotatedText(scoreStr, 10, 67, CP_YELLOW);
    
    // Draw rotating wheel arc on the left
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    String labels[4] = {"3x3", "4x4", "5x5", "BACK"};
    String descs[4] = {"Base Points Payout", "Higher Points Payout", "Maximum Points Payout", "Return to Menu"};
    
    for (int i = 0; i < 4; i++) {
        // Calculate shortest wrapping distance for seamless infinite scroll
        float rawOffset = i - currentGridScroll;
        float offset = fmod(rawOffset, 4.0);
        if (offset > 2.0) offset -= 4.0;
        if (offset < -2.0) offset += 4.0;
        
        // Don't draw items too far off screen
        if (abs(offset) > 1.5) continue;
        
        // Calculate tick position on the arc
        float angle = offset * 0.391; // ~22.4 degrees in radians
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == gridMenuFocus);
        
        uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;
        
        // Draw outward-pointing rotated ticks instead of flat rectangles
        float tickEndX = -80 + cos(angle) * (isSelected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (isSelected ? 117 : 115);
        
        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
        if (isSelected) {
            canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);
        }
        
        // Button dynamic properties
        float scale = 1.0 - abs(offset) * 0.3333;
        if (scale < 0.1) scale = 0.1;
        float h = 30.0 * scale;
        float y = tickY - h / 2.0;
        float w = 195.0 * scale;
        float x = tickX + 10;
        
        int textSize = isSelected ? 2 : 1;
        uint16_t color = isSelected ? CP_YELLOW : CP_DIM;
        if (i == 3 && lastBreachFailed) {
            color = CP_RED;
        }
        
        drawChippedButton(x, y, w, h, color);
        
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
        
        if (isSelected) {
            int labelWidth = canvas.textWidth(labels[i]);
            canvas.setTextSize(1);
            canvas.setTextColor(WHITE);
            canvas.setCursor(textX + labelWidth + 6, textY + 4); 
            canvas.print(descs[i]);
        }
    }
    
    pushCanvas();
}

void handleGridSelectInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (gridMenuFocus == 3) {
            enterMainMenu();
            return;
        }
        
        if (gridMenuFocus == 0) selectedGridSize = 3;
        else if (gridMenuFocus == 1) selectedGridSize = 4;
        else selectedGridSize = 5;
        
        currentPhase = 1;
        accumulatedScore = 0;
        appState = STATE_PLAYING;
        initGame();
        drawScreen();
        return;
    }
    bool hasUp = false, hasDown = false;
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '`') hasEsc = true;
    }
    if (hasEsc) {
        playSound(sound_select, sound_select_size);
        enterMainMenu();
        return;
    }
    if (hasUp) { 
        playSound(sound_hover, sound_hover_size); 
        gridMenuFocus--; 
        if (gridMenuFocus < 0) gridMenuFocus = 3; 
        targetGridScroll -= 1.0;
    }
    if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        gridMenuFocus++; 
        if (gridMenuFocus > 3) gridMenuFocus = 0; 
        targetGridScroll += 1.0;
    }
}

void drawPhaseTransition() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("PHASE " + String(currentPhase) + " COMPLETE", 120, 15);
    
    canvas.setTextSize(1);
    canvas.setTextColor(CP_DIM);
    String formulaStr = "1000*" + String(selectedGridSize) + "*" + String(currentPhase) + "*(" + String(1.0 + lastTimeRatio, 4) + ")=" + String(lastPhaseScore);
    canvas.drawCenterString(formulaStr, 120, 35);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("ACCUMULATED SCORE: " + String(accumulatedScore), 120, 50);
    
    if (currentPhase < 8) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NEXT PHASE TIME: " + String(phaseTimes[currentPhase]/1000) + "s", 120, 70);
        
        uint16_t colCont = (phaseMenuFocus == 0) ? CP_YELLOW : WHITE;
        drawChippedButton(20, 95, 90, 20, colCont);
        canvas.setTextColor(colCont);
        drawGlitchText("CONTINUE", 65, 100, 1, colCont);
        
        uint16_t colSave = (phaseMenuFocus == 1) ? CP_YELLOW : WHITE;
        drawChippedButton(130, 95, 90, 20, colSave);
        canvas.setTextColor(colSave);
        drawGlitchText("SAVE & EXIT", 175, 100, 1, colSave);
    } else {
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("ALL PHASES COMPLETE!", 120, 70);
        uint16_t colSave = CP_YELLOW;
        drawChippedButton(75, 95, 90, 20, colSave);
        canvas.setTextColor(colSave);
        drawGlitchText("SAVE SCORE", 120, 100, 1, CP_YELLOW);
    }
    
    pushCanvas();
}

void handlePhaseTransitionInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (currentPhase >= 8 || phaseMenuFocus == 1) {
            drawMessage("SAVING SCORE...");
            submitScore(accumulatedScore);
            enterMainMenu();
        } else {
            currentPhase++;
            initGame();
            appState = STATE_PLAYING;
            drawScreen();
        }
        return;
    }
    if (currentPhase < 8) {
        bool hasLeft = false, hasRight = false;
        for (char c : status.word) {
            if (c == ',') hasLeft = true;
            if (c == '/') hasRight = true;
        }
        if (hasLeft || hasRight) {
            phaseMenuFocus = (phaseMenuFocus == 0) ? 1 : 0;
            playSound(sound_hover, sound_hover_size);
        }
    }
}

void drawGameOverFailed() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_RED);
    canvas.setTextSize(2);
    canvas.drawCenterString("SYSTEM LOCKED", 120, 30);
    canvas.setTextSize(1);
    canvas.drawCenterString("ALL ACCUMULATED SCORE LOST", 120, 60);
    uint16_t btnColor = blinkState ? CP_RED : CP_DIM;
    drawChippedButton(70, 95, 100, 20, btnColor);
    canvas.setTextColor(btnColor);
    canvas.setTextSize(1);
    drawGlitchText("PRESS ENTER", 120, 101, 1, btnColor);
    pushCanvas();
}

void drawLeaderboard() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("GLOBAL SCORES", 120, 10);
    
    canvas.setTextSize(1);
    
    if (globalLeaderboard.size() == 0) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("[ NO NETWORK DATA ]", 120, 60);
    } else {
        int y = 35;
        for (int i = leaderboardScrollOffset; i < min(leaderboardScrollOffset + 3, (int)globalLeaderboard.size()); i++) {
            if (i == leaderboardCursor) {
                canvas.fillRect(0, y - 2, 240, 24, canvas.color565(40, 40, 40));
            }
            canvas.setTextColor(CP_RED);
            canvas.setCursor(5, y);
            canvas.print("#" + String(i + 1));
            
            canvas.setTextColor(WHITE);
            canvas.setCursor(30, y);
            canvas.print(globalLeaderboard[i].username);
            
            canvas.setTextColor(CP_DIM);
            canvas.setCursor(135, y);
            canvas.print(globalLeaderboard[i].date);
            
            canvas.setTextColor(CP_CYAN);
            canvas.setCursor(30, y + 10);
            canvas.print(String(globalLeaderboard[i].grid) + "x" + String(globalLeaderboard[i].grid) + " Phase " + String(globalLeaderboard[i].phase));
            
            canvas.setTextColor(CP_YELLOW);
            canvas.setCursor(135, y + 10);
            canvas.print("SCORE: " + String(globalLeaderboard[i].score));
            
            y += 25;
        }
    }
    
    canvas.setTextColor(WHITE);
    canvas.drawCenterString("Press ENTER to return", 120, 115);
    pushCanvas();
}

void initGame(bool keepDiff) {
    // Generate hexCodes based on current network BSSID or hardware MAC address
    String mac = WiFi.BSSIDstr();
    if (mac == "" || mac == "00:00:00:00:00:00") {
        mac = WiFi.macAddress();
    }
    
    String octets[6];
    int oIdx = 0;
    for (int i = 0; i < mac.length(); i++) {
        if (mac[i] == ':') {
            oIdx++;
        } else {
            if (oIdx < 6) octets[oIdx] += mac[i];
        }
    }
    
    String unique[7];
    int uCount = 0;
    for (int i = 0; i < 6; i++) {
        String code = octets[i];
        code.toUpperCase();
        if (code.length() != 2) continue;
        
        bool dup = false;
        for (int j = 0; j < uCount; j++) {
            if (unique[j] == code) { dup = true; break; }
        }
        if (!dup && uCount < 7) {
            unique[uCount++] = code;
        }
    }
    
    String fallbacks[] = {"1C", "55", "BD", "E9", "FF", "7A", "42"};
    for (int i = 0; i < 7; i++) {
        if (uCount >= 7) break;
        String code = fallbacks[i];
        bool dup = false;
        for (int j = 0; j < uCount; j++) {
            if (unique[j] == code) { dup = true; break; }
        }
        if (!dup) {
            unique[uCount++] = code;
        }
    }
    
    for (int i = 0; i < 7; i++) {
        hexCodes[i] = unique[i];
    }

    lastBreachFailed = false;
    gridSize = selectedGridSize;
    maxTime = phaseTimes[currentPhase - 1];
    
    if (gridSize == 3) targetSize = 3;
    else targetSize = 4;

    for(int i=0; i<gridSize; i++) {
        for(int j=0; j<gridSize; j++) {
            matrix[i][j] = hexCodes[random(7)];
        }
    }
    
    int r = 0, c = random(gridSize);
    targetSeq[0] = matrix[r][c];
    
    bool visited[5][5] = {false};
    for(int i=0; i<5; i++) {
        for(int j=0; j<5; j++) visited[i][j] = false;
    }
    visited[r][c] = true;
    
    bool makeRow = false;
    for(int i=1; i<targetSize; i++) {
        int avail[5];
        int count = 0;
        if (makeRow) {
            for(int cc=0; cc<gridSize; cc++) if(!visited[r][cc]) avail[count++] = cc;
            if(count > 0) c = avail[random(count)];
        } else {
            for(int rr=0; rr<gridSize; rr++) if(!visited[rr][c]) avail[count++] = rr;
            if(count > 0) r = avail[random(count)];
        }
        visited[r][c] = true;
        targetSeq[i] = matrix[r][c];
        makeRow = !makeRow;
    }
    
    bufferIndex = 0;
    for(int i=0; i<gridSize; i++) buffer[i] = "";
    
    isRowActive = true;
    activeRow = 0;
    cursorIdx = 0;
    
    timeLeft = maxTime;
    gameOver = false;
    hackSuccess = false;
    lastUpdate = millis();
    isAnimating = false;
    blinkState = false;
    lastBlink = millis();
}

void initSPIFFS() {
    if (SPIFFS.begin(true)) {
        if (!SPIFFS.exists("/deck_config.json")) {
            File f = SPIFFS.open("/deck_config.json", FILE_WRITE);
            if (f) {
                f.println("{");
                f.println("  \"deck_id\": \"CYBER_D_01\",");
                f.println("  \"os\": \"cyber_os_7.0\",");
                f.println("  \"icebreaker\": \"v4.2\"");
                f.println("}");
                f.close();
            }
        }
        if (!SPIFFS.exists("/breach_log.txt")) {
            File f = SPIFFS.open("/breach_log.txt", FILE_WRITE);
            if (f) {
                f.println("BREACH ACCESS LOG:");
                f.println("OP: sl01220");
                f.println("STATUS: COMPLETED");
                f.close();
            }
        }
        if (!SPIFFS.exists("/system.ini")) {
            File f = SPIFFS.open("/system.ini", FILE_WRITE);
            if (f) {
                f.println("[system]");
                f.println("node=BP_X1");
                f.println("security=HIGH");
                f.close();
            }
        }
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    initSPIFFS();
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(240, 135);
    
    prefs.begin("breach", false);
    highScore = prefs.getInt("highscore", 0);
    savedSSID = prefs.getString("wifi_ssid", "");
    savedWifiPass = prefs.getString("wifi_pass", "");
    if (prefs.isKey("insane")) {
        insaneMode = prefs.getInt("insane", -1);
        if (insaneMode == -1) {
            insaneMode = prefs.getBool("insane", false) ? 2 : 1;
            prefs.remove("insane");
            prefs.putInt("insane", insaneMode);
        }
    } else {
        insaneMode = 1;
    }
    authUser = prefs.getString("user", "");
    authPass = prefs.getString("pass", "");
    globalVolume = prefs.getInt("volume", 80);
    if (globalVolume > 100) globalVolume = (globalVolume * 100) / 255;
    globalVolume = ((globalVolume + 2) / 5) * 5;
    if (globalVolume > 100) globalVolume = 100;
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    
    globalBrightness = prefs.getInt("brightness", 80);
    if (globalBrightness > 100) globalBrightness = (globalBrightness * 100) / 255;
    globalBrightness = ((globalBrightness + 2) / 5) * 5;
    if (globalBrightness > 100) globalBrightness = 100;
    if (globalBrightness < 5) globalBrightness = 5;
    M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
    
    if (authUser != "") rememberMe = true;
    
    if (savedSSID != "") {
        WiFi.begin(savedSSID.c_str(), savedWifiPass.c_str());
    }
    
    appState = STATE_SPLASH;
    drawSplash();
    playSound(intro_wav, intro_wav_len);
}

void drawTimer(bool forceRedraw = false) {
    static int lastBarWidth = -1;
    static int lastTimeLeft = -1;
    if (forceRedraw) { lastBarWidth = -1; lastTimeLeft = -1; }
    
    int barWidth = map(timeLeft, 0, maxTime, 0, 80);
    if (barWidth < 0) barWidth = 0;
    
    if (barWidth == lastBarWidth && timeLeft == lastTimeLeft) return;
    
    canvas.startWrite();
    
    if (timeLeft != lastTimeLeft) {
        int secs = timeLeft / 1000;
        int centis = (timeLeft % 1000) / 10;
        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", secs, centis);
        canvas.setTextColor(CP_YELLOW, CP_PANEL);
        canvas.setTextSize(1);
        canvas.setCursor(95, 5);
        canvas.print(timeStr);
        // Clear any ghosting after the timer string
        canvas.print("  ");
    }

    if (barWidth != lastBarWidth) {
        if (lastBarWidth == -1 || barWidth > lastBarWidth) {
            canvas.fillRect(146, 6, barWidth, 6, CP_YELLOW);
            canvas.fillRect(146 + barWidth, 6, 80 - barWidth, 6, CP_PANEL);
        } else {
            canvas.fillRect(146 + barWidth, 6, lastBarWidth - barWidth, 6, CP_PANEL);
        }
    }
    pushCanvas();
    
    lastBarWidth = barWidth;
    lastTimeLeft = timeLeft;
}

void drawScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.fillRect(0, 0, 240, 18, CP_PANEL);
    canvas.setTextColor(CP_YELLOW, CP_PANEL);
    canvas.setTextSize(1);
    canvas.setCursor(5, 5);
    canvas.print("PHASE: " + String(currentPhase));
    
    canvas.drawRect(144, 4, 84, 10, CP_YELLOW);
    drawTimer(true);

    // TARGET
    canvas.setTextColor(WHITE, CP_BG);
    canvas.setCursor(120, 25);
    canvas.print("SEQUENCE REQUIRED");
    
    int matchLen = 0;
    for (int len = min(bufferIndex, targetSize); len > 0; len--) {
        bool match = true;
        for (int i = 0; i < len; i++) {
            if (buffer[bufferIndex - len + i] != targetSeq[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            matchLen = len;
            break;
        }
    }
    
    int boxW = min(22, (120 / gridSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;

    for(int i=0; i<targetSize; i++) {
        int tx = 120 + i*boxStep;
        int ty = 38;
        
        if (i < matchLen) {
            canvas.drawRect(tx, ty, boxW, 18, CP_CYAN);
            canvas.setTextColor(CP_CYAN, CP_BG);
        } else {
            canvas.drawRect(tx, ty, boxW, 18, CP_DIM);
            canvas.setTextColor(WHITE, CP_BG);
        }
        canvas.setCursor(tx + txtOff, ty + 5);
        canvas.print(targetSeq[i]);
    }
    
    // BUFFER
    canvas.setTextColor(CP_YELLOW, CP_BG);
    canvas.setCursor(120, 70);
    canvas.print("BUFFER");
    
    for(int i=0; i<gridSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        
        if (i < bufferIndex) {
            canvas.fillRect(bx, by, boxW, 18, CP_CYAN);
            canvas.setTextColor(BLACK, CP_CYAN);
            canvas.setCursor(bx + txtOff, by + 5);
            canvas.print(buffer[i]);
        } else if (i == bufferIndex) {
            canvas.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
        } else {
            canvas.drawRect(bx, by, boxW, 18, CP_DIM);
        }
    }
    
    if (gameOver && !isAnimating) {
        uint16_t color = hackSuccess ? canvas.color565(66, 245, 84) : CP_RED;
        canvas.setTextSize(2);
        canvas.setTextColor(color, CP_BG);
        canvas.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", 170, 106);
        canvas.setTextSize(1);
        canvas.setTextColor(WHITE, CP_BG);
        canvas.drawCenterString("Press ENTER", 170, 125);
        canvas.fillRect(120, 102, 100, 2, color);
    }
    
    // MATRIX
    int startX = 5 + (5 - gridSize) * 11;
    int startY = 30 + (5 - gridSize) * 10;
    
    if (isRowActive) {
        canvas.fillRect(startX, startY + activeRow*20, gridSize*22, 18, CP_ACTIVE_LINE);
    } else {
        canvas.fillRect(startX + activeCol*22, startY, 20, gridSize*20, CP_ACTIVE_LINE);
    }
    
    for(int i=0; i<gridSize; i++) {
        for(int j=0; j<gridSize; j++) {
            int cx = startX + j*22;
            int cy = startY + i*20;
            
            bool isActiveLine = (isRowActive && i == activeRow) || (!isRowActive && j == activeCol);
            uint16_t bgColor = isActiveLine ? CP_ACTIVE_LINE : CP_BG;
            
            if (matrix[i][j] == "") {
                canvas.setTextColor(CP_DIM, bgColor);
                canvas.setCursor(cx + 4, cy + 5);
                canvas.print("[]");
                continue;
            }
            
            bool isHovered = (isRowActive && i == activeRow && j == cursorIdx) || 
                             (!isRowActive && j == activeCol && i == cursorIdx);
            
            if (isHovered) {
                canvas.fillRect(cx, cy, 20, 18, CP_CYAN);
                canvas.setTextColor(BLACK, CP_CYAN);
            } else if (isActiveLine) {
                canvas.setTextColor(CP_YELLOW, CP_ACTIVE_LINE);
            } else {
                canvas.setTextColor(WHITE, CP_BG);
            }
            canvas.setCursor(cx + 4, cy + 5);
            canvas.print(matrix[i][j]);
        }
    }
    pushCanvas();
}

void updateAnimation() {
    unsigned long t = millis() - animStartTime;
    canvas.startWrite();
    uint16_t color = hackSuccess ? canvas.color565(66, 245, 84) : CP_RED;
    
    int boxW = min(22, (120 / gridSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;
    
    int bufferCenter = 120 + (gridSize * boxStep) / 2;

    for(int i=0; i<gridSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        
        float distFromCenter = abs(i - (gridSize - 1.0) / 2.0);
        long delayStart = distFromCenter * 150;
        
        if (t > delayStart) {
            int fillHeight = map(t - delayStart, 0, 300, 0, 18);
            if (fillHeight > 18) fillHeight = 18;
            canvas.fillRect(bx, by + 18 - fillHeight, boxW, fillHeight, color);
            if (i < bufferIndex) {
                canvas.setTextColor(BLACK);
                canvas.setCursor(bx + txtOff, by + 5);
                canvas.print(buffer[i]);
            }
        }
    }
    
    int barWidth = map(t, 0, 600, 0, 100);
    if (barWidth > 100) barWidth = 100;
    canvas.fillRect(bufferCenter - barWidth/2, 105, barWidth, 2, color);
    
    if (t > 600 && t < 650) {
        canvas.setTextSize(2);
        canvas.setTextColor(color, CP_BG);
        canvas.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", bufferCenter, 110);
        canvas.setTextSize(1);
        canvas.setTextColor(WHITE, CP_BG);
        canvas.drawCenterString("Press ENTER", bufferCenter, 125);
    }
    pushCanvas();
}

void drawAccountMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("OPERATIVE PROFILE", 120, 10);
    
    if (!accountStatsFetched) {
        canvas.setTextSize(1);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("FETCHING DATA...", 120, 40);
        pushCanvas();
        if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
        HTTPClient http;
        String url = String(API_URL) + "/account";
        if (url.startsWith("https")) http.begin(secureClient, url);
        else http.begin(url);
        
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"action\":\"get_stats\",\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\"}";
        int httpCode = http.POST(payload);
        if (httpCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getString());
            accountHighScore = doc["highScore"].as<int>();
            accountRank = doc["rank"].as<int>();
            accountHighGrid = doc["grid"].as<int>();
            accountHighPhase = doc["phase"].as<int>();
        }
        http.end();
        accountStatsFetched = true;
        newAccountName = authUser;
        newAccountPass = authPass;
        
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        canvas.setTextColor(CP_CYAN);
        canvas.setTextSize(2);
        canvas.drawCenterString("OPERATIVE PROFILE", 120, 10);
    }
    
    canvas.setTextSize(1);
    canvas.setTextColor(WHITE);
    canvas.setCursor(10, 35);
    canvas.print("NAME: ");
    canvas.setTextColor(CP_CYAN);
    canvas.print(authUser);
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(10, 50);
    canvas.print("HIGHSCORE: ");
    canvas.setTextColor(CP_YELLOW);
    if (accountRank > 0) {
        canvas.print(String(accountHighScore) + " (" + String(accountHighGrid) + "x" + String(accountHighGrid) + " P" + String(accountHighPhase) + ") R:#" + String(accountRank));
    } else {
        canvas.print("NO DATA (RANK: N/A)");
    }
    
    uint16_t c0 = (accountFocus == 0) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c0);
    drawGlitchText("> NAME:   " + newAccountName + (accountFocus == 0 && blinkState ? "_" : ""), 10, 60, 1, c0, false);
    
    uint16_t c1 = (accountFocus == 1) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c1);
    String stars = "";
    for (int i=0; i<newAccountPass.length(); i++) stars += "*";
    drawGlitchText("> PASS:   " + stars + (accountFocus == 1 && blinkState ? "_" : ""), 10, 75, 1, c1, false);
    
    uint16_t c2 = (accountFocus == 2) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c2);
    String modeLabel = "";
    if (insaneMode == 0) modeLabel = "[OFF]";
    else if (insaneMode == 1) modeLabel = "[ON]";
    else modeLabel = "[INSANE]";
    drawGlitchText("> GLITCH TEXT: " + modeLabel, 10, 90, 1, c2, false);

    uint16_t c3 = (accountFocus == 3) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 110, 100, 20, c3);
    canvas.setTextColor(c3);
    drawGlitchText("UPDATE", 60, 115, 1, c3);
    
    uint16_t c4 = (accountFocus == 4) ? CP_YELLOW : WHITE;
    drawChippedButton(130, 110, 100, 20, c4);
    canvas.setTextColor(c4);
    drawGlitchText("BACK", 180, 115, 1, c4);
    
    pushCanvas();
}

void handleAccountInput(Keyboard_Class::KeysState status) {
    if (status.del) {
        if (accountFocus == 0 && newAccountName.length() > 0) newAccountName.remove(newAccountName.length()-1);
        if (accountFocus == 1 && newAccountPass.length() > 0) newAccountPass.remove(newAccountPass.length()-1);
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (accountFocus == 2) {
            insaneMode = (insaneMode + 1) % 3;
            prefs.putInt("insane", insaneMode);
            drawAccountMenu();
            return;
        } else if (accountFocus == 3) {
            if (newAccountName == authUser && newAccountPass == authPass) {
                accountFocus = 4;
                return;
            }
            if (newAccountName == "") return;
            drawMessage("UPDATING...");
            if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
            HTTPClient http;
            String url = String(API_URL) + "/account";
            if (url.startsWith("https")) http.begin(secureClient, url);
            else http.begin(url);
            
            http.addHeader("Content-Type", "application/json");
            String payload = "{\"action\":\"update_account\",\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\",\"newUsername\":\"" + newAccountName + "\",\"newPassword\":\"" + newAccountPass + "\"}";
            int httpCode = http.POST(payload);
            http.end();
            
            if (httpCode == 200) {
                authUser = newAccountName;
                authPass = newAccountPass;
                drawMessage("UPDATE SUCCESS!");
            } else {
                drawMessage("UPDATE FAILED!");
            }
            delay(1500);
            accountStatsFetched = false;
            enterMainMenu();
            return;
        } else if (accountFocus == 4) {
            accountStatsFetched = false;
            enterMainMenu();
            return;
        }
        accountFocus++;
        if (accountFocus > 4) accountFocus = 0;
        return;
    }
    
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c >= 32 && c <= 126 && c != ';' && c != '.' && c != ',' && c != '/') {
            if (accountFocus == 0 && newAccountName.length() < 16) newAccountName += c;
            if (accountFocus == 1 && newAccountPass.length() < 16) newAccountPass += c;
        }
    }
    
    if (hasUp) { 
        playSound(sound_hover, sound_hover_size); 
        if (accountFocus == 3 || accountFocus == 4) accountFocus = 2;
        else if (accountFocus == 2) accountFocus = 1;
        else if (accountFocus == 1) accountFocus = 0;
        else if (accountFocus == 0) accountFocus = 3; 
    }
    if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        if (accountFocus == 0) accountFocus = 1;
        else if (accountFocus == 1) accountFocus = 2;
        else if (accountFocus == 2) accountFocus = 3;
        else if (accountFocus == 3 || accountFocus == 4) accountFocus = 0;
    }
    if (hasLeft || hasRight) {
        if (accountFocus == 3) { accountFocus = 4; playSound(sound_hover, sound_hover_size); }
        else if (accountFocus == 4) { accountFocus = 3; playSound(sound_hover, sound_hover_size); }
    }
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!otaInit) {
            ArduinoOTA.setHostname("Cardputer-Breach");
            ArduinoOTA.begin();
            otaInit = true;
        }
        ArduinoOTA.handle();
    }

    M5Cardputer.update();
    unsigned long now = millis();
    
    bool keyChanged = M5Cardputer.Keyboard.isChange();
    bool keyPressed = M5Cardputer.Keyboard.isPressed();
    Keyboard_Class::KeysState globalStatus;
    if (keyChanged && keyPressed) {
        globalStatus = M5Cardputer.Keyboard.keysState();
    }
    
    if (keyChanged && keyPressed) {
        Keyboard_Class::KeysState status = globalStatus;
        bool volChanged = false;
        bool brtChanged = false;
        for (auto i : status.word) {
            if (i == '-' || i == '_') {
                globalVolume -= 5;
                if (globalVolume < 0) globalVolume = 0;
                volChanged = true;
            } else if (i == '=' || i == '+') {
                globalVolume += 5;
                if (globalVolume > 100) globalVolume = 100;
                volChanged = true;
            } else if (i == '[') {
                globalBrightness -= 5;
                if (globalBrightness < 5) globalBrightness = 5;
                brtChanged = true;
            } else if (i == ']') {
                globalBrightness += 5;
                if (globalBrightness > 100) globalBrightness = 100;
                brtChanged = true;
            }
        }
        if (volChanged) {
            M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
            prefs.putInt("volume", globalVolume);
            playSound(sound_hover, sound_hover_size);
            showVolumePopup = true;
            lastVolumeChangeTime = now;
            drawCurrentScreen();
        }
        if (brtChanged) {
            M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
            prefs.putInt("brightness", globalBrightness);
            playSound(sound_hover, sound_hover_size);
            showBrightnessPopup = true;
            lastBrightnessChangeTime = now;
            drawCurrentScreen();
        }
    }
    
    if (showVolumePopup) {
        unsigned long elapsed = now - lastVolumeChangeTime;
        if (elapsed > 1000) {
            if (elapsed >= 1300) {
                showVolumePopup = false;
                drawCurrentScreen();
            } else {
                drawCurrentScreen();
            }
        }
    }
    
    if (showBrightnessPopup) {
        unsigned long elapsed = now - lastBrightnessChangeTime;
        if (elapsed > 1000) {
            if (elapsed >= 1300) {
                showBrightnessPopup = false;
                drawCurrentScreen();
            } else {
                drawCurrentScreen();
            }
        }
    }
    
    if (appState == STATE_SPLASH) {
        if (now - lastLogUpdate > 200) {
            if (logOffset < dummyLogs.size() - 7) {
                logOffset++;
                drawSplash();
            }
            lastLogUpdate = now;
        }
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawSplash();
        }
        if (keyChanged && keyPressed) {
            handleSplashInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (now - lastBlink > 500) {
        blinkState = !blinkState;
        lastBlink = now;
        if (appState == STATE_AUTH_MENU) drawAuthMenu();
        if (appState == STATE_WIFI_PASS) drawWifiPass();
        if (appState == STATE_ACCOUNT) drawAccountMenu();
    }
    
    if (appState == STATE_AUTH_MENU) {
        if (keyChanged && keyPressed) {
            handleAuthInput(globalStatus);
            if (appState == STATE_AUTH_MENU) drawAuthMenu();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_WIFI_SCAN) {
        if (keyChanged && keyPressed) {
            handleWifiScanInput(globalStatus);
            if (appState == STATE_WIFI_SCAN) drawWifiScan();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_WIFI_PASS) {
        if (keyChanged && keyPressed) {
            handleWifiPassInput(globalStatus);
            if (appState == STATE_WIFI_PASS) drawWifiPass();
        }
        delay(10);
        return;
    }
    
    if (insaneMode == 2) {
        static unsigned long lastInsane = 0;
        static unsigned long nextInsane = 500;
        if (now - lastInsane > nextInsane) {
            if (appState == STATE_AUTH_MENU) drawAuthMenu();
            else if (appState == STATE_MAIN_MENU) drawMainMenu();
            else if (appState == STATE_ACCOUNT) drawAccountMenu();
            else if (appState == STATE_GRID_SELECT) drawGridSelect();
            else if (appState == STATE_PHASE_TRANSITION) drawPhaseTransition();
            else if (appState == STATE_FAILED_SCREEN) drawGameOverFailed();
            else if (appState == STATE_HARDWARE_MENU) drawHardwareMenu();

            lastInsane = now;
            nextInsane = random(50, 1200);
        }
    }
    
    if (appState == STATE_MAIN_MENU) {
        if (insaneMode == 1) {
            static unsigned long lastMenuGlitch = 0;
            static unsigned long nextMenuGlitch = 500;
            if (now - lastMenuGlitch > nextMenuGlitch) {
                drawMainMenu();
                lastMenuGlitch = now;
                nextMenuGlitch = random(50, 1200);
            }
        }
        
        if (keyChanged && keyPressed) {
            handleMainMenuInput(globalStatus);
            if (appState == STATE_MAIN_MENU) drawMainMenu();
        }
        
        bool needsRedraw = false;
        if (abs(currentMenuScroll - targetMenuScroll) > 0.01) {
            currentMenuScroll += (targetMenuScroll - currentMenuScroll) * 0.3; // Smooth lerp
            if (abs(currentMenuScroll - targetMenuScroll) <= 0.01) {
                currentMenuScroll = targetMenuScroll;
            }
            needsRedraw = true;
        }
        
        if (showMenuDesc && descAnimWidth < 195.0) {
            descAnimWidth += (195.0 - descAnimWidth) * 0.4;
            if (195.0 - descAnimWidth < 1.0) descAnimWidth = 195.0;
            needsRedraw = true;
        } else if (!showMenuDesc && descAnimWidth > 0.0) {
            descAnimWidth += (0.0 - descAnimWidth) * 0.4;
            if (descAnimWidth < 1.0) descAnimWidth = 0.0;
            needsRedraw = true;
        }
        
        if (needsRedraw) {
            drawMainMenu();
        }
        
        delay(10);
        return;
    }

    if (appState == STATE_CONTROLS) {
        if (keyChanged && keyPressed) {
            handleControlsInput(globalStatus);
            if (appState == STATE_CONTROLS) drawControlsScreen();
        }
        delay(10);
        return;
    }

    if (appState == STATE_CREDITS) {
        if (keyChanged && keyPressed) {
            handleCreditsInput(globalStatus);
            if (appState == STATE_CREDITS) drawCreditsScreen();
        }
        delay(10);
        return;
    }

    if (appState == STATE_HARDWARE_MENU) {
        if (insaneMode == 1) {
            static unsigned long lastHardwareGlitch = 0;
            static unsigned long nextHardwareGlitch = 500;
            if (now - lastHardwareGlitch > nextHardwareGlitch) {
                drawHardwareMenu();
                lastHardwareGlitch = now;
                nextHardwareGlitch = random(50, 1200);
            }
        }
        
        if (keyChanged && keyPressed) {
            handleHardwareMenuInput(globalStatus);
            if (appState == STATE_HARDWARE_MENU) drawHardwareMenu();
        }
        
        bool needsRedraw = false;
        if (abs(currentHardwareScroll - targetHardwareScroll) > 0.01) {
            currentHardwareScroll += (targetHardwareScroll - currentHardwareScroll) * 0.3; // Smooth lerp
            if (abs(currentHardwareScroll - targetHardwareScroll) <= 0.01) {
                currentHardwareScroll = targetHardwareScroll;
            }
            needsRedraw = true;
        }
        
        if (showHardwareDesc && hardwareDescAnimWidth < 195.0) {
            hardwareDescAnimWidth += (195.0 - hardwareDescAnimWidth) * 0.4;
            if (195.0 - hardwareDescAnimWidth < 1.0) hardwareDescAnimWidth = 195.0;
            needsRedraw = true;
        } else if (!showHardwareDesc && hardwareDescAnimWidth > 0.0) {
            hardwareDescAnimWidth += (0.0 - hardwareDescAnimWidth) * 0.4;
            if (hardwareDescAnimWidth < 1.0) hardwareDescAnimWidth = 0.0;
            needsRedraw = true;
        }
        
        if (needsRedraw) {
            drawHardwareMenu();
        }
        
        delay(10);
        return;
    }
    
    if (appState == STATE_HARDWARE_SETTINGS) {
        if (keyChanged && keyPressed) {
            handleHardwareSettingsInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_LOADING) {
        drawFileLoading();
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_MANAGER) {
        if (keyChanged && keyPressed) {
            handleFileManagerInput(globalStatus);
        }
        
        if (isMp3Playing) {
            if (mp3) {
                if (!mp3->loop()) {
                    stopMp3();
                } else {
                    static unsigned long lastVisualizerUpdate = 0;
                    if (millis() - lastVisualizerUpdate > 100) {
                        drawFileManager();
                        lastVisualizerUpdate = millis();
                    }
                }
            } else {
                stopMp3();
            }
        } else {
            if (!loadedFiles.empty() && loadedFiles[fileManagerSelected].name.length() > 18) {
                if (millis() - lastFileSelectionTime > 1000) {
                    if (millis() - lastMarqueeUpdate > 250) {
                        marqueeScrollOffset++;
                        drawFileManager();
                        lastMarqueeUpdate = millis();
                    }
                }
            }
        }
        
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_ACTIONS_MENU) {
        if (keyChanged && keyPressed) {
            handleFileActionsMenuInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_RENAME_INPUT) {
        if (keyChanged && keyPressed) {
            handleFileRenameInput(globalStatus);
        }
        
        static unsigned long lastRenameBlink = 0;
        if (millis() - lastRenameBlink > 500) {
            blinkState = !blinkState;
            drawFileRenameInput();
            lastRenameBlink = millis();
        }
        
        delay(10);
        return;
    }

    if (appState == STATE_LEADERBOARD) {
        if (keyChanged && keyPressed) {
            Keyboard_Class::KeysState status = globalStatus;
            
            bool hasUp = false, hasDown = false;
            for (char c : status.word) {
                if (c == ';') hasUp = true;
                if (c == '.') hasDown = true;
            }
            
            if (hasDown && leaderboardCursor < totalLeaderboardSize - 1) {
                playSound(sound_hover, sound_hover_size);
                leaderboardCursor++;
                if (leaderboardCursor >= globalLeaderboard.size()) {
                    drawMessage("LOADING MORE...");
                    fetchLeaderboard(globalLeaderboard.size(), 10);
                }
                if (leaderboardCursor > leaderboardScrollOffset + 2) {
                    leaderboardScrollOffset = leaderboardCursor - 2;
                }
                drawLeaderboard();
            }
            
            if (hasUp && leaderboardCursor > 0) {
                playSound(sound_hover, sound_hover_size);
                leaderboardCursor--;
                if (leaderboardCursor < leaderboardScrollOffset) {
                    leaderboardScrollOffset = leaderboardCursor;
                }
                drawLeaderboard();
            }
            
            if (status.enter || status.del) {
                playSound(sound_select, sound_select_size);
                enterMainMenu();
            }
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_ACCOUNT) {
        if (keyChanged && keyPressed) {
            handleAccountInput(globalStatus);
            if (appState == STATE_ACCOUNT) drawAccountMenu();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_GRID_SELECT) {
        if (keyChanged && keyPressed) {
            handleGridSelectInput(globalStatus);
        }
        if (abs(currentGridScroll - targetGridScroll) > 0.01) {
            currentGridScroll += (targetGridScroll - currentGridScroll) * 0.3; // Smooth lerp
            if (abs(currentGridScroll - targetGridScroll) <= 0.01) {
                currentGridScroll = targetGridScroll;
            }
            drawGridSelect();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_PHASE_TRANSITION) {
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawPhaseTransition();
        }
        if (keyChanged && keyPressed) {
            handlePhaseTransitionInput(globalStatus);
            if (appState == STATE_PHASE_TRANSITION) drawPhaseTransition();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_FAILED_SCREEN) {
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawGameOverFailed();
        }
        if (keyChanged && keyPressed) {
            if (globalStatus.enter) {
                playSound(sound_select, sound_select_size);
                lastBreachFailed = true;
                enterMainMenu();
            }
        }
        delay(10);
        return;
    }
    
    if (!gameOver && now - lastUpdate > 10) {
        timeLeft -= (now - lastUpdate);
        lastUpdate = now;
        if (timeLeft <= 0) {
            timeLeft = 0;
            gameOver = true;
            hackSuccess = false;
            playSound(sound_fail, sound_fail_size);
            isAnimating = true;
            animStartTime = now;
            drawScreen();
        } else {
            drawTimer(false);
        }
    }
    
    if (!gameOver && now - lastBlink > 600) {
        blinkState = !blinkState;
        lastBlink = now;
        if (bufferIndex < targetSize) {
            int boxW = min(22, (120 / targetSize) - 2);
            int boxStep = boxW + 2;
            int bx = 120 + bufferIndex*boxStep;
            int by = 83;
            canvas.startWrite();
            canvas.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
            pushCanvas();
        }
    }
    
    if (isAnimating) {
        updateAnimation();
        if (now - animStartTime > 1000) {
            isAnimating = false;
        }
    }
    
    if (keyChanged && keyPressed) {
        Keyboard_Class::KeysState status = globalStatus;
        
        for (char c : status.word) {
            if (c == 27 || c == '`') {
                playSound(sound_select, sound_select_size);
                appState = STATE_GRID_SELECT;
                gridMenuFocus = 0;
                currentGridScroll = 0;
                targetGridScroll = 0;
                drawGridSelect();
                return;
            }
        }
        
        if (gameOver) {
            if (status.enter) {
                playSound(sound_select, sound_select_size);
                if (hackSuccess) {
                    appState = STATE_PHASE_TRANSITION;
                    phaseMenuFocus = 0;
                    drawPhaseTransition();
                } else {
                    appState = STATE_FAILED_SCREEN;
                    drawGameOverFailed();
                }
            }
            return;
        }
        
        bool hasW = false, hasA = false, hasS = false, hasD = false;
        bool hasSemi = false, hasDot = false, hasComma = false, hasSlash = false;
        for (char c : status.word) {
            if (c == 'w') hasW = true;
            if (c == 'a') hasA = true;
            if (c == 's') hasS = true;
            if (c == 'd') hasD = true;
            if (c == ';') hasSemi = true;
            if (c == '.') hasDot = true;
            if (c == ',') hasComma = true;
            if (c == '/') hasSlash = true;
        }
        
        int cR_curr = isRowActive ? activeRow : cursorIdx;
        int cC_curr = isRowActive ? cursorIdx : activeCol;

        if (hasComma || hasA || hasSemi || hasW) {
            bool moved = false;
            if (isRowActive) {
                for(int j=cC_curr-1; j>=0; j--) { if (matrix[cR_curr][j] != "") { cursorIdx = j; moved = true; break; } }
            } else {
                for(int i=cR_curr-1; i>=0; i--) { if (matrix[i][cC_curr] != "") { cursorIdx = i; moved = true; break; } }
            }
            if (moved) playSound(sound_hover, sound_hover_size);
        }
        if (hasSlash || hasD || hasDot || hasS) {
            bool moved = false;
            if (isRowActive) {
                for(int j=cC_curr+1; j<gridSize; j++) { if (matrix[cR_curr][j] != "") { cursorIdx = j; moved = true; break; } }
            } else {
                for(int i=cR_curr+1; i<gridSize; i++) { if (matrix[i][cC_curr] != "") { cursorIdx = i; moved = true; break; } }
            }
            if (moved) playSound(sound_hover, sound_hover_size);
        }
        
        if (status.enter) {
            int cR = isRowActive ? activeRow : cursorIdx;
            int cC = isRowActive ? cursorIdx : activeCol;
            
            if (matrix[cR][cC] != "") {
                playSound(sound_buffer, sound_buffer_size);
                
                buffer[bufferIndex++] = matrix[cR][cC];
                matrix[cR][cC] = ""; 
                
                isRowActive = !isRowActive;
                if (isRowActive) {
                    activeRow = cR;
                } else {
                    activeCol = cC;
                }
                
                int oldIdx = isRowActive ? cC : cR;
                int newCursor = -1;
                int dist = 999;
                if (isRowActive) {
                    for (int j=0; j<gridSize; j++) {
                        if (matrix[activeRow][j] != "") {
                            int d = abs(j - oldIdx);
                            if (d < dist) { dist = d; newCursor = j; }
                        }
                    }
                } else {
                    for (int i=0; i<gridSize; i++) {
                        if (matrix[i][activeCol] != "") {
                            int d = abs(i - oldIdx);
                            if (d < dist) { dist = d; newCursor = i; }
                        }
                    }
                }
                cursorIdx = (newCursor != -1) ? newCursor : oldIdx;
                
                bool isPrefix = true;
                for (int i = 0; i < bufferIndex; i++) {
                    if (buffer[i] != targetSeq[i]) {
                        isPrefix = false;
                        break;
                    }
                }
                
                if (!isPrefix) {
                    gameOver = true;
                    hackSuccess = false;
                    playSound(sound_fail, sound_fail_size);
                    isAnimating = true;
                    animStartTime = millis();
                } else if (bufferIndex >= targetSize) {
                    gameOver = true;
                    hackSuccess = true;
                    lastTimeRatio = (float)timeLeft / (float)maxTime;
                    lastPhaseScore = 1000 * gridSize * currentPhase * (1.0 + lastTimeRatio);
                    accumulatedScore += lastPhaseScore;
                    playSound(sound_success, sound_success_size);
                    isAnimating = true;
                    animStartTime = millis();
                }
            }
        }
        drawScreen();
    }
    delay(10);
}






