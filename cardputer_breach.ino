#include "M5Cardputer.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
bool secureClientInit = false;

#define API_URL "https://m5cardputer-cyberpunk-breach-protoc.vercel.app/api"

M5Canvas canvas(&M5Cardputer.Display);

// --- AUDIO PLACEHOLDERS ---
#include "sounds.h"

int globalVolume = 255;

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
#define CP_BG canvas.color565(14, 17, 21)
#define CP_PANEL canvas.color565(14, 17, 21)
#define CP_ACTIVE_LINE canvas.color565(44, 53, 71)
#define CP_DIM canvas.color565(88, 97, 10)

String hexCodes[] = {"1C", "55", "BD", "E9", "FF", "7A", "42"};

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
    STATE_PLAYING
};
AppState appState = STATE_SPLASH;

bool isGuest = false;
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
void drawLeaderboard();
void drawAccountMenu();
void drawGridSelect();
void drawPhaseTransition();
void drawGameOverFailed();
void fetchLeaderboard(int offset = 0, int limit = 10);

void drawMessage(String msg, String line2 = "");
void drawGlitchText(String text, int x, int y, int size, uint16_t color, bool center = true) {
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
    canvas.pushSprite(0, 0); canvas.endWrite();
}

void drawChippedButton(int x, int y, int w, int h, uint16_t color) {
    int chip = (h > 25) ? 8 : 5;
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
    canvas.drawCenterString("Breach_Protocol", 120, 5);
    
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

    canvas.fillRect(125, 40, 115, 30, CP_BG);
    
    canvas.setCursor(125, 40);
    if (WiFi.status() == WL_CONNECTED) {
        canvas.setTextColor(CP_YELLOW);
        canvas.print("WIFI: CONNECTED");
    } else {
        canvas.setTextColor(CP_DIM);
        canvas.print("WIFI: CONNECTING");
    }
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(125, 60);
    canvas.print("VERSION: v5.0sound");
    
    if (blinkState) {
        canvas.setTextColor(WHITE);
        canvas.setCursor(5, 120);
        canvas.print("> Press ENTER");
    }
    
    canvas.pushSprite(0, 0); canvas.endWrite();
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
}

void startWifiScan() {
    if (savedSSID != "" && savedWifiPass != "") {
        drawMessage("CONNECTING TO:", savedSSID);
        WiFi.begin(savedSSID.c_str(), savedWifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40) {
            delay(100);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            appState = STATE_AUTH_MENU;
            drawAuthMenu();
            return;
        }
    }

    drawMessage("SCANNING WIFI...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    wifiList.clear();
    for (int i = 0; i < n && i < 10; ++i) {
        wifiList.push_back(WiFi.SSID(i));
    }
    appState = STATE_WIFI_SCAN;
    wifiSelection = 0;
    drawWifiScan();
}

void submitScore(int scoreToSubmit) {
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
    canvas.drawCenterString("LOGIN", 60, 115);
    
    uint16_t colorBtn2 = (authFocus == 4) ? CP_YELLOW : WHITE;
    drawChippedButton(130, 110, 100, 20, colorBtn2);
    canvas.setTextColor(colorBtn2);
    canvas.drawCenterString("GUEST", 180, 115);
    canvas.pushSprite(0, 0); canvas.endWrite();
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
            drawMessage("AUTHENTICATING...");
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
                
                playSound(wifi_finished_wav, wifi_finished_wav_len);
                
                if (action == "signup") {
                    drawMessage("NEW OPERATIVE REGISTERED!");
                } else {
                    drawMessage("LOGIN SUCCESSFUL!");
                }
                http.end();
                delay(1500);
                
                if (rememberMe) {
                    prefs.putString("user", authUser);
                    prefs.putString("pass", authPass);
                } else {
                    prefs.putString("user", "");
                    prefs.putString("pass", "");
                }
                
                isGuest = false;
                appState = STATE_MAIN_MENU;
                drawMainMenu();
            } else {
                http.end();
                drawMessage("ACCESS DENIED");
                delay(2000);
                drawAuthMenu();
            }
            return;
        } else if (authFocus == 4) {
            isGuest = true;
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            appState = STATE_MAIN_MENU;
            drawMainMenu();
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
    canvas.setCursor(5, 5);
    canvas.print("SELECT WIFI NETWORK:");
    
    for (int i = 0; i < wifiList.size(); i++) {
        int y = 25 + i * 12;
        if (i == wifiSelection) {
            canvas.fillRect(5, y - 1, 230, 11, CP_CYAN);
            canvas.setTextColor(BLACK, CP_CYAN);
        } else {
            canvas.setTextColor(WHITE, CP_BG);
        }
        canvas.setCursor(10, y);
        canvas.print(wifiList[i]);
    }
    canvas.pushSprite(0, 0); canvas.endWrite();
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
    canvas.pushSprite(0, 0); canvas.endWrite();
}

void handleWifiPassInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        drawMessage("CONNECTING TO:", wifiList[wifiSelection]);
        WiFi.begin(wifiList[wifiSelection].c_str(), wifiPass.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 50) {
            delay(100);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            prefs.putString("wifi_ssid", wifiList[wifiSelection]);
            prefs.putString("wifi_pass", wifiPass);
            savedSSID = wifiList[wifiSelection];
            savedWifiPass = wifiPass;
            appState = STATE_AUTH_MENU;
            drawAuthMenu();
        } else {
            playSound(sound_fail, sound_fail_size);
            drawMessage("WIFI FAILED!");
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

void drawMainMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    drawGlitchText("NETWORK NODE", 120, 15, 2, CP_CYAN);
    
    canvas.setTextSize(1);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("OPERATIVE: " + (isGuest ? String("GUEST") : authUser), 120, 40);
    
    uint16_t colorPlay = (mainMenuFocus == 0) ? CP_YELLOW : WHITE;
    drawChippedButton(70, 60, 100, 20, colorPlay);
    canvas.setTextColor(colorPlay);
    canvas.drawCenterString("HACK", 120, 65);
    
    if (!isGuest) {
        uint16_t colorLDB = (mainMenuFocus == 1) ? CP_YELLOW : WHITE;
        drawChippedButton(70, 85, 100, 20, colorLDB);
        canvas.setTextColor(colorLDB);
        canvas.drawCenterString("LEADERBOARD", 120, 90);
        
        uint16_t colorAccount = (mainMenuFocus == 2) ? CP_YELLOW : WHITE;
        drawChippedButton(70, 110, 100, 20, colorAccount);
        canvas.setTextColor(colorAccount);
        canvas.drawCenterString("ACCOUNT", 120, 115);
    }
    
    canvas.pushSprite(0, 0); canvas.endWrite();
}

void handleMainMenuInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (mainMenuFocus == 0) {
            appState = STATE_GRID_SELECT;
            gridMenuFocus = 0;
            drawGridSelect();
        } else if (mainMenuFocus == 1 && !isGuest) { // LEADERBOARD
            appState = STATE_LEADERBOARD;
            drawMessage("FETCHING DATABANK...");
            fetchLeaderboard(0, 10);
            leaderboardCursor = 0;
            leaderboardScrollOffset = 0;
            drawLeaderboard();
        } else if (mainMenuFocus == 2 && !isGuest) {
            appState = STATE_ACCOUNT;
            accountFocus = 0;
            accountStatsFetched = false;
        }
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp) {
        mainMenuFocus--;
        if (mainMenuFocus < 0) mainMenuFocus = isGuest ? 0 : 2;
        playSound(sound_hover, sound_hover_size);
    }
    if (hasDown) {
        mainMenuFocus++;
        if (mainMenuFocus > (isGuest ? 0 : 2)) mainMenuFocus = 0;
        playSound(sound_hover, sound_hover_size);
    }
}

void drawGridSelect() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("SELECT GRID SIZE", 120, 5);
    
    int btnW = 232;
    int btnH = 32;
    int startX = 4;
    int startY = 25;
    int spacing = 36;
    
    for (int i = 0; i < 3; i++) {
        uint16_t c = (gridMenuFocus == i) ? CP_YELLOW : WHITE;
        int y = startY + i * spacing;
        
        drawChippedButton(startX, y, btnW, btnH, c);
        
        canvas.setTextColor(c);
        canvas.setTextSize(2);
        String label = (i == 0) ? "3x3" : ((i == 1) ? "4x4" : "5x5");
        canvas.setCursor(startX + 15, y + 10);
        canvas.print(label);
        
        canvas.setTextSize(1);
        canvas.setTextColor(CP_DIM);
        String desc = (i == 0) ? "Base Points Payout" : ((i == 1) ? "Higher Points Payout" : "Maximum Points Payout");
        canvas.setCursor(startX + 65, y + 13);
        canvas.print(desc);
    }
    
    canvas.pushSprite(0, 0); canvas.endWrite();
}

void handleGridSelectInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
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
    for (char c : status.word) {
        if (c == ',' || c == ';') hasUp = true;
        if (c == '/' || c == '.') hasDown = true;
    }
    if (hasUp) { playSound(sound_hover, sound_hover_size); gridMenuFocus--; if (gridMenuFocus < 0) gridMenuFocus = 2; }
    if (hasDown) { playSound(sound_hover, sound_hover_size); gridMenuFocus++; if (gridMenuFocus > 2) gridMenuFocus = 0; }
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
        canvas.drawCenterString("CONTINUE", 65, 100);
        
        uint16_t colSave = (phaseMenuFocus == 1) ? CP_YELLOW : WHITE;
        drawChippedButton(130, 95, 90, 20, colSave);
        canvas.setTextColor(colSave);
        canvas.drawCenterString("SAVE & EXIT", 175, 100);
    } else {
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("ALL PHASES COMPLETE!", 120, 70);
        uint16_t colSave = CP_YELLOW;
        drawChippedButton(75, 95, 90, 20, colSave);
        canvas.setTextColor(colSave);
        canvas.drawCenterString("SAVE SCORE", 120, 100);
    }
    
    canvas.pushSprite(0, 0); canvas.endWrite();
}

void handlePhaseTransitionInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (currentPhase >= 8 || phaseMenuFocus == 1) {
            drawMessage("SAVING SCORE...");
            submitScore(accumulatedScore);
            appState = STATE_MAIN_MENU;
            drawMainMenu();
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
    uint16_t btnColor = blinkState ? CP_YELLOW : CP_DIM;
    drawChippedButton(70, 95, 100, 20, btnColor);
    canvas.setTextColor(btnColor);
    canvas.setTextSize(1);
    canvas.drawCenterString("PRESS ENTER", 120, 101);
    canvas.pushSprite(0, 0); canvas.endWrite();
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
    canvas.pushSprite(0, 0); canvas.endWrite();
}

void initGame(bool keepDiff) {
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
    
    M5Cardputer.Speaker.setVolume(255);
    
    prefs.begin("breach", false);
    highScore = prefs.getInt("highscore", 0);
    savedSSID = prefs.getString("wifi_ssid", "");
    savedWifiPass = prefs.getString("wifi_pass", "");
    authUser = prefs.getString("user", "");
    authPass = prefs.getString("pass", "");
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
    canvas.pushSprite(0, 0); canvas.endWrite();
    
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
    canvas.pushSprite(0, 0); canvas.endWrite();
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
    canvas.pushSprite(0, 0); canvas.endWrite();
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
        canvas.pushSprite(0, 0); canvas.endWrite();
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
    canvas.drawString("> NAME:   " + newAccountName + (accountFocus == 0 && blinkState ? "_" : ""), 10, 70);
    
    uint16_t c1 = (accountFocus == 1) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c1);
    String stars = "";
    for (int i=0; i<newAccountPass.length(); i++) stars += "*";
    canvas.drawString("> PASS:   " + stars + (accountFocus == 1 && blinkState ? "_" : ""), 10, 85);
    
    uint16_t c2 = (accountFocus == 2) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 110, 100, 20, c2);
    canvas.setTextColor(c2);
    canvas.drawCenterString("UPDATE", 60, 115);
    
    uint16_t c3 = (accountFocus == 3) ? CP_YELLOW : WHITE;
    drawChippedButton(130, 110, 100, 20, c3);
    canvas.setTextColor(c3);
    canvas.drawCenterString("BACK", 180, 115);
    
    canvas.pushSprite(0, 0); canvas.endWrite();
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
            if (newAccountName == authUser && newAccountPass == authPass) {
                accountFocus = 3;
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
            appState = STATE_MAIN_MENU;
            drawMainMenu();
            return;
        } else if (accountFocus == 3) {
            accountStatsFetched = false;
            appState = STATE_MAIN_MENU;
            drawMainMenu();
            return;
        }
        accountFocus++;
        if (accountFocus > 3) accountFocus = 0;
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
        if (accountFocus == 2 || accountFocus == 3) accountFocus = 1;
        else if (accountFocus == 1) accountFocus = 0;
        else if (accountFocus == 0) accountFocus = 2; 
    }
    if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        if (accountFocus == 0) accountFocus = 1;
        else if (accountFocus == 1) accountFocus = 2;
        else if (accountFocus == 2 || accountFocus == 3) accountFocus = 0;
    }
    if (hasLeft || hasRight) {
        if (accountFocus == 2) { accountFocus = 3; playSound(sound_hover, sound_hover_size); }
        else if (accountFocus == 3) { accountFocus = 2; playSound(sound_hover, sound_hover_size); }
    }
}

void loop() {
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
        for (auto i : status.word) {
            if (i == '-' || i == '_') {
                globalVolume -= 25;
                if (globalVolume < 0) globalVolume = 0;
                volChanged = true;
            } else if (i == '=' || i == '+') {
                globalVolume += 25;
                if (globalVolume > 255) globalVolume = 255;
                volChanged = true;
            }
        }
        if (volChanged) {
            M5Cardputer.Speaker.setVolume(globalVolume);
            playSound(sound_hover, sound_hover_size);
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

    if (appState == STATE_MAIN_MENU) {
        static unsigned long lastMenuGlitch = 0;
        static unsigned long nextMenuGlitch = 1000;
        if (now - lastMenuGlitch > nextMenuGlitch) {
            drawMainMenu();
            lastMenuGlitch = now;
            nextMenuGlitch = random(100, 3000);
        }
        
        if (keyChanged && keyPressed) {
            handleMainMenuInput(globalStatus);
            if (appState == STATE_MAIN_MENU) drawMainMenu();
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
                appState = STATE_MAIN_MENU;
                drawMainMenu();
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
            if (appState == STATE_GRID_SELECT) drawGridSelect();
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
                appState = STATE_MAIN_MENU;
                drawMainMenu();
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
            canvas.pushSprite(0, 0); canvas.endWrite();
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
                drawGridSelect();
                return;
            }
        }
        
        if (gameOver) {
            if (status.enter) {
                if (hackSuccess) {
                    playSound(sound_select, sound_select_size);
                    appState = STATE_PHASE_TRANSITION;
                    phaseMenuFocus = 0;
                    drawPhaseTransition();
                } else {
                    playSound(sound_select, sound_select_size);
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






