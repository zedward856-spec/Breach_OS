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
#include <WebServer.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <ArduinoJson.h>
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <driver/gpio.h>
#include "libssh_esp32.h"
#include <libssh/libssh.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <AudioOutput.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceSPIFFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourceBuffer.h>
#include "bluetooth_scan_types.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#if (defined(SOC_BLE_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)) && \
    (defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED))
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#define BREACH_BLE_SCAN_AVAILABLE 1
#else
#define BREACH_BLE_SCAN_AVAILABLE 0
#endif
#define EXCLUDE_EXOTIC_PROTOCOLS
#define DECODE_NEC
#define DECODE_SAMSUNG
#define DECODE_LG
#define DECODE_SONY
#define DECODE_RC5
#define DECODE_RC6
#define DECODE_DISTANCE_WIDTH
#define DECODE_HASH
#define RAW_BUFFER_LENGTH 750
#define NO_LED_FEEDBACK_CODE
#include <IRremote.hpp>
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 0)
#include <USB.h>
#include <USBMSC.h>
#include <USBHIDKeyboard.h>
#define BREACH_USB_MSC_AVAILABLE 1
#define BREACH_USB_HID_AVAILABLE 1
#else
#define BREACH_USB_MSC_AVAILABLE 0
#define BREACH_USB_HID_AVAILABLE 0
#endif

static constexpr int AUDIO_SPECTRUM_BARS = 18;
static constexpr const char* PREF_BOOT_SOUND_OFF = "boot_snd_off"; // ESP32 NVS keys max out at 15 chars.
void feedAudioSpectrumSample(int16_t left, int16_t right);

struct RealFile {
    String name;
    String sizeStr;
    bool isDir;
};

struct BluetoothScanDeviceInfo;

class AudioOutputM5Speaker : public AudioOutput {
  public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) { 
        _m5sound = m5sound; 
        _virtual_ch = virtual_sound_channel; 
    }
    virtual ~AudioOutputM5Speaker(void) {};
    virtual bool begin(void) override { return true; }
    bool hasOutputStarted() const { return _output_started; }
    virtual bool ConsumeSample(int16_t sample[2]) override {
      feedAudioSpectrumSample(sample[0], sample[1]);
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
        _output_started = true;
        _tri_index = _tri_index < 2 ? _tri_index + 1 : 0; 
        _tri_buffer_index = 0; 
      }
    }
    virtual bool stop(void) override { 
        flush(); 
        _m5sound->stop(_virtual_ch); 
        _output_started = false;
        return true; 
    }
  protected:
    m5::Speaker_Class* _m5sound; 
    uint8_t _virtual_ch; 
    static constexpr size_t tri_buf_size = 512;
    int16_t _tri_buffer[3][tri_buf_size]; 
    size_t _tri_buffer_index = 0; 
    size_t _tri_index = 0;
    bool _output_started = false;
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
WiFiClient otaDataClient;
WiFiClientSecure secureClient;
WiFiClient sshClient;
WiFiClient telnetClient;
SemaphoreHandle_t sshMutex = NULL;
TaskHandle_t sshTaskHandle = NULL;
static constexpr uint32_t BREACH_SSH_TASK_STACK_SIZE = 1024 * 32;
static constexpr size_t SSH_IO_BUFFER_SIZE = 256;
static constexpr size_t SSH_MAX_QUEUE_BYTES = 3072;
bool secureClientInit = false;
bool otaInit = false;
bool sshLibReady = false;

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
    STATE_BREACH_MODE,
    STATE_AUTH_MENU,
    STATE_WIFI_SCAN,
    STATE_WIFI_PASS,
    STATE_MAIN_MENU,
    STATE_LEADERBOARD,
    STATE_ACCOUNT,
    STATE_SSH,
    STATE_TELNET_BBS,
    STATE_TEXTFILES,
    STATE_BLUETOOTH_SCAN,
    STATE_WIFI_SCANNER,
    STATE_AP_MODE,
    STATE_GRID_SELECT,
    STATE_PHASE_TRANSITION,
    STATE_FAILED_SCREEN,
    STATE_PLAYING,
    STATE_CONTROLS,
    STATE_CREDITS,
    STATE_GITHUB_QR,
    STATE_HARDWARE_MENU,
    STATE_FILE_MANAGER,
    STATE_FILE_LOADING,
    STATE_HARDWARE_SETTINGS,
    STATE_FILE_ACTIONS_MENU,
    STATE_FILE_NEW_TYPE_MENU,
    STATE_FILE_RENAME_INPUT,
    STATE_FILE_TEXT_EDITOR,
    STATE_OTA_CATALOG,
    STATE_DIR_CONFIRM_POPUP,
    STATE_MUSIC_PLAYER,
    STATE_USB_DRIVE,
    STATE_BADUSB,
    STATE_IR,
    STATE_SSTV
};
AppState appState = STATE_SPLASH;
bool suppressBatteryPercentBox = false;

bool isGuest = false;
bool launchBreachAfterAuth = false;
bool launchAccountAfterAuth = false;
bool accountReturnToBreach = false;
bool leaderboardReturnToBreach = false;
int breachModeFocus = 0;
float currentBreachScroll = 0;
float targetBreachScroll = 0;
int insaneMode = 0;
bool lastBreachFailed = false;
String authUser = "";
String authPass = "";
int authFocus = 0; 
bool rememberMe = false;
String savedSSID = "";
String savedWifiPass = "";
static constexpr const char* WIFI_CREDENTIALS_OS_DIR = "/Breach_OS";
static constexpr const char* WIFI_CREDENTIALS_FILE = "/Breach_OS/wifi.txt";
static constexpr const char* WIFI_CREDENTIALS_LEGACY_FILE = "/wifi.txt";
String apModeSsid = "Breach_OS_AP";
String apModePass = "breach123";
int apModeChannel = 6;
bool apModeOpen = false;
bool apModeRunning = false;
bool apModeStaWasConnected = false;
int apModeFocus = 0;
String apModeStatus = "READY";
String apModeIp = "";

std::vector<String> wifiList;
int wifiSelection = 0;
int wifiScrollOffset = 0;
String wifiPass = "";
bool resumeOtaAfterWifi = false;
bool resumeTextfilesAfterWifi = false;
bool resumeApModeLanWebAfterWifi = false;

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
int mainMenuFocus = 0; // NETWORK NODE wheel focus

int accountFocus = 0;
String newAccountName = "";
String newAccountPass = "";
int accountHighScore = 0;
int accountRank = 0;
int accountHighGrid = 0;
int accountHighPhase = 0;
bool accountStatsFetched = false;

int sshFocus = 0;
const char* BREACH_DEFAULT_SSH_TARGET = "sl01220@raspi";
const char* BREACH_SSH_PROMPT_HEADER = "-[sl01220@raspi]-[~]";
const char* BREACH_SSH_LOCAL_PROMPT = "-> ";
const char* BREACH_SSH_REMOTE_PROMPT_CMD =
    "stty -echo 2>/dev/null; bind 'set enable-bracketed-paste off' 2>/dev/null; unset PROMPT_COMMAND; export PS1=''; printf '\\033[?2004l\\033[2J\\033[H'\r";
const char* BREACH_SSH_CLEAR_SCREEN = "\033[2J\033[H";
static constexpr int BREACH_SSH_PTY_COLS = 40;
static constexpr int BREACH_SSH_PTY_ROWS = 12;
static constexpr int BREACH_SSH_VISIBLE_COLS = 38;
static constexpr int BREACH_SSH_VISIBLE_ROWS = 6;
static constexpr int BREACH_SSH_LOG_MAX = 1800;
String sshTarget = "";
String sshHost = "";
String sshPort = "22";
String sshUser = "";
String sshPass = "";
bool sshRememberMe = false;
String sshStatus = "READY";
String sshBanner = "";
bool sshConnected = false;
bool sshTerminalMode = false;
bool sshTerminalDirty = false;
String sshTerminalLog = "";
String sshInputLine = "";
int sshScrollOffset = 0;
bool sshAnsiEsc = false;
bool sshAnsiCsi = false;
bool sshShellReady = false;
bool sshUseCustomPrompt = false;
String sshQueuedCommand = "";
String sshQueuedOutput = "";
String sshWorkerHost = "";
String sshWorkerUser = "";
String sshWorkerPass = "";
int sshWorkerPort = 22;
bool sshWorkerUseCustomPrompt = false;
bool sshStopRequested = false;
bool sshTaskExited = false;

int telnetFocus = 0;
static constexpr int BREACH_TELNET_VISIBLE_COLS = 38;
static constexpr int BREACH_TELNET_VISIBLE_ROWS = 6;
static constexpr int BREACH_TELNET_BBS_COLS = 80;
static constexpr int BREACH_TELNET_BBS_ROWS = 24;
static constexpr int BREACH_TELNET_PAN_COL_STEP = 10;
static constexpr int BREACH_TELNET_PAN_ROW_STEP = 3;
static constexpr int BREACH_TELNET_LOG_MAX = 1800;
String telnetTarget = "";
String telnetHost = "";
String telnetPort = "23";
String telnetStatus = "READY";
String telnetTerminalLog = "";
String telnetInputLine = "";
int telnetScrollOffset = 0;
char telnetScreen[BREACH_TELNET_BBS_ROWS][BREACH_TELNET_BBS_COLS + 1];
int telnetCursorCol = 0;
int telnetCursorRow = 0;
int telnetPanCol = 0;
int telnetPanRow = 0;
String telnetAnsiParams = "";
bool telnetRememberMe = false;
bool telnetConnected = false;
bool telnetTerminalMode = false;
bool telnetTerminalDirty = false;
bool telnetAnsiEsc = false;
bool telnetAnsiCsi = false;
int telnetIacState = 0;
int telnetIacCommand = 0;
int telnetSbOption = -1;
String telnetSbData = "";

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
void resetSplashBootScroll();
void drawBreachModePrompt();
void handleBreachModeInput(Keyboard_Class::KeysState status);
void resetBreachModeScroll();
void returnToBreachMode();
void startBreachGridSelect();
void startOfflineBreach();
void startOnlineBreach();
void openBreachAccount();
void openBreachLeaderboard();
void drawAuthMenu();
void drawWifiScan();
void drawWifiPass();
void drawMainMenu();
void enterMainMenu();
void drawLeaderboard();
void drawAccountMenu();
void drawSshScreen();
void prepareSshSetupPrompt();
bool connectSshProfile();
void pollSshTerminal();
void closeSshSession();
void drawTelnetBbsScreen();
void prepareTelnetBbsSetup();
bool connectTelnetBbs();
void pollTelnetBbs();
void closeTelnetBbs();
void handleTelnetBbsInput(Keyboard_Class::KeysState status);
void enterTextfilesMode();
void drawTextfilesScreen();
void handleTextfilesInput(Keyboard_Class::KeysState status);
bool updateTextfilesUi();
void enterBluetoothScan();
void drawBluetoothScanScreen();
void handleBluetoothScanInput(Keyboard_Class::KeysState status);
void enterWifiScanNode();
void drawWifiScanNodeScreen();
void handleWifiScanNodeInput(Keyboard_Class::KeysState status);
bool loadWifiCredentialsFromSd();
bool saveWifiCredentialsToSd();
void enterApMode();
void enterApModeLanWeb();
void drawApModeScreen();
void handleApModeInput(Keyboard_Class::KeysState status);
bool updateApModeSourcePromptAnimation();
void serviceApModeWeb();
bool trySshKeyFile(ssh_session session, const String &authSecret, const char* path);
bool authenticateSshSession(ssh_session session, const String &authSecret);
TaskHandle_t getSshTaskHandle();
void snapshotSshState(String &status, bool &connected, bool &shellReady, TaskHandle_t &taskHandle);
bool verifySshKnownHost(ssh_session session);
bool drainSshStream(ssh_session session, ssh_channel channel, char *buffer, bool &pollingEnabled,
                    int isStderr, String &finalStatus, bool &finalFailed);
void discardSshStartupNoise(ssh_channel channel, char *buffer);
void sshWorkerTask(void *pvParameters);
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
void drawGithubQrScreen();
void handleGithubQrInput(Keyboard_Class::KeysState status);
void drawHardwareMenu();
void handleHardwareMenuInput(Keyboard_Class::KeysState status);
void drawFileManager(bool push = true);
void handleFileManagerInput(Keyboard_Class::KeysState status);
void drawFileLoading();
void drawHardwareSettings();
void handleHardwareSettingsInput(Keyboard_Class::KeysState status);
void drawFileActionsMenu();
void handleFileActionsMenuInput(Keyboard_Class::KeysState status);
void drawFileNewTypeMenu();
void handleFileNewTypeMenuInput(Keyboard_Class::KeysState status);
void drawFileRenameInput();
void handleFileRenameInput(Keyboard_Class::KeysState status);
void openTextEditor(String fileName);
void drawFileTextEditor();
void handleFileTextEditorInput(Keyboard_Class::KeysState status);
void drawOtaCatalog();
void enterOtaCatalog();
void handleOtaCatalogInput(Keyboard_Class::KeysState status);
void performOtaUpdate(String binUrl);
void performOtaUpdate(String binUrl, size_t expectedBytes);
void performOtaUpdate(String binUrl, size_t expectedBytes, String firmwareName, String firmwareVersion);
bool fetchOtaCatalog();
String resolveOtaFirmwareUrl(String fid);
bool fetchOtaFirmwareDetails(String fid);
void drawOtaFirmwareDetails();
void startMp3(String fileName);
void drawDirConfirmPopup();
void handleDirConfirmPopupInput(Keyboard_Class::KeysState status);
void drawMusicPlayer();
void handleMusicPlayerInput(Keyboard_Class::KeysState status);
void resetUsbDriveStateForBoot();
void enterUsbDriveMode();
void drawUsbDriveScreen();
void handleUsbDriveInput(Keyboard_Class::KeysState status);
void enterBadUsbMode();
void drawBadUsbScreen();
void handleBadUsbInput(Keyboard_Class::KeysState status);
void enterIrMode();
void drawIrScreen();
void handleIrInput(Keyboard_Class::KeysState status);
bool pollIrReceiver();
bool updateIrUiAnimation();
void enterSstvMode();
void drawSstvScreen();
void handleSstvInput(Keyboard_Class::KeysState status);
bool updateSstvUiAnimation();
void updateMusicInputGate(bool enterDown);
bool pumpMusicPlayback(bool startupBurst);
void clearMusicDurationProbe();
void populatePlaylist();
void playNextTrack();
void playPrevTrack();
void startMp3InPlayer(String fileName);
void toggleMusicPlaybackPause();
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
void drawBatteryPercentBox();
void drawTopStatusIcons(int x, int y);
void drawWheelPositionIndicator(float scroll, int totalItems);
void resetAudioSpectrum();
void feedAudioSpectrumBuffer(const int16_t* samples, size_t sampleCount);
void drawAudioSpectrum(int x, int baselineY, int width, int height);
void handleSplashInput(Keyboard_Class::KeysState status);
void startWifiScan();
void submitScore(int scoreToSubmit);
void handleAuthInput(Keyboard_Class::KeysState status);
void handleWifiScanInput(Keyboard_Class::KeysState status);
void handleWifiPassInput(Keyboard_Class::KeysState status);
void handleMainMenuInput(Keyboard_Class::KeysState status);
void handleSshInput(Keyboard_Class::KeysState status);
void drawRotatedText(String text, int cx, int cy, uint16_t color);
void drawDefaultGlitchText(String text, int x, int y, int size, uint16_t color, bool center = true);
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
int fileNewTypeMenuSelected = 0;
int fileNameInputMode = 0;
String textEditorFileName = "";
String textEditorPath = "";
String textEditorBuffer = "";
String textEditorStatus = "";
int textEditorCursor = 0;
bool textEditorDirty = false;
unsigned long textEditorStatusUntil = 0;
int settingsTab = 0;
int settingsTabScrollOffset = 0;
int settingsFocus = 0;
bool showSystemFiles = false;
bool disableSplash = false;
bool disableBootSound = false;
bool speakerInitSkippedForBootSound = false;
unsigned long lastFileSelectionTime = 0;
int marqueeScrollOffset = 0;
unsigned long lastMarqueeUpdate = 0;
AudioGeneratorMP3 *mp3 = nullptr;
AudioGeneratorWAV *wav = nullptr;
AudioFileSourceSD *fileSD = nullptr;
AudioFileSourceSPIFFS *fileSPIFFS = nullptr;
AudioFileSourceBuffer *audioBuffer = nullptr;
AudioOutputM5Speaker *audioOut = nullptr;
uint16_t audioSpectrumLevels[AUDIO_SPECTRUM_BARS] = {0};
uint32_t audioSpectrumCursor = 0;
unsigned long audioSpectrumLastDecay = 0;
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
uint32_t musicPlaybackDurationMs = 0;
uint32_t musicPlaybackElapsedMs = 0;
bool showImage = false;
String openedImageName = "";
float imageScale = 1.0f;
bool showBootMenu = false;
bool showSplashBootMenu = false;
int splashBootFocus = 0;
float currentSplashBootScroll = 0;
float targetSplashBootScroll = 0;

#if BREACH_USB_MSC_AVAILABLE
USBMSC *usbDriveMsc = nullptr;
#endif
bool usbDriveConfigured = false;
bool usbDriveActive = false;
bool usbDriveSdMounted = false;
uint32_t usbDriveSectorCount = 0;
uint32_t usbDriveSectorSize = 512;
volatile uint32_t usbDriveReadOps = 0;
volatile uint32_t usbDriveWriteOps = 0;
volatile uint32_t usbDriveErrorOps = 0;
volatile bool usbDriveHostEjected = false;
String usbDriveStatus = "IDLE";
#if BREACH_USB_HID_AVAILABLE
USBHIDKeyboard badUsbKeyboard;
#endif
std::vector<String> badUsbScripts;
int badUsbFocus = 0;
int badUsbScrollOffset = 0;
int badUsbMode = 0;
String badUsbSelectedName = "";
String badUsbStatus = "IDLE";
uint32_t badUsbLineNumber = 0;
uint32_t badUsbExecutedLines = 0;
uint32_t badUsbSkippedLines = 0;
uint32_t badUsbDefaultDelayMs = 0;
bool badUsbHidReady = false;
bool badUsbAbortFlag = false;
int irFocus = 0;
int irFileFocus = 0;
int irConfirmFocus = 0;
float currentIrActionScroll = 0;
float targetIrActionScroll = 0;
float currentIrFileScroll = 0;
float targetIrFileScroll = 0;
float currentIrConfirmScroll = 0;
float targetIrConfirmScroll = 0;
bool irReady = false;
bool irPinsSwapped = false;
bool irFileMode = false;
bool irAutoSaveNext = false;
bool irConfirmMode = false;
bool irNameMode = false;
int irNameCursor = 0;
uint8_t irRxPin = G1;
uint8_t irTxPin = G2;
String irStatus = "IDLE";
String irPendingFileName = "";
String irLastProtocolName = "NONE";
decode_type_t irLastProtocol = UNKNOWN;
uint16_t irLastAddress = 0;
uint16_t irLastCommand = 0;
uint16_t irLastBits = 0;
IRDecodedRawDataType irLastRaw = 0;
std::vector<uint16_t> irLastRawMicros;
uint8_t irLastFlags = 0;
bool irHasLastCode = false;
uint32_t irReceiveCount = 0;
uint32_t irOverflowCount = 0;
uint32_t irSendCount = 0;
uint32_t irFileSaveCount = 0;
unsigned long irLastReceiveMs = 0;
unsigned long irLastOverflowMs = 0;
unsigned long irLastActionMs = 0;
std::vector<String> irSavedFiles;
int sstvFocus = 0;
int sstvFileFocus = 0;
int sstvMode = 0;
float currentSstvActionScroll = 0;
float targetSstvActionScroll = 0;
float currentSstvFileScroll = 0;
float targetSstvFileScroll = 0;
bool sstvFileMode = false;
bool sstvTransmitting = false;
String sstvStatus = "IDLE";
std::vector<String> sstvFiles;
std::vector<uint8_t> sstvImage;
uint32_t sstvTransmitCount = 0;
unsigned long sstvLastActionMs = 0;

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
    size_t sizeBytes;
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

