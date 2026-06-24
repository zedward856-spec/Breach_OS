#include "M5Cardputer.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include <ArduinoJson.h>

// --- AUDIO PLACEHOLDERS ---
const unsigned char* sound_hover = nullptr;
size_t sound_hover_size = 0;
const unsigned char* sound_select = nullptr;
size_t sound_select_size = 0;
const unsigned char* sound_success = nullptr;
size_t sound_success_size = 0;
const unsigned char* sound_fail = nullptr;
size_t sound_fail_size = 0;

void playSound(const unsigned char* soundData, size_t soundSize) {
    if (soundData != nullptr && soundSize > 0) {
        M5Cardputer.Speaker.playWav(soundData, soundSize);
    }
}
// --------------------------

// Cyberpunk Colors in RGB565
#define CP_YELLOW M5Cardputer.Display.color565(220, 244, 27)
#define CP_CYAN M5Cardputer.Display.color565(56, 190, 201)
#define CP_RED M5Cardputer.Display.color565(255, 0, 60)
#define CP_BG M5Cardputer.Display.color565(14, 17, 21)
#define CP_PANEL M5Cardputer.Display.color565(14, 17, 21)
#define CP_ACTIVE_LINE M5Cardputer.Display.color565(44, 53, 71)
#define CP_DIM M5Cardputer.Display.color565(88, 97, 10)

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
    STATE_PLAYING
};
AppState appState = STATE_SPLASH;

bool isGuest = false;
String authUser = "";
String authPass = "";
int authFocus = 0; 
String savedSSID = "";
String savedWifiPass = "";

std::vector<String> wifiList;
int wifiSelection = 0;
String wifiPass = "";

struct LeaderboardEntry {
    String username;
    int score;
};
std::vector<LeaderboardEntry> globalLeaderboard;
int mainMenuFocus = 0; // 0: PLAY, 1: LEADERBOARD, 2: ACCOUNT

int accountFocus = 0;
String newAccountName = "";
String newAccountPass = "";
int accountHighScore = 0;
bool accountStatsFetched = false;

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
void fetchLeaderboard();

void drawMessage(String msg) {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_YELLOW);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawCenterString(msg, 120, 60);
    M5Cardputer.Display.endWrite();
}

std::vector<String> dummyLogs = {
    "INIT KERNEL...",
    "LOADING MEMORY...",
    "BYPASSING SEC...",
    "AUTH SERVER OK",
    "NODE ACTIVE.",
    "SCANNING...",
    "PORT 80 OPEN",
    "DECRYPTING...",
    "ACCESS GRANTED.",
    "PINGING RELAY...",
    "FETCHING DATA..."
};
int logOffset = 0;
unsigned long lastLogUpdate = 0;

void drawSplash() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    
    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawCenterString("Breach_Protocol", 120, 5);
    
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(125, 40);
    if (WiFi.status() == WL_CONNECTED) {
        M5Cardputer.Display.setTextColor(CP_YELLOW);
        M5Cardputer.Display.print("WIFI: CONNECTED");
    } else {
        M5Cardputer.Display.setTextColor(CP_DIM);
        M5Cardputer.Display.print("WIFI: CONNECTING");
    }
    
    M5Cardputer.Display.setTextColor(CP_DIM);
    M5Cardputer.Display.setCursor(125, 60);
    M5Cardputer.Display.print("VERSION: v3.1");
    
    int maxLogs = 7;
    int y = 35;
    M5Cardputer.Display.setTextColor(CP_ACTIVE_LINE);
    for (int i = 0; i < maxLogs; i++) {
        int logIdx = (logOffset + i) % dummyLogs.size();
        M5Cardputer.Display.setCursor(5, y);
        M5Cardputer.Display.print(dummyLogs[logIdx]);
        y += 11;
    }
    
    if (blinkState) {
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.setCursor(5, 120);
        M5Cardputer.Display.print("> Press ENTER");
    }
    
    M5Cardputer.Display.endWrite();
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
        drawMessage("CONNECTING " + savedSSID + "...");
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

void submitScore() {
    if (WiFi.status() == WL_CONNECTED && !isGuest) {
        HTTPClient http;
        http.begin("http://192.168.0.176:3000/api/leaderboard");
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"username\":\"" + authUser + "\",\"score\":" + String(currentScore) + "}";
        http.POST(payload);
        http.end();
    }
}

void fetchLeaderboard() {
    globalLeaderboard.clear();
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin("http://192.168.0.176:3000/api/leaderboard");
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                JsonArray array = doc.as<JsonArray>();
                int count = 0;
                for (JsonVariant v : array) {
                    if (count >= 5) break;
                    LeaderboardEntry entry;
                    entry.username = v["username"].as<String>();
                    entry.score = v["score"].as<int>();
                    globalLeaderboard.push_back(entry);
                    count++;
                }
            }
        }
        http.end();
    }
}

void drawAuthMenu() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.print("ACCOUNT NAME:");
    
    uint16_t colorUser = (authFocus == 0) ? CP_YELLOW : WHITE;
    M5Cardputer.Display.drawRect(10, 25, 220, 20, colorUser);
    M5Cardputer.Display.setTextColor(colorUser);
    M5Cardputer.Display.setCursor(15, 30);
    M5Cardputer.Display.print(authUser + ((authFocus == 0 && blinkState) ? "_" : ""));

    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setCursor(10, 55);
    M5Cardputer.Display.print("PASSWORD:");

    uint16_t colorPass = (authFocus == 1) ? CP_YELLOW : WHITE;
    M5Cardputer.Display.drawRect(10, 70, 220, 20, colorPass);
    M5Cardputer.Display.setTextColor(colorPass);
    M5Cardputer.Display.setCursor(15, 75);
    String starPass = "";
    for(int i=0; i<authPass.length(); i++) starPass += "*";
    M5Cardputer.Display.print(starPass + ((authFocus == 1 && blinkState) ? "_" : ""));

    uint16_t colorBtn1 = (authFocus == 2) ? CP_YELLOW : WHITE;
    M5Cardputer.Display.drawRect(10, 105, 100, 20, colorBtn1);
    M5Cardputer.Display.setTextColor(colorBtn1);
    M5Cardputer.Display.drawCenterString("LOGIN", 60, 110);
    
    uint16_t colorBtn2 = (authFocus == 3) ? CP_YELLOW : WHITE;
    M5Cardputer.Display.drawRect(130, 105, 100, 20, colorBtn2);
    M5Cardputer.Display.setTextColor(colorBtn2);
    M5Cardputer.Display.drawCenterString("GUEST", 180, 110);
    M5Cardputer.Display.endWrite();
}

void handleAuthInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (authFocus == 2) {
            if (authUser == "") return;
            drawMessage("AUTHENTICATING...");
            
            HTTPClient http;
            http.begin("http://192.168.0.176:3000/api/auth");
            http.addHeader("Content-Type", "application/json");
            String payload = "{\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\"}";
            int httpCode = http.POST(payload);
            
            if (httpCode == 200) {
                String response = http.getString();
                JsonDocument doc;
                deserializeJson(doc, response);
                String action = doc["action"].as<String>();
                
                if (action == "signup") {
                    drawMessage("NEW OPERATIVE REGISTERED!");
                } else {
                    drawMessage("LOGIN SUCCESSFUL!");
                }
                http.end();
                delay(1500);
                
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
        } else if (authFocus == 3) {
            isGuest = true;
            appState = STATE_MAIN_MENU;
            drawMainMenu();
            return;
        }
        authFocus++;
        if (authFocus > 3) authFocus = 0;
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp) {
        authFocus--;
        if (authFocus < 0) authFocus = 3;
        playSound(sound_hover, sound_hover_size);
        return;
    }
    if (hasDown) {
        authFocus++;
        if (authFocus > 3) authFocus = 0;
        playSound(sound_hover, sound_hover_size);
        return;
    }
    
    if (status.del) {
        if (authFocus == 0 && authUser.length() > 0) authUser.remove(authUser.length()-1);
        if (authFocus == 1 && authPass.length() > 0) authPass.remove(authPass.length()-1);
        return;
    }
    
    for (char c : status.word) {
        if (c >= 32 && c <= 126) {
            if (authFocus == 0 && authUser.length() < 16) authUser += c;
            if (authFocus == 1 && authPass.length() < 16) authPass += c;
        }
    }
}

void drawWifiScan() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_YELLOW);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.print("SELECT WIFI NETWORK:");
    
    for (int i = 0; i < wifiList.size(); i++) {
        int y = 25 + i * 12;
        if (i == wifiSelection) {
            M5Cardputer.Display.fillRect(5, y - 1, 230, 11, CP_CYAN);
            M5Cardputer.Display.setTextColor(BLACK, CP_CYAN);
        } else {
            M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        }
        M5Cardputer.Display.setCursor(10, y);
        M5Cardputer.Display.print(wifiList[i]);
    }
    M5Cardputer.Display.endWrite();
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
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_YELLOW);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.print("NETWORK: " + wifiList[wifiSelection]);
    
    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setCursor(10, 40);
    M5Cardputer.Display.print("ENTER PASSWORD:");
    
    M5Cardputer.Display.drawRect(10, 55, 220, 20, WHITE);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setCursor(15, 60);
    M5Cardputer.Display.print(wifiPass + (blinkState ? "_" : ""));
    M5Cardputer.Display.endWrite();
}

void handleWifiPassInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        drawMessage("CONNECTING...");
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
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawCenterString("NETWORK NODE", 120, 15);
    
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(CP_DIM);
    M5Cardputer.Display.drawCenterString("OPERATIVE: " + (isGuest ? String("GUEST") : authUser), 120, 40);
    
    uint16_t colorPlay = (mainMenuFocus == 0) ? CP_YELLOW : WHITE;
    M5Cardputer.Display.drawRect(70, 65, 100, 20, colorPlay);
    M5Cardputer.Display.setTextColor(colorPlay);
    M5Cardputer.Display.drawCenterString("HACK", 120, 70);
    
    uint16_t colorLDB = (mainMenuFocus == 1) ? CP_YELLOW : WHITE;
    M5Cardputer.Display.drawRect(70, 85, 100, 20, colorLDB);
    M5Cardputer.Display.setTextColor(colorLDB);
    M5Cardputer.Display.drawCenterString("LEADERBOARD", 120, 90);
    
    uint16_t colorAccount = (mainMenuFocus == 2) ? CP_YELLOW : WHITE;
    if (!isGuest) {
        M5Cardputer.Display.drawRect(70, 110, 100, 20, colorAccount);
        M5Cardputer.Display.setTextColor(colorAccount);
        M5Cardputer.Display.drawCenterString("ACCOUNT", 120, 115);
    }
    
    M5Cardputer.Display.endWrite();
}

void handleMainMenuInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (mainMenuFocus == 0) {
            appState = STATE_PLAYING;
            currentScore = 0;
            initGame();
            drawScreen();
        } else if (mainMenuFocus == 1) {
            appState = STATE_LEADERBOARD;
            drawMessage("FETCHING DATABANK...");
            fetchLeaderboard();
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
        if (mainMenuFocus < 0) mainMenuFocus = isGuest ? 1 : 2;
        playSound(sound_hover, sound_hover_size);
    }
    if (hasDown) {
        mainMenuFocus++;
        if (mainMenuFocus > (isGuest ? 1 : 2)) mainMenuFocus = 0;
        playSound(sound_hover, sound_hover_size);
    }
}

void drawLeaderboard() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawCenterString("GLOBAL SCORES", 120, 10);
    
    M5Cardputer.Display.setTextSize(1);
    
    if (globalLeaderboard.size() == 0) {
        M5Cardputer.Display.setTextColor(CP_DIM);
        M5Cardputer.Display.drawCenterString("[ NO NETWORK DATA ]", 120, 60);
    } else {
        int y = 45;
        for (int i = 0; i < globalLeaderboard.size(); i++) {
            M5Cardputer.Display.setTextColor(CP_RED);
            M5Cardputer.Display.setCursor(20, y);
            M5Cardputer.Display.print("#" + String(i + 1));
            
            M5Cardputer.Display.setTextColor(WHITE);
            M5Cardputer.Display.setCursor(60, y);
            M5Cardputer.Display.print(globalLeaderboard[i].username);
            
            M5Cardputer.Display.setTextColor(CP_YELLOW);
            M5Cardputer.Display.setCursor(180, y);
            M5Cardputer.Display.print(globalLeaderboard[i].score);
            y += 15;
        }
    }
    
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.drawCenterString("Press ENTER to return", 120, 120);
    M5Cardputer.Display.endWrite();
}

void initGame(bool keepDiff) {
    if (!keepDiff) {
        int sizes[] = {3, 4, 5};
        gridSize = sizes[random(3)];
        if (gridSize == 3) maxTime = 1500;
        else if (gridSize == 4) maxTime = 2000;
        else maxTime = 2500;
        
        targetSize = random(4, 7);
    }

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
    for(int i=0; i<targetSize; i++) buffer[i] = "";
    
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
    
    M5Cardputer.Speaker.setVolume(128);
    
    prefs.begin("breach", false);
    highScore = prefs.getInt("highscore", 0);
    savedSSID = prefs.getString("wifi_ssid", "");
    savedWifiPass = prefs.getString("wifi_pass", "");
    
    if (savedSSID != "") {
        WiFi.begin(savedSSID.c_str(), savedWifiPass.c_str());
    }
    
    appState = STATE_SPLASH;
    drawSplash();
}

void drawTimer(bool forceRedraw = false) {
    static int lastBarWidth = -1;
    static int lastTimeLeft = -1;
    if (forceRedraw) { lastBarWidth = -1; lastTimeLeft = -1; }
    
    int barWidth = map(timeLeft, 0, maxTime, 0, 80);
    if (barWidth < 0) barWidth = 0;
    
    if (barWidth == lastBarWidth && timeLeft == lastTimeLeft) return;
    
    M5Cardputer.Display.startWrite();
    
    if (timeLeft != lastTimeLeft) {
        int secs = timeLeft / 100;
        int centis = timeLeft % 100;
        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", secs, centis);
        M5Cardputer.Display.setTextColor(CP_YELLOW, CP_PANEL);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(95, 5);
        M5Cardputer.Display.print(timeStr);
    }

    if (barWidth != lastBarWidth) {
        if (lastBarWidth == -1 || barWidth > lastBarWidth) {
            M5Cardputer.Display.fillRect(146, 6, barWidth, 6, CP_YELLOW);
            M5Cardputer.Display.fillRect(146 + barWidth, 6, 80 - barWidth, 6, CP_PANEL);
        } else {
            M5Cardputer.Display.fillRect(146 + barWidth, 6, lastBarWidth - barWidth, 6, CP_PANEL);
        }
    }
    M5Cardputer.Display.endWrite();
    
    lastBarWidth = barWidth;
    lastTimeLeft = timeLeft;
}

void drawScreen() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    
    M5Cardputer.Display.fillRect(0, 0, 240, 18, CP_PANEL);
    M5Cardputer.Display.setTextColor(CP_YELLOW, CP_PANEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.print("SCORE: ");
    M5Cardputer.Display.print(currentScore);
    
    M5Cardputer.Display.drawRect(144, 4, 84, 10, CP_YELLOW);
    drawTimer(true);

    // TARGET
    M5Cardputer.Display.setTextColor(WHITE, CP_BG);
    M5Cardputer.Display.setCursor(120, 25);
    M5Cardputer.Display.print("SEQUENCE REQUIRED");
    
    int matchLen = 0;
    for (int i=0; i<bufferIndex; i++) {
        if (buffer[i] == targetSeq[i]) matchLen++;
        else break;
    }
    
    int boxW = min(22, (120 / targetSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;

    for(int i=0; i<targetSize; i++) {
        int tx = 120 + i*boxStep;
        int ty = 38;
        
        if (i < matchLen) {
            M5Cardputer.Display.drawRect(tx, ty, boxW, 18, CP_CYAN);
            M5Cardputer.Display.setTextColor(CP_CYAN, CP_BG);
        } else {
            M5Cardputer.Display.drawRect(tx, ty, boxW, 18, CP_DIM);
            M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        }
        M5Cardputer.Display.setCursor(tx + txtOff, ty + 5);
        M5Cardputer.Display.print(targetSeq[i]);
    }
    
    // BUFFER
    M5Cardputer.Display.setTextColor(CP_YELLOW, CP_BG);
    M5Cardputer.Display.setCursor(120, 70);
    M5Cardputer.Display.print("BUFFER");
    
    for(int i=0; i<targetSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        
        if (i < bufferIndex) {
            M5Cardputer.Display.fillRect(bx, by, boxW, 18, CP_CYAN);
            M5Cardputer.Display.setTextColor(BLACK, CP_CYAN);
            M5Cardputer.Display.setCursor(bx + txtOff, by + 5);
            M5Cardputer.Display.print(buffer[i]);
        } else if (i == bufferIndex) {
            M5Cardputer.Display.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
        } else {
            M5Cardputer.Display.drawRect(bx, by, boxW, 18, CP_DIM);
        }
    }
    
    if (gameOver && !isAnimating) {
        uint16_t color = hackSuccess ? M5Cardputer.Display.color565(66, 245, 84) : CP_RED;
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(color, CP_BG);
        M5Cardputer.Display.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", 170, 106);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        M5Cardputer.Display.drawCenterString("Press ENTER", 170, 125);
        M5Cardputer.Display.fillRect(120, 102, 100, 2, color);
    }
    
    // MATRIX
    int startX = 5 + (5 - gridSize) * 11;
    int startY = 30 + (5 - gridSize) * 10;
    
    if (isRowActive) {
        M5Cardputer.Display.fillRect(startX, startY + activeRow*20, gridSize*22, 18, CP_ACTIVE_LINE);
    } else {
        M5Cardputer.Display.fillRect(startX + activeCol*22, startY, 20, gridSize*20, CP_ACTIVE_LINE);
    }
    
    for(int i=0; i<gridSize; i++) {
        for(int j=0; j<gridSize; j++) {
            int cx = startX + j*22;
            int cy = startY + i*20;
            
            bool isActiveLine = (isRowActive && i == activeRow) || (!isRowActive && j == activeCol);
            uint16_t bgColor = isActiveLine ? CP_ACTIVE_LINE : CP_BG;
            
            if (matrix[i][j] == "") {
                M5Cardputer.Display.setTextColor(CP_DIM, bgColor);
                M5Cardputer.Display.setCursor(cx + 4, cy + 5);
                M5Cardputer.Display.print("[]");
                continue;
            }
            
            bool isHovered = (isRowActive && i == activeRow && j == cursorIdx) || 
                             (!isRowActive && j == activeCol && i == cursorIdx);
            
            if (isHovered) {
                M5Cardputer.Display.fillRect(cx, cy, 20, 18, CP_CYAN);
                M5Cardputer.Display.setTextColor(BLACK, CP_CYAN);
            } else if (isActiveLine) {
                M5Cardputer.Display.setTextColor(CP_YELLOW, CP_ACTIVE_LINE);
            } else {
                M5Cardputer.Display.setTextColor(WHITE, CP_BG);
            }
            M5Cardputer.Display.setCursor(cx + 4, cy + 5);
            M5Cardputer.Display.print(matrix[i][j]);
        }
    }
    M5Cardputer.Display.endWrite();
}

void updateAnimation() {
    unsigned long t = millis() - animStartTime;
    M5Cardputer.Display.startWrite();
    uint16_t color = hackSuccess ? M5Cardputer.Display.color565(66, 245, 84) : CP_RED;
    
    int boxW = min(22, (120 / targetSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;

    for(int i=0; i<targetSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        long delayStart = i * 100;
        if (t > delayStart) {
            int fillHeight = map(t - delayStart, 0, 300, 0, 18);
            if (fillHeight > 18) fillHeight = 18;
            M5Cardputer.Display.fillRect(bx, by + 18 - fillHeight, boxW, fillHeight, color);
            if (i < bufferIndex) {
                M5Cardputer.Display.setTextColor(BLACK);
                M5Cardputer.Display.setCursor(bx + txtOff, by + 5);
                M5Cardputer.Display.print(buffer[i]);
            }
        }
    }
    
    int barWidth = map(t, 0, 600, 0, 100);
    if (barWidth > 100) barWidth = 100;
    M5Cardputer.Display.fillRect(170 - barWidth/2, 102, barWidth, 2, color);
    
    if (t > 600 && t < 650) {
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(color, CP_BG);
        M5Cardputer.Display.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", 170, 106);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        M5Cardputer.Display.drawCenterString("Press ENTER", 170, 125);
    }
    M5Cardputer.Display.endWrite();
}

void drawAccountMenu() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    M5Cardputer.Display.setTextColor(CP_CYAN);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.drawCenterString("OPERATIVE PROFILE", 120, 10);
    
    if (!accountStatsFetched) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(CP_DIM);
        M5Cardputer.Display.drawCenterString("FETCHING DATA...", 120, 40);
        M5Cardputer.Display.endWrite();
        
        HTTPClient http;
        http.begin("http://192.168.0.176:3000/api/account");
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"action\":\"get_stats\",\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\"}";
        int httpCode = http.POST(payload);
        if (httpCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getString());
            accountHighScore = doc["highScore"].as<int>();
        }
        http.end();
        accountStatsFetched = true;
        newAccountName = authUser;
        newAccountPass = authPass;
        
        M5Cardputer.Display.startWrite();
        M5Cardputer.Display.fillScreen(CP_BG);
        M5Cardputer.Display.setTextColor(CP_CYAN);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.drawCenterString("OPERATIVE PROFILE", 120, 10);
    }
    
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(CP_YELLOW);
    M5Cardputer.Display.drawCenterString("HIGH SCORE: " + String(accountHighScore), 120, 35);
    
    M5Cardputer.Display.setTextColor(accountFocus == 0 ? CP_RED : CP_DIM);
    M5Cardputer.Display.drawString("> NAME: " + newAccountName + (accountFocus == 0 && blinkState ? "_" : ""), 20, 60);
    
    M5Cardputer.Display.setTextColor(accountFocus == 1 ? CP_RED : CP_DIM);
    String stars = "";
    for (int i=0; i<newAccountPass.length(); i++) stars += "*";
    M5Cardputer.Display.drawString("> PASS: " + stars + (accountFocus == 1 && blinkState ? "_" : ""), 20, 80);
    
    M5Cardputer.Display.setTextColor(accountFocus == 2 ? WHITE : CP_DIM);
    M5Cardputer.Display.drawString(accountFocus == 2 ? "[ UPDATE ]" : "  UPDATE  ", 20, 110);
    
    M5Cardputer.Display.setTextColor(accountFocus == 3 ? WHITE : CP_DIM);
    M5Cardputer.Display.drawString(accountFocus == 3 ? "[ BACK ]" : "  BACK  ", 140, 110);
    
    M5Cardputer.Display.endWrite();
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
            if (newAccountName == "") return;
            drawMessage("UPDATING...");
            
            HTTPClient http;
            http.begin("http://192.168.0.176:3000/api/account");
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
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c >= 32 && c <= 126 && c != ';' && c != '.') {
            if (accountFocus == 0 && newAccountName.length() < 16) newAccountName += c;
            if (accountFocus == 1 && newAccountPass.length() < 16) newAccountPass += c;
        }
    }
    
    if (hasUp) { playSound(sound_hover, sound_hover_size); accountFocus--; if (accountFocus < 0) accountFocus = 3; }
    if (hasDown) { playSound(sound_hover, sound_hover_size); accountFocus++; if (accountFocus > 3) accountFocus = 0; }
}

void loop() {
    M5Cardputer.update();
    unsigned long now = millis();
    
    if (appState == STATE_SPLASH) {
        if (now - lastLogUpdate > 200) {
            logOffset++;
            lastLogUpdate = now;
            drawSplash();
        }
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawSplash();
        }
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            handleSplashInput(M5Cardputer.Keyboard.keysState());
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
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            handleAuthInput(M5Cardputer.Keyboard.keysState());
            if (appState == STATE_AUTH_MENU) drawAuthMenu();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_WIFI_SCAN) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            handleWifiScanInput(M5Cardputer.Keyboard.keysState());
            if (appState == STATE_WIFI_SCAN) drawWifiScan();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_WIFI_PASS) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            handleWifiPassInput(M5Cardputer.Keyboard.keysState());
            if (appState == STATE_WIFI_PASS) drawWifiPass();
        }
        delay(10);
        return;
    }

    if (appState == STATE_MAIN_MENU) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            handleMainMenuInput(M5Cardputer.Keyboard.keysState());
            if (appState == STATE_MAIN_MENU) drawMainMenu();
        }
        delay(10);
        return;
    }

    if (appState == STATE_LEADERBOARD) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
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
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            handleAccountInput(M5Cardputer.Keyboard.keysState());
            if (appState == STATE_ACCOUNT) drawAccountMenu();
        }
        delay(10);
        return;
    }
    
    if (!gameOver && now - lastUpdate > 10) {
        timeLeft -= (now - lastUpdate) / 10;
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
            M5Cardputer.Display.startWrite();
            M5Cardputer.Display.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
            M5Cardputer.Display.endWrite();
        }
    }
    
    if (isAnimating) {
        updateAnimation();
        if (now - animStartTime > 1000) {
            isAnimating = false;
        }
    }
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        
        if (gameOver) {
            if (status.enter) {
                if (hackSuccess) {
                    playSound(sound_select, sound_select_size);
                    initGame(false); // next level
                    drawScreen();
                } else {
                    playSound(sound_select, sound_select_size);
                    appState = STATE_MAIN_MENU;
                    drawMainMenu();
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
                playSound(sound_select, sound_select_size);
                
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
                for(int i=0; i<bufferIndex; i++) {
                    if (buffer[i] != targetSeq[i]) {
                        isPrefix = false;
                        break;
                    }
                }
                
                if (!isPrefix || bufferIndex >= targetSize) {
                    gameOver = true;
                    if (isPrefix && bufferIndex == targetSize) {
                        hackSuccess = true;
                        currentScore += 100 + (timeLeft / 10);
                        if (currentScore > highScore) {
                            highScore = currentScore;
                            prefs.putInt("highscore", highScore);
                        }
                        submitScore();
                        playSound(sound_success, sound_success_size);
                    } else {
                        hackSuccess = false;
                        playSound(sound_fail, sound_fail_size);
                    }
                    isAnimating = true;
                    animStartTime = millis();
                }
            }
        }
        drawScreen();
    }
    delay(10);
}
