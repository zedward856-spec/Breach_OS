// HARDWARE NODE / SSTV: transmit BMP images from SD or the repo QR code as selectable SSTV audio.

static constexpr int SSTV_ACTION_COUNT = 4;
static constexpr int SSTV_WIDTH = 320;
static constexpr int SSTV_HEIGHT = 256;
static constexpr int SSTV_IMAGE_PIXELS = SSTV_WIDTH * SSTV_HEIGHT;
static constexpr int SSTV_AUDIO_FRAMES = 256;
static constexpr int SSTV_AUDIO_BUFFER_COUNT = 3;
static constexpr uint32_t SSTV_SAMPLE_RATE = 16000;
static constexpr uint8_t SSTV_AUDIO_CHANNEL = 0;
static constexpr uint8_t SSTV_VIS_MARTIN_M1 = 44;
static constexpr uint8_t SSTV_VIS_ROBOT36 = 8;
static constexpr int SSTV_MODE_MARTIN_M1 = 0;
static constexpr int SSTV_MODE_ROBOT36 = 1;
static constexpr int SSTV_ROBOT36_LINES = 240;
static constexpr const char* SSTV_OS_DIR = "/Breach_OS";
static constexpr const char* SSTV_FILE_DIR = "/Breach_OS/sstv";
static const char* SSTV_ACTION_LABELS[SSTV_ACTION_COUNT] = {
    "SD FILE",
    "GITHUB QR",
    "MODE",
    "BACK"
};
static int16_t sstvAudioBuffers[SSTV_AUDIO_BUFFER_COUNT][SSTV_AUDIO_FRAMES * 2];
static int16_t* sstvAudioBuffer = sstvAudioBuffers[0];
static uint8_t sstvAudioBufferIndex = 0;
static float sstvAudioPhase = 0.0f;

static String sstvShort(String value, int maxLen) {
    if ((int)value.length() <= maxLen) return value;
    if (maxLen <= 3) return value.substring(0, maxLen);
    return value.substring(0, maxLen - 3) + "...";
}

static String sstvBaseName(String path) {
    int slash = path.lastIndexOf('/');
    if (slash >= 0) return path.substring(slash + 1);
    return path;
}

static String sstvModeLabel() {
    return sstvMode == SSTV_MODE_ROBOT36 ? "ROBOT36" : "MARTIN M1";
}

static String sstvModeDesc() {
    return sstvMode == SSTV_MODE_ROBOT36 ? "ROBOT36 GRAY" : "MARTIN M1 GRAY";
}

static String sstvActionLabel(int index) {
    if (index == 2) return "MODE " + sstvModeLabel();
    if (index >= 0 && index < SSTV_ACTION_COUNT) return String(SSTV_ACTION_LABELS[index]);
    return "";
}

static void sstvToggleMode() {
    sstvMode = sstvMode == SSTV_MODE_ROBOT36 ? SSTV_MODE_MARTIN_M1 : SSTV_MODE_ROBOT36;
    sstvStatus = "MODE " + sstvModeLabel();
}

static bool sstvLerpScroll(float &current, float target) {
    if (fabs(current - target) <= 0.01) {
        current = target;
        return false;
    }
    current += (target - current) * 0.35;
    if (fabs(current - target) <= 0.01) current = target;
    return true;
}

static int sstvButtonShift(float current, float target) {
    float delta = target - current;
    if (delta > 1.0) delta = 1.0;
    if (delta < -1.0) delta = -1.0;
    return (int)(delta * 12.0);
}

static void sstvDrawListIndicator(float scroll, int total, int x, int y, int w) {
    const int railH = 6;
    const int railR = railH / 2;
    const uint16_t railColor = CP_CYAN;
    const uint16_t knobColor = CP_YELLOW;
    int leftX = x + railR;
    int rightX = x + w - railR;
    int midY = y + railR;

    canvas.drawLine(leftX, y, rightX, y, railColor);
    canvas.drawLine(leftX, y + railH, rightX, y + railH, railColor);
    canvas.drawLine(leftX - 1, y, leftX, y, railColor);
    canvas.drawLine(leftX - 2, y + 1, leftX - 2, y + 1, railColor);
    canvas.drawLine(leftX - railR, midY - 1, leftX - railR, midY + 1, railColor);
    canvas.drawLine(leftX - 2, y + railH - 1, leftX - 2, y + railH - 1, railColor);
    canvas.drawLine(leftX - 1, y + railH, leftX, y + railH, railColor);
    canvas.drawLine(rightX, y, rightX + 1, y, railColor);
    canvas.drawLine(rightX + 2, y + 1, rightX + 2, y + 1, railColor);
    canvas.drawLine(rightX + railR, midY - 1, rightX + railR, midY + 1, railColor);
    canvas.drawLine(rightX + 2, y + railH - 1, rightX + 2, y + railH - 1, railColor);
    canvas.drawLine(rightX, y + railH, rightX + 1, y + railH, railColor);

    if (total <= 1) return;

    float wrapped = fmod(scroll, (float)total);
    if (wrapped < 0.0) wrapped += (float)total;
    float lastIndex = (float)(total - 1);
    float pos = wrapped;
    if (wrapped > lastIndex) pos = lastIndex * ((float)total - wrapped);
    float ratio = pos / lastIndex;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;

    int dotX = leftX + (int)((rightX - leftX) * ratio);
    canvas.fillCircle(dotX, midY, 5, knobColor);
}

bool updateSstvUiAnimation() {
    bool needsRedraw = false;
    needsRedraw |= sstvLerpScroll(currentSstvActionScroll, targetSstvActionScroll);
    needsRedraw |= sstvLerpScroll(currentSstvFileScroll, targetSstvFileScroll);
    return needsRedraw;
}

static bool sstvMountSd() {
    SPI.begin(40, 39, 14, 12);
    if (!SD.begin(12, SPI, 20000000) || SD.cardType() == CARD_NONE) {
        sstvStatus = "SD MOUNT FAIL";
        return false;
    }
    return true;
}

static bool sstvEnsureDir(const char* path) {
    File dir = SD.open(path);
    if (dir) {
        bool ok = dir.isDirectory();
        dir.close();
        return ok;
    }
    return SD.mkdir(path);
}

static bool sstvEnsureFileDir() {
    if (!sstvMountSd()) return false;
    if (!sstvEnsureDir(SSTV_OS_DIR) || !sstvEnsureDir(SSTV_FILE_DIR)) {
        sstvStatus = "SSTV DIR FAIL";
        return false;
    }
    return true;
}

static bool sstvPopulateFiles() {
    sstvFiles.clear();
    if (!sstvEnsureFileDir()) return false;

    File dir = SD.open(SSTV_FILE_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        sstvStatus = "SSTV DIR FAIL";
        return false;
    }

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = sstvBaseName(String(file.name()));
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".bmp")) sstvFiles.push_back(name);
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();

    std::sort(sstvFiles.begin(), sstvFiles.end(), [](const String &a, const String &b) {
        return a < b;
    });
    if (sstvFileFocus >= (int)sstvFiles.size()) sstvFileFocus = (int)sstvFiles.size() - 1;
    if (sstvFileFocus < 0) sstvFileFocus = 0;
    sstvStatus = sstvFiles.empty() ? String("NO BMP FILES") : String("BMP FILES ") + String((uint32_t)sstvFiles.size());
    return true;
}

static uint8_t sstvLuma(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)(((uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11) / 100);
}

static bool sstvLoadBmp(String name) {
    if (!sstvEnsureFileDir()) return false;
    String path = name.startsWith("/") ? name : String(SSTV_FILE_DIR) + "/" + name;
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) {
        sstvStatus = "BMP OPEN FAIL";
        return false;
    }

    auto readLe16 = [&file]() -> uint16_t {
        uint16_t v = 0;
        v |= (uint16_t)(uint8_t)file.read();
        v |= (uint16_t)(uint8_t)file.read() << 8;
        return v;
    };
    auto readLe32 = [&file]() -> uint32_t {
        uint32_t v = 0;
        v |= (uint32_t)(uint8_t)file.read();
        v |= (uint32_t)(uint8_t)file.read() << 8;
        v |= (uint32_t)(uint8_t)file.read() << 16;
        v |= (uint32_t)(uint8_t)file.read() << 24;
        return v;
    };

    if (readLe16() != 0x4D42) {
        file.close();
        sstvStatus = "NOT BMP";
        return false;
    }
    (void)readLe32();
    (void)readLe16();
    (void)readLe16();
    uint32_t dataOffset = readLe32();
    uint32_t dibSize = readLe32();
    if (dibSize < 40) {
        file.close();
        sstvStatus = "BMP DIB BAD";
        return false;
    }

    int32_t srcW = (int32_t)readLe32();
    int32_t srcH = (int32_t)readLe32();
    uint16_t planes = readLe16();
    uint16_t bpp = readLe16();
    uint32_t compression = readLe32();
    if (planes != 1 || srcW <= 0 || srcH == 0 || compression != 0 || (bpp != 16 && bpp != 24 && bpp != 32)) {
        file.close();
        sstvStatus = "BMP 16/24/32";
        return false;
    }

    bool topDown = srcH < 0;
    int32_t absH = topDown ? -srcH : srcH;
    if (srcW > 4096 || absH > 4096) {
        file.close();
        sstvStatus = "BMP TOO LARGE";
        return false;
    }

    uint32_t rowStride = (((uint32_t)bpp * (uint32_t)srcW + 31) / 32) * 4;
    if (rowStride == 0 || rowStride > 32768) {
        file.close();
        sstvStatus = "BMP ROW BAD";
        return false;
    }

    sstvImage.assign(SSTV_IMAGE_PIXELS, 255);
    if ((int)sstvImage.size() != SSTV_IMAGE_PIXELS) {
        file.close();
        sstvStatus = "IMG MEM FAIL";
        return false;
    }

    std::vector<uint8_t> row(rowStride);
    uint8_t bytesPerPixel = bpp / 8;
    for (int y = 0; y < SSTV_HEIGHT; y++) {
        int32_t srcY = ((int32_t)y * absH) / SSTV_HEIGHT;
        int32_t fileY = topDown ? srcY : (absH - 1 - srcY);
        uint32_t rowOffset = dataOffset + (uint32_t)fileY * rowStride;
        if (!file.seek(rowOffset) || file.read(row.data(), rowStride) != (int)rowStride) {
            file.close();
            sstvStatus = "BMP READ FAIL";
            return false;
        }
        for (int x = 0; x < SSTV_WIDTH; x++) {
            int32_t srcX = ((int32_t)x * srcW) / SSTV_WIDTH;
            uint32_t idx = (uint32_t)srcX * bytesPerPixel;
            uint8_t r = 0, g = 0, b = 0;
            if (bpp == 16) {
                uint16_t px = (uint16_t)row[idx] | ((uint16_t)row[idx + 1] << 8);
                r = (uint8_t)((((px >> 11) & 0x1F) * 255) / 31);
                g = (uint8_t)((((px >> 5) & 0x3F) * 255) / 63);
                b = (uint8_t)(((px & 0x1F) * 255) / 31);
            } else {
                b = row[idx];
                g = row[idx + 1];
                r = row[idx + 2];
            }
            sstvImage[y * SSTV_WIDTH + x] = sstvLuma(r, g, b);
        }
    }
    file.close();
    sstvStatus = "BMP READY " + sstvShort(sstvBaseName(path), 12);
    return true;
}

static bool sstvPrepareGithubQr() {
    sstvImage.assign(SSTV_IMAGE_PIXELS, 255);
    if ((int)sstvImage.size() != SSTV_IMAGE_PIXELS) {
        sstvStatus = "IMG MEM FAIL";
        return false;
    }

    constexpr int qrSize = 29;
    constexpr int quiet = 4;
    constexpr int module = 6;
    constexpr int fullModules = qrSize + quiet * 2;
    constexpr int qrPixels = fullModules * module;
    int startX = (SSTV_WIDTH - qrPixels) / 2;
    int startY = (SSTV_HEIGHT - qrPixels) / 2;

    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            if (GITHUB_QR_ROWS[y][x] != '1') continue;
            int px0 = startX + (x + quiet) * module;
            int py0 = startY + (y + quiet) * module;
            for (int yy = 0; yy < module; yy++) {
                int py = py0 + yy;
                if (py < 0 || py >= SSTV_HEIGHT) continue;
                for (int xx = 0; xx < module; xx++) {
                    int px = px0 + xx;
                    if (px >= 0 && px < SSTV_WIDTH) sstvImage[py * SSTV_WIDTH + px] = 0;
                }
            }
        }
    }
    sstvStatus = "QR READY";
    return true;
}

static void sstvPlayAudioFrames(uint32_t frames) {
    if (frames == 0) return;
    feedAudioSpectrumBuffer(sstvAudioBuffer, frames * 2);
    M5Cardputer.Speaker.playRaw(sstvAudioBuffer, frames * 2, SSTV_SAMPLE_RATE, true, 1, SSTV_AUDIO_CHANNEL, false);
    sstvAudioBufferIndex = (sstvAudioBufferIndex + 1) % SSTV_AUDIO_BUFFER_COUNT;
    sstvAudioBuffer = sstvAudioBuffers[sstvAudioBufferIndex];
}

static void sstvWaitForAudioQueueRoom() {
    while (M5Cardputer.Speaker.isPlaying(SSTV_AUDIO_CHANNEL) >= 2) {
        delay(1);
        M5Cardputer.update();
    }
}

static void sstvWaitForAudioDone() {
    while (M5Cardputer.Speaker.isPlaying(SSTV_AUDIO_CHANNEL)) {
        delay(1);
        M5Cardputer.update();
    }
}

static void sstvResetAudioQueue() {
    M5Cardputer.Speaker.stop(SSTV_AUDIO_CHANNEL);
    sstvAudioBufferIndex = 0;
    sstvAudioBuffer = sstvAudioBuffers[0];
    sstvAudioPhase = 0.0f;
}

static float sstvByteFrequency(uint8_t value) {
    return 1500.0f + ((float)value * 800.0f / 255.0f);
}

static void sstvFillTone(float freq, uint32_t frames) {
    sstvWaitForAudioQueueRoom();
    const float twoPi = 6.28318530718f;
    float step = twoPi * freq / (float)SSTV_SAMPLE_RATE;
    for (uint32_t i = 0; i < frames; i++) {
        int16_t sample = (int16_t)(sinf(sstvAudioPhase) * 7200.0f);
        sstvAudioBuffer[i * 2] = sample;
        sstvAudioBuffer[i * 2 + 1] = sample;
        sstvAudioPhase += step;
        if (sstvAudioPhase >= twoPi) sstvAudioPhase -= twoPi;
    }
}

static void sstvPlayToneUs(float freq, uint32_t durationUs) {
    uint32_t framesLeft = ((uint64_t)SSTV_SAMPLE_RATE * durationUs + 500000ULL) / 1000000ULL;
    if (framesLeft == 0) framesLeft = 1;
    while (framesLeft > 0) {
        uint32_t frames = framesLeft > SSTV_AUDIO_FRAMES ? SSTV_AUDIO_FRAMES : framesLeft;
        sstvFillTone(freq, frames);
        sstvPlayAudioFrames(frames);
        framesLeft -= frames;
    }
}

static void sstvPlayPixelScan(int line, uint32_t durationUs) {
    const float twoPi = 6.28318530718f;
    uint32_t totalFrames = ((uint64_t)SSTV_SAMPLE_RATE * durationUs + 500000ULL) / 1000000ULL;
    if (totalFrames == 0) totalFrames = 1;
    uint32_t played = 0;
    while (played < totalFrames) {
        uint32_t frames = (totalFrames - played) > SSTV_AUDIO_FRAMES ? SSTV_AUDIO_FRAMES : (totalFrames - played);
        sstvWaitForAudioQueueRoom();
        for (uint32_t i = 0; i < frames; i++) {
            uint32_t frameIndex = played + i;
            int x = (int)((frameIndex * SSTV_WIDTH) / totalFrames);
            if (x >= SSTV_WIDTH) x = SSTV_WIDTH - 1;
            uint8_t lum = sstvImage[line * SSTV_WIDTH + x];
            float freq = sstvByteFrequency(lum);
            int16_t sample = (int16_t)(sinf(sstvAudioPhase) * 7200.0f);
            sstvAudioBuffer[i * 2] = sample;
            sstvAudioBuffer[i * 2 + 1] = sample;
            sstvAudioPhase += twoPi * freq / (float)SSTV_SAMPLE_RATE;
            if (sstvAudioPhase >= twoPi) sstvAudioPhase -= twoPi;
        }
        sstvPlayAudioFrames(frames);
        played += frames;
    }
}

static void sstvSendVis(uint8_t visCode) {
    sstvPlayToneUs(1900.0f, 300000);
    sstvPlayToneUs(1200.0f, 10000);
    sstvPlayToneUs(1900.0f, 300000);
    sstvPlayToneUs(1200.0f, 30000);

    uint8_t ones = 0;
    for (uint8_t i = 0; i < 7; i++) {
        bool bit = (visCode >> i) & 0x01;
        if (bit) ones++;
        sstvPlayToneUs(bit ? 1100.0f : 1300.0f, 30000);
    }
    bool parity = ones & 0x01;
    sstvPlayToneUs(parity ? 1100.0f : 1300.0f, 30000);
    sstvPlayToneUs(1200.0f, 30000);
}

static bool sstvAbortRequested() {
    M5Cardputer.update();
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    if (status.del) return true;
    for (char c : status.word) {
        if (c == '`') return true;
    }
    return false;
}

static void sstvDrawTxProgress(String label, int line, int totalLines) {
    int progress = totalLines > 0 ? (line * 100) / totalLines : 0;
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(10, 10, 220, 118, CP_YELLOW);
    drawChippedButton(12, 12, 216, 114, CP_DIM);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(String("--- SSTV TX ") + sstvModeLabel() + " ---", 120, 20);
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString(sstvShort(label, 25), 120, 42);
    canvas.drawRect(30, 66, 180, 14, CP_CYAN);
    canvas.fillRect(32, 68, (176 * progress) / 100, 10, CP_YELLOW);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString(String(progress) + "%  LINE " + String(line) + "/" + String(totalLines), 120, 90);
    canvas.drawCenterString("DEL/BACKTICK ABORT", 120, 110);
    pushCanvas();
}

static bool sstvTransmitMartinM1(String label) {
    if ((int)sstvImage.size() != SSTV_IMAGE_PIXELS) {
        sstvStatus = "NO IMAGE READY";
        return false;
    }

    stopMp3Playback();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    sstvTransmitting = true;
    sstvStatus = "TX " + sstvShort(label, 18);
    sstvResetAudioQueue();
    sstvDrawTxProgress(label, 0, SSTV_HEIGHT);

    sstvSendVis(SSTV_VIS_MARTIN_M1);
    for (int y = 0; y < SSTV_HEIGHT; y++) {
        if ((y & 0x07) == 0) sstvDrawTxProgress(label, y, SSTV_HEIGHT);
        sstvPlayToneUs(1200.0f, 4862);
        sstvPlayToneUs(1500.0f, 572);
        sstvPlayPixelScan(y, 146432);
        sstvPlayToneUs(1500.0f, 572);
        sstvPlayPixelScan(y, 146432);
        sstvPlayToneUs(1500.0f, 572);
        sstvPlayPixelScan(y, 146432);
        sstvPlayToneUs(1500.0f, 572);
        if (sstvAbortRequested()) {
            sstvResetAudioQueue();
            sstvStatus = "TX ABORTED";
            sstvTransmitting = false;
            drawSstvScreen();
            return false;
        }
        delay(1);
    }

    sstvWaitForAudioDone();
    sstvTransmitCount++;
    sstvLastActionMs = millis();
    sstvStatus = "TX COMPLETE";
    sstvDrawTxProgress(label, SSTV_HEIGHT, SSTV_HEIGHT);
    delay(900);
    sstvTransmitting = false;
    drawSstvScreen();
    return true;
}

static bool sstvTransmitRobot36(String label) {
    if ((int)sstvImage.size() != SSTV_IMAGE_PIXELS) {
        sstvStatus = "NO IMAGE READY";
        return false;
    }

    stopMp3Playback();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    sstvTransmitting = true;
    sstvStatus = "TX " + sstvShort(label, 18);
    sstvResetAudioQueue();
    sstvDrawTxProgress(label, 0, SSTV_ROBOT36_LINES);

    sstvSendVis(SSTV_VIS_ROBOT36);
    for (int y = 0; y < SSTV_ROBOT36_LINES; y++) {
        if ((y & 0x07) == 0) sstvDrawTxProgress(label, y, SSTV_ROBOT36_LINES);
        int srcY = (y * SSTV_HEIGHT) / SSTV_ROBOT36_LINES;
        if (srcY >= SSTV_HEIGHT) srcY = SSTV_HEIGHT - 1;
        sstvPlayToneUs(1200.0f, 9000);
        sstvPlayToneUs(1500.0f, 3000);
        sstvPlayPixelScan(srcY, 88000);
        sstvPlayToneUs((y & 1) ? 2300.0f : 1500.0f, 4500);
        sstvPlayToneUs(1900.0f, 1500);
        sstvPlayToneUs(sstvByteFrequency(128), 44000);
        if (sstvAbortRequested()) {
            sstvResetAudioQueue();
            sstvStatus = "TX ABORTED";
            sstvTransmitting = false;
            drawSstvScreen();
            return false;
        }
        delay(1);
    }

    sstvWaitForAudioDone();
    sstvTransmitCount++;
    sstvLastActionMs = millis();
    sstvStatus = "TX COMPLETE";
    sstvDrawTxProgress(label, SSTV_ROBOT36_LINES, SSTV_ROBOT36_LINES);
    delay(900);
    sstvTransmitting = false;
    drawSstvScreen();
    return true;
}

static bool sstvTransmitSelected(String label) {
    if (sstvMode == SSTV_MODE_ROBOT36) return sstvTransmitRobot36(label);
    return sstvTransmitMartinM1(label);
}

void enterSstvMode() {
    stopMp3Playback();
    appState = STATE_SSTV;
    sstvFocus = 0;
    sstvFileFocus = 0;
    currentSstvActionScroll = 0;
    targetSstvActionScroll = 0;
    currentSstvFileScroll = 0;
    targetSstvFileScroll = 0;
    sstvFileMode = false;
    sstvTransmitting = false;
    sstvStatus = "SSTV READY";
    drawSstvScreen();
}

void drawSstvScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_YELLOW);
    drawChippedButton(8, 7, 224, 120, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(sstvFileMode ? "--- SSTV SD FILE ---" : "--- SSTV IMAGE TX ---", 120, 12);
    canvas.drawLine(14, 26, 226, 26, CP_YELLOW);

    bool statusBad = sstvStatus.indexOf("FAIL") >= 0 || sstvStatus.startsWith("NO ") || sstvStatus.indexOf("BAD") >= 0;
    canvas.setTextColor(statusBad ? CP_RED : CP_CYAN);
    canvas.drawCenterString(sstvShort(sstvStatus, 27), 120, 31);

    if (sstvFileMode) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("SD /Breach_OS/sstv", 120, 43);
        canvas.drawCenterString(String("BMP -> ") + sstvModeLabel(), 120, 55);

        int buttonW = 195;
        int buttonX = (240 - buttonW) / 2;
        int buttonY = 74;
        int buttonH = 30;
        int buttonShift = sstvFiles.empty() ? 0 : sstvButtonShift(currentSstvFileScroll, targetSstvFileScroll);
        if (sstvFiles.empty()) {
            drawChippedButton(buttonX, buttonY, buttonW, buttonH, CP_RED);
            canvas.setTextColor(CP_RED);
            canvas.setTextSize(2);
            canvas.drawCenterString("NO BMP FILES", 120, buttonY + 8);
            canvas.setTextSize(1);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString("PUT .BMP IN FOLDER", 120, 108);
        } else {
            drawChippedButton(buttonX + buttonShift, buttonY, buttonW, buttonH, CP_YELLOW);
            canvas.setTextColor(CP_YELLOW);
            canvas.setTextSize(1);
            canvas.drawCenterString(sstvShort(sstvFiles[sstvFileFocus], 30), 120 + buttonShift, buttonY + 11);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString(String(sstvFileFocus + 1) + "/" + String((uint32_t)sstvFiles.size()), 120, 108);
        }
        sstvDrawListIndicator(sstvFiles.empty() ? 0.0f : currentSstvFileScroll, sstvFiles.empty() ? 1 : (int)sstvFiles.size(), 45, 114, 150);
        pushCanvas();
        return;
    }

    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString(String("MODE ") + sstvModeDesc(), 120, 43);
    canvas.drawCenterString("PHONE/RADIO SSTV RX", 120, 55);
    canvas.setCursor(22, 67);
    canvas.print("TX COUNT " + String((uint32_t)sstvTransmitCount));

    int buttonW = 195;
    int buttonX = (240 - buttonW) / 2;
    int buttonY = 78;
    int buttonH = 30;
    int buttonShift = sstvButtonShift(currentSstvActionScroll, targetSstvActionScroll);
    drawChippedButton(buttonX + buttonShift, buttonY, buttonW, buttonH, CP_YELLOW);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(2);
    canvas.drawCenterString(sstvActionLabel(sstvFocus), 120 + buttonShift, buttonY + 8);

    sstvDrawListIndicator(currentSstvActionScroll, SSTV_ACTION_COUNT, 45, 114, 150);
    pushCanvas();
}

void handleSstvInput(Keyboard_Class::KeysState status) {
    if (sstvTransmitting) return;

    bool hasPrev = false, hasNext = false, hasBack = status.del;
    for (char c : status.word) {
        if (c == ',' || c == ';') hasPrev = true;
        if (c == '/' || c == '.') hasNext = true;
        if (c == '`') hasBack = true;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (sstvFileMode) {
            sstvFileMode = false;
            sstvStatus = "SSTV READY";
            drawSstvScreen();
            return;
        }
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 5;
        currentHardwareScroll = 5;
        targetHardwareScroll = 5;
        drawHardwareMenu();
        return;
    }

    if (hasPrev) {
        playSound(sound_hover, sound_hover_size);
        if (sstvFileMode) {
            if (!sstvFiles.empty()) {
                sstvFileFocus--;
                if (sstvFileFocus < 0) sstvFileFocus = (int)sstvFiles.size() - 1;
                targetSstvFileScroll -= 1.0;
            }
        } else {
            sstvFocus--;
            if (sstvFocus < 0) sstvFocus = SSTV_ACTION_COUNT - 1;
            targetSstvActionScroll -= 1.0;
        }
        drawSstvScreen();
    }

    if (hasNext) {
        playSound(sound_hover, sound_hover_size);
        if (sstvFileMode) {
            if (!sstvFiles.empty()) {
                sstvFileFocus++;
                if (sstvFileFocus >= (int)sstvFiles.size()) sstvFileFocus = 0;
                targetSstvFileScroll += 1.0;
            }
        } else {
            sstvFocus++;
            if (sstvFocus >= SSTV_ACTION_COUNT) sstvFocus = 0;
            targetSstvActionScroll += 1.0;
        }
        drawSstvScreen();
    }

    if (!status.enter) return;
    playSound(sound_select, sound_select_size);

    if (sstvFileMode) {
        if (sstvFiles.empty()) {
            sstvPopulateFiles();
            drawSstvScreen();
            return;
        }
        String fileName = sstvFiles[sstvFileFocus];
        if (sstvLoadBmp(fileName)) {
            sstvTransmitSelected(fileName);
        } else {
            drawSstvScreen();
        }
        return;
    }

    if (sstvFocus == 0) {
        sstvFileMode = true;
        sstvFileFocus = 0;
        sstvPopulateFiles();
        currentSstvFileScroll = sstvFileFocus;
        targetSstvFileScroll = sstvFileFocus;
        drawSstvScreen();
    } else if (sstvFocus == 1) {
        if (sstvPrepareGithubQr()) {
            sstvTransmitSelected("GITHUB QR");
        } else {
            drawSstvScreen();
        }
    } else if (sstvFocus == 2) {
        sstvToggleMode();
        drawSstvScreen();
    } else if (sstvFocus == 3) {
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 5;
        currentHardwareScroll = 5;
        targetHardwareScroll = 5;
        drawHardwareMenu();
    }
}
