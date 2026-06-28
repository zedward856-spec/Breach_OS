#include "M5Cardputer.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>

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
    STATE_CREDITS
};
AppState appState = STATE_SPLASH;

bool isGuest = false;
bool insaneMode = false;
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

void drawMessage(String msg, String line2 = "");
void drawGlitchText(String text, int x, int y, int size, uint16_t color, bool center = true, bool forceGlitch = false) {
    bool canGlitch = insaneMode || forceGlitch;
    if (appState == STATE_MAIN_MENU && abs(currentMenuScroll - targetMenuScroll) > 0.01) {
        canGlitch = false;
    }
    if (!canGlitch) {
        canvas.setTextSize(size);
        canvas.setTextColor(color);
        if (center) canvas.drawCenterString(text, x, y);
        else canvas.drawString(text, x, y);
        return;
    }
    bool glitch = (random(100) < 60); // 60% chance to glitch when called
    canvas.setTextSize(size);
    
    if (!glitch) {
        canvas.setTextColor(color);
        if (center) canvas.drawCenterString(text, x, y);
        else canvas.drawString(text, x, y);
        return;
    }
    
    // Jitter
    int jx = x + random(-4, 5);
    int jy = y + random(-2, 3);
    
    // Color separation
    if (random(2) == 0) {
        canvas.setTextColor(TFT_MAGENTA);
        if (center) canvas.drawCenterString(text, jx + random(2, 5), jy);
        else canvas.drawString(text, jx + random(2, 5), jy);
    }
    
    canvas.setTextColor(random(2) == 0 ? WHITE : color);
    if (center) canvas.drawCenterString(text, jx, jy);
    else canvas.drawString(text, jx, jy);
    
    // Slice (erase horizontal lines)
    int numSlices = random(1, 4);
    for (int i=0; i<numSlices; i++) {
        int sy = jy + random(0, size * 8);
        canvas.drawLine(jx - 100, sy, jx + 100, sy, CP_BG);
        if (random(2) == 0) canvas.drawLine(jx - 100, sy+1, jx + 100, sy+1, CP_BG);
    }
    
    // Random artifact lines
    if (random(3) == 0) {
        int ax = jx + random(-50, 50);
        int ay = jy + random(0, size * 8);
        canvas.drawLine(ax, ay, ax + random(10, 30), ay, color);
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






void drawChippedButton(int x, int y, int w, int h, uint16_t color) {
    int chip = (h > 25) ? 8 : 5;
    if (w <= chip) return;
    canvas.drawLine(x, y, x + w, y, color);
    canvas.drawLine(x, y, x, y + h, color);
    canvas.drawLine(x, y + h, x + w - chip, y + h, color);
    canvas.drawLine(x + w, y, x + w, y + h - chip, color);
    canvas.drawLine(x + w, y + h - chip, x + w - chip, y + h, color);
}

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
    
    canvas.drawString("> Press ", 5, 115);
    int x1 = 5 + canvas.textWidth("> Press ");
    drawGlitchText("ENTER", x1, 115, 1, WHITE, false, true);
    int x2 = x1 + canvas.textWidth("ENTER");
    canvas.setTextColor(WHITE);
    canvas.drawString(" to Connect", x2, 115);
    
    canvas.drawString("> Press ", 5, 125);
    int x3 = 5 + canvas.textWidth("> Press ");
    drawGlitchText("ESC", x3, 125, 1, WHITE, false, true);
    int x4 = x3 + canvas.textWidth("ESC");
    canvas.setTextColor(WHITE);
    canvas.drawString(" to Play Offline", x4, 125);
    
    pushCanvas();
}

void handleSplashInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (WiFi.status() == WL_CONNECTED) {
            appState = STATE_AUTH_MENU;
            drawAuthMenu();
        } else {
            startWifiScan();
        }
    }
    
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == '`') hasEsc = true; // ESC key on Cardputer
    }
    
    if (hasEsc) {
        playSound(sound_select, sound_select_size);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        isGuest = true;
        authUser = "GUEST";
        enterMainMenu();
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
    drawMainMenu();
}

void drawMainMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw headers centered on the right side of the screen to avoid the scroll wheel
    drawGlitchText("NETWORK NODE", 135, 12, 2, CP_CYAN, true, false);
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

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(240, 135);
    
    prefs.begin("breach", false);
    highScore = prefs.getInt("highscore", 0);
    savedSSID = prefs.getString("wifi_ssid", "");
    savedWifiPass = prefs.getString("wifi_pass", "");
    insaneMode = prefs.getBool("insane", false);
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
    drawGlitchText("> INSANE MODE: " + String(insaneMode ? "[ON]" : "[OFF]"), 10, 90, 1, c2, false);

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
            insaneMode = !insaneMode;
            prefs.putBool("insane", insaneMode);
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
    
    if (insaneMode) {
        static unsigned long lastInsane = 0;
        static unsigned long nextInsane = 500;
        if (now - lastInsane > nextInsane) {
            if (appState == STATE_AUTH_MENU) drawAuthMenu();
            else if (appState == STATE_MAIN_MENU) drawMainMenu();
            else if (appState == STATE_ACCOUNT) drawAccountMenu();
            else if (appState == STATE_GRID_SELECT) drawGridSelect();
            else if (appState == STATE_PHASE_TRANSITION) drawPhaseTransition();
            else if (appState == STATE_FAILED_SCREEN) drawGameOverFailed();

            lastInsane = now;
            nextInsane = random(50, 1200);
        }
    }
    
    if (appState == STATE_MAIN_MENU) {
        if (!insaneMode) {
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






