// Primary Arduino sketch tab.
// Shared includes, types, global state, and forward declarations live here.
// Feature code is split into the numbered .ino tabs in this folder.

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
#include <Update.h>
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


// Core globals and declarations
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
    STATE_FILE_RENAME_INPUT,
    STATE_OTA_CATALOG,
    STATE_DIR_CONFIRM_POPUP,
    STATE_MUSIC_PLAYER
};
AppState appState = STATE_SPLASH;

bool isGuest = false;
int insaneMode = 0;
bool lastBreachFailed = false;
String authUser = "";
String authPass = "";
int authFocus = 0; 
bool rememberMe = false;
String savedSSID = "";
String savedWifiPass = "";

std::vector<String> wifiList;
int wifiSelection = 0;
int wifiScrollOffset = 0;
String wifiPass = "";
bool resumeOtaAfterWifi = false;

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
void drawOtaCatalog();
void enterOtaCatalog();
void handleOtaCatalogInput(Keyboard_Class::KeysState status);
void performOtaUpdate(String binUrl);
bool fetchOtaCatalog();
String resolveOtaFirmwareUrl(String fid);
bool fetchOtaFirmwareDetails(String fid);
void drawOtaFirmwareDetails();
void startMp3(String fileName);
void drawDirConfirmPopup();
void handleDirConfirmPopupInput(Keyboard_Class::KeysState status);
void drawMusicPlayer();
void handleMusicPlayerInput(Keyboard_Class::KeysState status);
void populatePlaylist();
void playNextTrack();
void playPrevTrack();
void startMp3InPlayer(String fileName);
void stopMp3Playback();
void stopMp3();
void drawMessage(String msg);
void drawMessage(String msg, String line2);
// Additional forward declarations for functions now split across sketch tabs.
void bootToFactory();
void playSound(const unsigned char* soundData, size_t soundSize);
bool isSystemFile(String name);
bool deleteRecursive(String path);
bool compareFiles(const RealFile& a, const RealFile& b);
void populateFileList();
void readSelectedFileContent(String fileName);
void drawChippedButton(int x, int y, int w, int h, uint16_t color);
void handleSplashInput(Keyboard_Class::KeysState status);
void startWifiScan();
void submitScore(int scoreToSubmit);
void handleAuthInput(Keyboard_Class::KeysState status);
void handleWifiScanInput(Keyboard_Class::KeysState status);
void handleWifiPassInput(Keyboard_Class::KeysState status);
void handleMainMenuInput(Keyboard_Class::KeysState status);
void drawRotatedText(String text, int cx, int cy, uint16_t color);
void handleGridSelectInput(Keyboard_Class::KeysState status);
void handlePhaseTransitionInput(Keyboard_Class::KeysState status);
void initSPIFFS();
void drawTimer(bool forceRedraw);
void updateAnimation();
void handleAccountInput(Keyboard_Class::KeysState status);

// Hardware/file/audio/OTA globals
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
int settingsTab = 0;
int settingsTabScrollOffset = 0;
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
String mp3PlayLoopMode = "name";
std::vector<String> playlist;
int playlistFocus = 0;
int playlistScrollOffset = 0;
String musicDir = "/";
bool isDirSelectionMode = false;
String pendingSelectedDir = "";
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

struct FirmwareCatalogItem {
    String fid;
    String name;
    String author;
    String version;
    String binUrl;
    String desc;
};

std::vector<FirmwareCatalogItem> otaCatalog;
bool otaCatalogLoaded = false;
String otaSortField = "downloads";

int otaCatalogPage = 1;
int otaCatalogTotal = 0;
int otaCatalogFocus = 0;
int otaCatalogScrollOffset = 0;

bool otaDetailMode = false;
struct FirmwareVersionItem {
    String version;
    String file;
    String publishedAt;
};
std::vector<FirmwareVersionItem> otaVersions;
int otaVersionFocus = 0;
int otaDescScrollOffset = 0;
String otaDetailDesc = "";
String otaDetailAuthor = "";
int otaDetailStars = 0;
String otaDetailName = "";

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
    "[ OK ] Started Breach_OS.",
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

