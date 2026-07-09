// Global TAB screenshot capture: save the current 240x135 canvas to SD as a BMP.

static constexpr int SCREENSHOT_W = 240;
static constexpr int SCREENSHOT_H = 135;
static constexpr const char* SCREENSHOT_OS_DIR = "/Breach_OS";
static constexpr const char* SCREENSHOT_DIR = "/Breach_OS/screenshots";

static bool screenshotEnsureSd() {
    SPI.begin(40, 39, 14, 12);
    return SD.begin(12, SPI, 20000000) && SD.cardType() != CARD_NONE;
}

static bool screenshotEnsureDir(const char* path) {
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

static void screenshotWriteLe16(File &file, uint16_t value) {
    file.write((uint8_t)(value & 0xFF));
    file.write((uint8_t)((value >> 8) & 0xFF));
}

static void screenshotWriteLe32(File &file, uint32_t value) {
    file.write((uint8_t)(value & 0xFF));
    file.write((uint8_t)((value >> 8) & 0xFF));
    file.write((uint8_t)((value >> 16) & 0xFF));
    file.write((uint8_t)((value >> 24) & 0xFF));
}

static String screenshotFileName() {
    return String("SCREEN_") + String(millis()) + ".bmp";
}

static bool saveCanvasScreenshotToSd(String &fileName, String &savePath) {
    fileName = screenshotFileName();
    savePath = String(SCREENSHOT_DIR) + "/" + fileName;

    if (!screenshotEnsureSd()) {
        savePath = "SD MOUNT FAIL";
        return false;
    }
    if (!screenshotEnsureDir(SCREENSHOT_OS_DIR) || !screenshotEnsureDir(SCREENSHOT_DIR)) {
        savePath = "DIR CREATE FAIL";
        return false;
    }

    File file = SD.open(savePath, FILE_WRITE);
    if (!file) {
        savePath = "FILE OPEN FAIL";
        return false;
    }

    const uint32_t rowBytes = ((SCREENSHOT_W * 3 + 3) / 4) * 4;
    const uint32_t pixelBytes = rowBytes * SCREENSHOT_H;
    const uint32_t fileBytes = 54 + pixelBytes;

    file.write((const uint8_t*)"BM", 2);
    screenshotWriteLe32(file, fileBytes);
    screenshotWriteLe16(file, 0);
    screenshotWriteLe16(file, 0);
    screenshotWriteLe32(file, 54);

    screenshotWriteLe32(file, 40);
    screenshotWriteLe32(file, SCREENSHOT_W);
    screenshotWriteLe32(file, SCREENSHOT_H);
    screenshotWriteLe16(file, 1);
    screenshotWriteLe16(file, 24);
    screenshotWriteLe32(file, 0);
    screenshotWriteLe32(file, pixelBytes);
    screenshotWriteLe32(file, 2835);
    screenshotWriteLe32(file, 2835);
    screenshotWriteLe32(file, 0);
    screenshotWriteLe32(file, 0);

    uint8_t row[rowBytes];
    for (int y = SCREENSHOT_H - 1; y >= 0; y--) {
        memset(row, 0, sizeof(row));
        for (int x = 0; x < SCREENSHOT_W; x++) {
            uint16_t pixel = (uint16_t)canvas.readPixel(x, y);
            uint8_t r = (uint8_t)((((pixel >> 11) & 0x1F) * 255) / 31);
            uint8_t g = (uint8_t)((((pixel >> 5) & 0x3F) * 255) / 63);
            uint8_t b = (uint8_t)(((pixel & 0x1F) * 255) / 31);
            int idx = x * 3;
            row[idx] = b;
            row[idx + 1] = g;
            row[idx + 2] = r;
        }
        if (file.write(row, rowBytes) != rowBytes) {
            file.close();
            SD.remove(savePath);
            savePath = "WRITE FAIL";
            return false;
        }
    }

    file.close();
    return true;
}

static void showScreenshotResult(bool ok, String fileName, String detail) {
    showScreenshotPopup = true;
    screenshotPopupUntil = millis() + 2400;
    if (ok) {
        screenshotPopupLine1 = "SCREEN SHOT SAVED";
        screenshotPopupLine2 = "SAVED TO /Breach_OS/screenshots";
        screenshotPopupLine3 = fileName;
    } else {
        screenshotPopupLine1 = "SCREEN SHOT FAILED";
        screenshotPopupLine2 = detail;
        screenshotPopupLine3 = "TAB RETRY";
    }
}

bool handleScreenshotShortcut(Keyboard_Class::KeysState &status) {
    bool hasTab = status.tab;
    std::vector<char> kept;
    kept.reserve(status.word.size());
    for (char c : status.word) {
        if (c == '\t' || c == 0x09) hasTab = true;
        else kept.push_back(c);
    }
    if (!hasTab) return false;
    status.tab = false;
    status.word = kept;

    String fileName;
    String savePath;
    bool ok = saveCanvasScreenshotToSd(fileName, savePath);
    showScreenshotResult(ok, fileName, ok ? savePath : savePath);
    playSound(ok ? sound_success : sound_fail, ok ? sound_success_size : sound_fail_size);
    drawCurrentScreen();
    return true;
}

void drawScreenshotSavedPopup() {
    int x = 18;
    int y = 45;
    int w = 204;
    int h = 52;
    uint16_t color = screenshotPopupLine1.indexOf("FAILED") >= 0 ? CP_RED : CP_CYAN;
    canvas.fillRect(x + 1, y + 1, w - 2, h - 2, CP_BG);
    drawChippedButton(x, y, w, h, color);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(screenshotPopupLine1, 120, y + 8);
    canvas.setTextColor(color);
    canvas.drawCenterString(screenshotPopupLine2, 120, y + 24);
    canvas.setTextColor(WHITE);
    canvas.drawCenterString(screenshotPopupLine3, 120, y + 38);
}
