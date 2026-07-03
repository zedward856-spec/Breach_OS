// Core helpers and shared UI drawing routines.

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

void playSound(const unsigned char* soundData, size_t soundSize) {
    if (soundData != nullptr && soundSize > 0) {
        M5Cardputer.Speaker.playWav(soundData, soundSize);
    }
}

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

void drawMessage(String msg) {
    drawMessage(msg, "");
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

uint16_t audioSpectrumLevelFromAmp(uint32_t amp) {
    uint32_t level = amp / 96;
    if (level > 255) level = 255;
    return (uint16_t)level;
}

void resetAudioSpectrum() {
    for (int i = 0; i < AUDIO_SPECTRUM_BARS; i++) audioSpectrumLevels[i] = 0;
    audioSpectrumCursor = 0;
    audioSpectrumLastDecay = millis();
}

void feedAudioSpectrumSample(int16_t left, int16_t right) {
    int32_t l = left;
    int32_t r = right;
    if (l < 0) l = -l;
    if (r < 0) r = -r;
    if (l > 32767) l = 32767;
    if (r > 32767) r = 32767;
    uint16_t level = audioSpectrumLevelFromAmp(((uint32_t)l + (uint32_t)r) / 2);
    int bin = audioSpectrumCursor++ % AUDIO_SPECTRUM_BARS;
    if (level > audioSpectrumLevels[bin]) audioSpectrumLevels[bin] = level;
}

void feedAudioSpectrumBuffer(const int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) return;
    for (int i = 0; i < AUDIO_SPECTRUM_BARS; i++) {
        size_t start = sampleCount * i / AUDIO_SPECTRUM_BARS;
        size_t end = sampleCount * (i + 1) / AUDIO_SPECTRUM_BARS;
        if (end <= start) end = start + 1;
        if (end > sampleCount) end = sampleCount;

        uint32_t avg = 0;
        uint32_t peak = 0;
        for (size_t j = start; j < end; j++) {
            int32_t v = samples[j];
            if (v < 0) v = -v;
            if (v > 32767) v = 32767;
            avg += (uint32_t)v;
            if ((uint32_t)v > peak) peak = (uint32_t)v;
        }
        size_t count = end - start;
        if (count > 0) avg /= count;
        uint32_t mixed = avg + (peak / 2);
        uint16_t level = audioSpectrumLevelFromAmp(mixed);
        if (level > audioSpectrumLevels[i]) audioSpectrumLevels[i] = level;
    }
}

void drawAudioSpectrum(int x, int baselineY, int width, int height) {
    unsigned long now = millis();
    if (audioSpectrumLastDecay == 0) audioSpectrumLastDecay = now;
    while (now - audioSpectrumLastDecay >= 35) {
        for (int i = 0; i < AUDIO_SPECTRUM_BARS; i++) {
            audioSpectrumLevels[i] = (audioSpectrumLevels[i] * 13) / 16;
        }
        audioSpectrumLastDecay += 35;
    }

    int gap = 2;
    int barW = (width - (AUDIO_SPECTRUM_BARS - 1) * gap) / AUDIO_SPECTRUM_BARS;
    if (barW < 2) barW = 2;
    canvas.drawFastHLine(x, baselineY, width, CP_DIM);
    for (int i = 0; i < AUDIO_SPECTRUM_BARS; i++) {
        int h = 2 + (audioSpectrumLevels[i] * (height - 2)) / 255;
        if (h < 2) h = 2;
        if (h > height) h = height;
        int bx = x + i * (barW + gap);
        uint16_t color = h > (height * 2 / 3) ? CP_RED : (h > (height / 3) ? CP_YELLOW : CP_CYAN);
        canvas.fillRect(bx, baselineY - h, barW, h, color);
        canvas.drawRect(bx, baselineY - h, barW, h, CP_DIM);
    }
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
    if (!suppressBatteryPercentBox && appState != STATE_PLAYING) {
        drawBatteryPercentBox();
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
        case STATE_SSH: drawSshScreen(); break;
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
        case STATE_OTA_CATALOG: drawOtaCatalog(); break;
        case STATE_DIR_CONFIRM_POPUP: drawDirConfirmPopup(); break;
        case STATE_MUSIC_PLAYER: drawMusicPlayer(); break;
    }
}

void drawProgressBar(int progress, String statusText, uint16_t color) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw Cyberpunk framed container with chipped bottom-right corners.
    drawChippedButton(20, 20, 200, 95, color);
    drawChippedButton(22, 22, 196, 91, CP_DIM);
    
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

void appendSshTerminal(String text) {
    sshTerminalLog += text;
    if (sshTerminalLog.length() > 900) {
        sshTerminalLog.remove(0, sshTerminalLog.length() - 900);
    }
    sshTerminalDirty = true;
}

bool initSshMutex() {
    if (sshMutex != NULL) return true;
    sshMutex = xSemaphoreCreateMutex();
    return sshMutex != NULL;
}

bool lockSshState(uint32_t waitMs = 50) {
    return initSshMutex() && xSemaphoreTake(sshMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void unlockSshState() {
    if (sshMutex != NULL) xSemaphoreGive(sshMutex);
}

void queueSshOutput(const String &text) {
    if (text == "") return;
    if (!lockSshState()) return;
    sshQueuedOutput += text;
    if (sshQueuedOutput.length() > SSH_MAX_QUEUE_BYTES) {
        sshQueuedOutput.remove(0, sshQueuedOutput.length() - SSH_MAX_QUEUE_BYTES);
    }
    sshTerminalDirty = true;
    unlockSshState();
}

String takeSshOutput() {
    String output = "";
    if (!lockSshState()) return output;
    output = sshQueuedOutput;
    sshQueuedOutput = "";
    unlockSshState();
    return output;
}

bool isSshOutputBackedUp() {
    bool backedUp = false;
    if (!lockSshState()) return true;
    backedUp = sshQueuedOutput.length() >= SSH_MAX_QUEUE_BYTES;
    unlockSshState();
    return backedUp;
}

void queueSshCommand(String command) {
    if (command == "") return;
    if (!lockSshState()) return;
    sshQueuedCommand += command;
    if (sshQueuedCommand.length() > SSH_MAX_QUEUE_BYTES) {
        sshQueuedCommand.remove(0, sshQueuedCommand.length() - SSH_MAX_QUEUE_BYTES);
    }
    unlockSshState();
}

String takeSshCommand() {
    String command = "";
    if (!lockSshState()) return command;
    command = sshQueuedCommand;
    sshQueuedCommand = "";
    unlockSshState();
    return command;
}

void setSshStatus(String status, bool connected, bool shellReady) {
    if (!lockSshState()) return;
    sshStatus = status;
    sshConnected = connected;
    sshShellReady = shellReady;
    sshTerminalDirty = true;
    unlockSshState();
}

bool isSshStopRequested() {
    bool stopRequested = false;
    if (!lockSshState()) return true;
    stopRequested = sshStopRequested;
    unlockSshState();
    return stopRequested;
}

TaskHandle_t getSshTaskHandle() {
    TaskHandle_t handle = NULL;
    if (!lockSshState()) return NULL;
    handle = sshTaskHandle;
    unlockSshState();
    return handle;
}

void snapshotSshState(String &status, bool &connected, bool &shellReady, TaskHandle_t &taskHandle) {
    if (!lockSshState()) return;
    status = sshStatus;
    connected = sshConnected;
    shellReady = sshShellReady;
    taskHandle = sshTaskHandle;
    unlockSshState();
}

void closeSshSession() {
    if (lockSshState()) {
        sshStopRequested = true;
        unlockSshState();
    }

    unsigned long start = millis();
    while (getSshTaskHandle() != NULL && millis() - start < 1200) delay(20);

    if (getSshTaskHandle() == NULL && lockSshState()) {
        sshConnected = false;
        sshShellReady = false;
        sshQueuedCommand = "";
        sshWorkerPass = "";
        unlockSshState();
    } else if (getSshTaskHandle() != NULL) {
        setSshStatus("CLOSING", false, false);
    }
}

void pollSshTerminal() {
    if (!sshTerminalMode) return;
    String output = takeSshOutput();
    if (output != "") appendSshTerminal(output);
}

void drawSshTerminal() {
    pollSshTerminal();
    String statusSnapshot = "";
    bool connectedSnapshot = false;
    bool shellReadySnapshot = false;
    TaskHandle_t taskSnapshot = NULL;
    snapshotSshState(statusSnapshot, connectedSnapshot, shellReadySnapshot, taskSnapshot);

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("--- SSH TERMINAL ---", 120, 10);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);

    String title = sshTarget == "" ? sshHost : sshTarget;
    if (sshPort != "" && sshPort != "22") title += ":" + sshPort;
    if (title.length() > 18) title = title.substring(0, 17) + "~";
    canvas.setTextColor(connectedSnapshot ? CP_GREEN : CP_RED);
    canvas.setCursor(12, 28);
    canvas.print(connectedSnapshot ? "CONNECTED" : "OFFLINE");
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(82, 28);
    canvas.print(title);

    String view = sshTerminalLog;
    int first = 0;
    int lineCount = 0;
    for (int i = view.length() - 1; i >= 0; i--) {
        if (view.charAt(i) == '\n') {
            lineCount++;
            if (lineCount > 6) {
                first = i + 1;
                break;
            }
        }
    }

    canvas.setTextColor(WHITE);
    int y = 42;
    int start = first;
    int rows = 0;
    while (start < view.length() && rows < 6) {
        int end = view.indexOf('\n', start);
        if (end < 0) end = view.length();
        String line = view.substring(start, end);
        if (line.length() > 34) line = line.substring(line.length() - 34);
        canvas.setCursor(12, y);
        canvas.print(line);
        y += 11;
        rows++;
        start = end + 1;
    }

    String prompt = "> " + sshInputLine + (blinkState ? "_" : "");
    if (prompt.length() > 34) prompt = prompt.substring(prompt.length() - 34);
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(12, 112);
    canvas.print(prompt);
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(150, 112);
    canvas.print("ESC:SETUP");

    pushCanvas();
}

void drawSshScreen() {
    if (sshTerminalMode) {
        drawSshTerminal();
        return;
    }
    String statusSnapshot = "";
    bool connectedSnapshot = false;
    bool shellReadySnapshot = false;
    TaskHandle_t taskSnapshot = NULL;
    snapshotSshState(statusSnapshot, connectedSnapshot, shellReadySnapshot, taskSnapshot);

    canvas.startWrite();
    canvas.fillScreen(CP_BG);

    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- SSH QUICK CONNECT ---", 120, 10);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);

    bool wifiOnline = WiFi.status() == WL_CONNECTED;
    canvas.setTextColor(wifiOnline ? CP_GREEN : CP_RED);
    canvas.setCursor(12, 27);
    canvas.print(wifiOnline ? "LINK: ONLINE" : "LINK: OFFLINE");

    String statusText = statusSnapshot == "" ? String("READY") : statusSnapshot;
    if (statusText.length() > 13) statusText = statusText.substring(0, 12) + "~";
    canvas.setTextColor(connectedSnapshot ? CP_GREEN : CP_DIM);
    canvas.setCursor(120, 27);
    canvas.print("SSH:" + statusText);

    String targetText = sshTarget == "" ? String("sl01220@raspi") : sshTarget;
    String authText = sshPass == "" ? String("") : String("********");
    if (targetText.length() > 24) targetText = targetText.substring(0, 23) + "~";
    if (authText.length() > 24) authText = authText.substring(0, 23) + "~";

    String values[2] = {targetText, authText};
    String labels[2] = {"TARGET", "PASS"};
    for (int i = 0; i < 2; i++) {
        int y = 45 + i * 20;
        uint16_t color = (sshFocus == i) ? CP_YELLOW : WHITE;
        canvas.setTextColor(color);
        drawChippedButton(10, y - 2, 220, 16, color);
        canvas.setCursor(16, y + 1);
        canvas.print(labels[i] + ": " + values[i] + ((sshFocus == i && blinkState) ? "_" : ""));
    }

    uint16_t saveColor = (sshFocus == 2) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 106, 100, 20, saveColor);
    canvas.setTextColor(saveColor);
    drawGlitchText("CONNECT", 60, 111, 1, saveColor);

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(122, 111);
    canvas.print("ENTER:NEXT  ESC:BACK");

    pushCanvas();
}

bool trySshKeyFile(ssh_session session, const String &authSecret, const char* path) {
    ssh_key key = NULL;
    const char* passphrase = authSecret == "" ? NULL : authSecret.c_str();
    int rc = ssh_pki_import_privkey_file(path, passphrase, NULL, NULL, &key);
    if (rc != SSH_OK || key == NULL) return false;
    queueSshOutput(String("[KEY] ") + path + "\n");
    rc = ssh_userauth_publickey(session, NULL, key);
    ssh_key_free(key);
    return rc == SSH_AUTH_SUCCESS;
}

bool verifySshKnownHost(ssh_session session) {
    enum ssh_known_hosts_e state = ssh_session_is_known_server(session);
    if (state == SSH_KNOWN_HOSTS_OK) return true;
    if (state == SSH_KNOWN_HOSTS_NOT_FOUND || state == SSH_KNOWN_HOSTS_UNKNOWN) {
        if (ssh_session_update_known_hosts(session) == SSH_OK) {
            queueSshOutput("[HOSTKEY] trusted\n");
            return true;
        }
        queueSshOutput("[HOSTKEY] store failed\n");
        return false;
    }
    if (state == SSH_KNOWN_HOSTS_CHANGED || state == SSH_KNOWN_HOSTS_OTHER) {
        queueSshOutput("[HOSTKEY] changed\n");
        return false;
    }
    queueSshOutput("[HOSTKEY] check failed\n");
    return false;
}

bool authenticateSshSession(ssh_session session, const String &authSecret) {
    if (authSecret != "") {
        queueSshOutput("[AUTH] password try\n");
        int rc = ssh_userauth_password(session, NULL, authSecret.c_str());
        if (rc == SSH_AUTH_SUCCESS) {
            queueSshOutput("[AUTH] password\n");
            return true;
        }
        queueSshOutput("[AUTH] password denied\n");

        rc = ssh_userauth_kbdint(session, NULL, NULL);
        while (rc == SSH_AUTH_INFO && !isSshStopRequested()) {
            int prompts = ssh_userauth_kbdint_getnprompts(session);
            queueSshOutput("[AUTH] keyboard try\n");
            for (int i = 0; i < prompts; i++) {
                ssh_userauth_kbdint_setanswer(session, i, authSecret.c_str());
            }
            rc = ssh_userauth_kbdint(session, NULL, NULL);
        }
        if (rc == SSH_AUTH_SUCCESS) {
            queueSshOutput("[AUTH] keyboard\n");
            return true;
        }
        queueSshOutput("[AUTH] keyboard denied\n");
    }

    int rc = ssh_userauth_none(session, NULL);
    if (rc == SSH_AUTH_SUCCESS) {
        queueSshOutput("[AUTH] none\n");
        return true;
    }

    int method = ssh_userauth_list(session, NULL);
    String methods = "[AUTH] server:";
    if (method & SSH_AUTH_METHOD_PASSWORD) methods += " password";
    if (method & SSH_AUTH_METHOD_INTERACTIVE) methods += " keyboard";
    if (method & SSH_AUTH_METHOD_PUBLICKEY) methods += " publickey";
    if (method == 0) methods += " unknown";
    queueSshOutput(methods + "\n");

    if (method & SSH_AUTH_METHOD_PUBLICKEY) {
        rc = ssh_userauth_publickey_auto(session, NULL, authSecret == "" ? NULL : authSecret.c_str());
        if (rc == SSH_AUTH_SUCCESS) {
            queueSshOutput("[AUTH] auto key\n");
            return true;
        }
        SPI.begin(40, 39, 14, 12);
        SD.begin(12, SPI, 20000000);
        const char* keyPaths[] = {
            "/sd/Breach_OS/ssh/id_ed25519",
            "/sd/Breach_OS/ssh/id_rsa",
            "/spiffs/ssh/id_ed25519",
            "/spiffs/ssh/id_rsa"
        };
        for (int i = 0; i < 4; i++) {
            if (trySshKeyFile(session, authSecret, keyPaths[i])) {
                queueSshOutput("[AUTH] key accepted\n");
                return true;
            }
        }
        queueSshOutput("[AUTH] no key accepted\n");
    }
    return false;
}

bool drainSshStream(ssh_session session, ssh_channel channel, char *buffer, bool &pollingEnabled,
                    int isStderr, String &finalStatus, bool &finalFailed) {
    if (!pollingEnabled) return true;
    int available = ssh_channel_poll(channel, isStderr);
    if (available == SSH_ERROR) {
        const char* sshError = ssh_get_error(session);
        bool channelEnded = ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel);
        if (!channelEnded && (sshError == NULL || sshError[0] == '\0')) {
            pollingEnabled = false;
            return true;
        }
        finalStatus = "POLL FAIL";
        finalFailed = true;
        queueSshOutput(String("[ERR] poll failed: ") + (sshError ? sshError : "") + "\n");
        return false;
    }
    if (available <= 0) return true;

    int readSize = available < (int)(SSH_IO_BUFFER_SIZE - 1) ? available : (int)(SSH_IO_BUFFER_SIZE - 1);
    int n = ssh_channel_read(channel, buffer, readSize, isStderr);
    if (n > 0) {
        String chunk = "";
        chunk.reserve(n);
        for (int i = 0; i < n; i++) {
            char c = buffer[i];
            if (c == '\r') continue;
            if (c == '\n' || (c >= 32 && c <= 126)) chunk += c;
        }
        if (chunk != "") queueSshOutput(chunk);
        return true;
    }
    if (n == SSH_AGAIN) return true;
    if (n == 0) {
        if (ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel)) {
            finalStatus = "SSH CLOSED";
            return false;
        }
        return true;
    }
    finalStatus = "READ FAIL";
    finalFailed = true;
    queueSshOutput("[ERR] SSH read failed\n");
    return false;
}

void sshWorkerTask(void *pvParameters) {
    char *buffer = (char *)malloc(SSH_IO_BUFFER_SIZE);
    ssh_session session = NULL;
    ssh_channel channel = NULL;
    bool stdoutPollingEnabled = true;
    bool stderrPollingEnabled = true;
    String finalStatus = "SSH CLOSED";
    bool finalFailed = false;
    String host = "";
    String user = "";
    String authSecret = "";
    int port = 22;
    long timeoutSec = 8;
    const char* knownHostsPath = "/spiffs/ssh_known_hosts";
    const char* globalKnownHostsPath = "/spiffs/ssh_global_known_hosts";

    if (lockSshState(250)) {
        host = sshWorkerHost;
        user = sshWorkerUser;
        authSecret = sshWorkerPass;
        sshWorkerPass = "";
        port = sshWorkerPort;
        unlockSshState();
    } else {
        finalStatus = "LOCK FAIL";
        finalFailed = true;
        queueSshOutput("[ERR] SSH state lock failed\n");
        goto SSH_EXIT;
    }

    if (buffer == NULL) {
        finalStatus = "BUFFER FAIL";
        finalFailed = true;
        queueSshOutput("[ERR] SSH buffer allocation failed\n");
        goto SSH_EXIT;
    }

    setSshStatus("CONNECTING", false, false);
    queueSshOutput("[BRUCE] SSH worker task started\n");
    Serial.printf("[SSHDBG] task start freeHeap=%u\n", (unsigned)ESP.getFreeHeap());

    session = ssh_new();
    if (session == NULL) {
        finalStatus = "SSH INIT FAIL";
        finalFailed = true;
        queueSshOutput("[ERR] ssh_new failed\n");
        goto SSH_EXIT;
    }

    ssh_options_set(session, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, user.c_str());
    ssh_options_set(session, SSH_OPTIONS_KNOWNHOSTS, knownHostsPath);
    ssh_options_set(session, SSH_OPTIONS_GLOBAL_KNOWNHOSTS, globalKnownHostsPath);
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeoutSec);

    if (WiFi.status() != WL_CONNECTED) {
        finalStatus = "WIFI OFFLINE";
        finalFailed = true;
        queueSshOutput("[ERR] WiFi disconnected before SSH connect\n");
        goto SSH_EXIT;
    }

    queueSshOutput("[SSH] connect start\n");
    Serial.printf("[SSHDBG] connect start\n");
    if (ssh_connect(session) != SSH_OK) {
        finalStatus = "CONNECT FAIL";
        finalFailed = true;
        queueSshOutput(String("[ERR] ") + ssh_get_error(session) + "\n");
        goto SSH_EXIT;
    }

    queueSshOutput("[SSH] connect ok\n");
    Serial.printf("[SSHDBG] connect ok\n");
    if (!verifySshKnownHost(session)) {
        finalStatus = "HOSTKEY FAIL";
        finalFailed = true;
        goto SSH_EXIT;
    }

    setSshStatus("AUTH", false, false);
    if (!authenticateSshSession(session, authSecret)) {
        finalStatus = authSecret == "" ? String("AUTH NEEDED") : String("AUTH FAIL");
        finalFailed = true;
        queueSshOutput(authSecret == "" ? String("[AUTH] enter PASS or add /sd/Breach_OS/ssh key\n") : String("[AUTH] failed\n"));
        goto SSH_EXIT;
    }

    queueSshOutput("[SSH] auth ok\n");
    authSecret = "";
    Serial.printf("[SSHDBG] auth ok\n");
    setSshStatus("CHANNEL", false, false);
    channel = ssh_channel_new(session);
    if (channel == NULL || ssh_channel_open_session(channel) != SSH_OK) {
        finalStatus = "CHANNEL FAIL";
        finalFailed = true;
        queueSshOutput("[ERR] channel open failed\n");
        goto SSH_EXIT;
    }

    if (ssh_channel_request_pty_size(channel, "vt100", 40, 12) != SSH_OK) {
        finalStatus = "PTY FAIL";
        finalFailed = true;
        queueSshOutput("[ERR] PTY request failed\n");
        goto SSH_EXIT;
    }

    if (ssh_channel_request_shell(channel) != SSH_OK) {
        finalStatus = "SHELL FAIL";
        finalFailed = true;
        queueSshOutput("[ERR] shell request failed\n");
        goto SSH_EXIT;
    }

    setSshStatus("SHELL READY", true, true);
    queueSshOutput("[SHELL READY]\n");
    Serial.printf("[SSHDBG] shell ready freeHeap=%u\n", (unsigned)ESP.getFreeHeap());

    while (!isSshStopRequested()) {
        if (WiFi.status() != WL_CONNECTED) {
            finalStatus = "WIFI DROPPED";
            finalFailed = true;
            queueSshOutput("\n[ERR] WiFi disconnected\n");
            goto SSH_EXIT;
        }

        if (channel == NULL || !ssh_channel_is_open(channel) ||
            ssh_channel_is_closed(channel) || ssh_channel_is_eof(channel)) {
            finalStatus = "SSH CLOSED";
            queueSshOutput("\n[SSH] channel closed\n");
            goto SSH_EXIT;
        }

        String outbound = takeSshCommand();
        if (outbound != "") {
            int written = ssh_channel_write(channel, outbound.c_str(), outbound.length());
            if (written == SSH_AGAIN) {
                queueSshCommand(outbound);
            } else if (written == SSH_ERROR) {
                finalStatus = "WRITE FAIL";
                finalFailed = true;
                queueSshOutput("[ERR] SSH write failed\n");
                goto SSH_EXIT;
            } else if (written > 0 && written < outbound.length()) {
                queueSshCommand(outbound.substring(written));
            }
        }

        if (isSshOutputBackedUp()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!drainSshStream(session, channel, buffer, stdoutPollingEnabled, 0, finalStatus, finalFailed)) goto SSH_EXIT;
        if (isSshOutputBackedUp()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!drainSshStream(session, channel, buffer, stderrPollingEnabled, 1, finalStatus, finalFailed)) goto SSH_EXIT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    finalStatus = "SSH CLOSED";

SSH_EXIT:
    if (channel != NULL) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        channel = NULL;
    }
    if (session != NULL) {
        if (ssh_is_connected(session)) ssh_disconnect(session);
        ssh_free(session);
        session = NULL;
    }
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }
    authSecret = "";

    if (lockSshState()) {
        sshTaskExited = true;
        if (sshTaskHandle == xTaskGetCurrentTaskHandle()) sshTaskHandle = NULL;
        sshStopRequested = false;
        sshWorkerPass = "";
        sshConnected = false;
        sshShellReady = false;
        sshStatus = finalStatus;
        sshTerminalDirty = true;
        unlockSshState();
    }
    if (finalStatus != "SSH CLOSED" || finalFailed) queueSshOutput("[STATUS] " + finalStatus + "\n");
    Serial.printf("[SSHDBG] task exit status=%s failed=%d freeHeap=%u\n", finalStatus.c_str(), finalFailed, (unsigned)ESP.getFreeHeap());
    vTaskDelete(NULL);
}

bool connectSshProfile() {
    sshTarget.trim();
    sshHost.trim();
    sshPort.trim();
    sshUser.trim();
    if (sshPort == "") sshPort = "22";

    if (sshTarget.startsWith("ssh ")) {
        sshTarget = sshTarget.substring(4);
        sshTarget.trim();
    }
    if (sshTarget != "") {
        int at = sshTarget.indexOf('@');
        if (at > 0 && at < sshTarget.length() - 1) {
            sshUser = sshTarget.substring(0, at);
            sshHost = sshTarget.substring(at + 1);
        } else {
            sshHost = sshTarget;
        }
        int colon = sshHost.lastIndexOf(':');
        if (colon > 0 && colon < sshHost.length() - 1) {
            String maybePort = sshHost.substring(colon + 1);
            bool numericPort = true;
            for (int i = 0; i < maybePort.length(); i++) {
                if (maybePort.charAt(i) < '0' || maybePort.charAt(i) > '9') numericPort = false;
            }
            if (numericPort) {
                sshPort = maybePort;
                sshHost = sshHost.substring(0, colon);
            }
        }
        sshHost.trim();
        sshUser.trim();
        sshTarget = (sshUser == "") ? sshHost : sshUser + "@" + sshHost;
    }

    prefs.putString("ssh_target", sshTarget);
    prefs.putString("ssh_host", sshHost);
    prefs.putString("ssh_user", sshUser);
    prefs.putString("ssh_port", sshPort);
    prefs.remove("ssh_pass");

    closeSshSession();
    if (getSshTaskHandle() != NULL) {
        setSshStatus("SSH BUSY", false, false);
        drawMessage("SSH BUSY", "CLOSING OLD SESSION");
        delay(1200);
        return false;
    }

    sshBanner = "";
    sshTerminalMode = false;
    sshTerminalLog = "";
    sshInputLine = "";
    sshTerminalDirty = false;
    sshQueuedCommand = "";
    sshQueuedOutput = "";

    if (WiFi.status() != WL_CONNECTED) {
        setSshStatus("WIFI OFFLINE", false, false);
        playSound(sound_fail, sound_fail_size);
        drawProgressBar(100, "SSH WIFI OFFLINE", CP_RED);
        delay(1200);
        return false;
    }
    if (sshHost == "") {
        setSshStatus("HOST REQUIRED", false, false);
        playSound(sound_fail, sound_fail_size);
        drawMessage("SSH HOST", "REQUIRED");
        delay(1200);
        return false;
    }
    if (sshUser == "") {
        setSshStatus("USER REQUIRED", false, false);
        playSound(sound_fail, sound_fail_size);
        drawMessage("USE user@host", "FOR SSH TARGET");
        delay(1400);
        return false;
    }

    long portValue = sshPort.toInt();
    if (portValue < 1 || portValue > 65535) {
        setSshStatus("BAD PORT", false, false);
        playSound(sound_fail, sound_fail_size);
        drawMessage("SSH PORT", "INVALID");
        delay(1200);
        return false;
    }

    IPAddress resolvedIp;
    drawProgressBar(20, "RESOLVING SSH HOST...", CP_CYAN);
    if (WiFi.hostByName(sshHost.c_str(), resolvedIp)) {
        sshHost = resolvedIp.toString();
    } else {
        setSshStatus("DNS FAIL", false, false);
        playSound(sound_fail, sound_fail_size);
        drawMessage("SSH DNS FAIL", sshHost);
        delay(1400);
        return false;
    }

    if (!sshLibReady) {
        libssh_begin();
        sshLibReady = true;
    }

    sshTerminalMode = true;
    appendSshTerminal(String("$ ssh ") + sshTarget + (sshPort != "22" ? String(":") + sshPort : String("")) + "\n");
    appendSshTerminal("[BRUCE] starting 32KB SSH task\n");

    if (!initSshMutex()) {
        setSshStatus("MUTEX FAIL", false, false);
        playSound(sound_fail, sound_fail_size);
        drawMessage("SSH MUTEX", "FAILED");
        delay(1200);
        return false;
    }

    if (lockSshState()) {
        sshWorkerHost = sshHost;
        sshWorkerUser = sshUser;
        sshWorkerPass = sshPass;
        sshWorkerPort = (int)portValue;
        sshStopRequested = false;
        sshTaskExited = false;
        sshConnected = false;
        sshShellReady = false;
        sshStatus = "CONNECTING";
        unlockSshState();
    }

    TaskHandle_t workerHandle = NULL;
    BaseType_t created = xTaskCreate(
        sshWorkerTask,
        "SSH Task",
        BREACH_SSH_TASK_STACK_SIZE,
        NULL,
        1,
        &workerHandle
    );
    if (created != pdPASS || workerHandle == NULL) {
        sshPass = "";
        if (lockSshState()) {
            sshWorkerPass = "";
            sshTaskExited = true;
            unlockSshState();
        }
        setSshStatus("TASK FAIL", false, false);
        playSound(sound_fail, sound_fail_size);
        drawProgressBar(100, "SSH TASK FAIL", CP_RED);
        delay(1400);
        return false;
    }
    if (lockSshState()) {
        if (!sshTaskExited) sshTaskHandle = workerHandle;
        unlockSshState();
    }
    sshPass = "";
    drawProgressBar(100, "SSH TASK STARTED", CP_CYAN);
    delay(500);
    return true;
}

void handleSshInput(Keyboard_Class::KeysState status) {
    if (sshTerminalMode) {
        bool exitTerminal = false;
        bool backspaceRequested = status.del;
        for (char c : status.word) {
            if (c == '\b' || c == 0x7f) backspaceRequested = true;
            if (c == '`' || (c == ',' && sshInputLine == "")) exitTerminal = true;
        }
        if (exitTerminal) {
            playSound(sound_select, sound_select_size);
            closeSshSession();
            setSshStatus("CLOSED", false, false);
            sshTerminalMode = false;
            return;
        }

        if (backspaceRequested) {
            if (sshInputLine.length() > 0) sshInputLine.remove(sshInputLine.length() - 1);
            return;
        }

        for (char c : status.word) {
            if (c == '\b' || c == 0x7f) continue;
            if (c < 32 || c > 126 || c == '`') continue;
            if (c == ',' && sshInputLine == "") continue;
            if (sshInputLine.length() < 80) sshInputLine += c;
        }

        if (status.enter) {
            String line = sshInputLine;
            String statusSnapshot = "";
            bool connectedSnapshot = false;
            bool shellReadySnapshot = false;
            TaskHandle_t taskSnapshot = NULL;
            snapshotSshState(statusSnapshot, connectedSnapshot, shellReadySnapshot, taskSnapshot);
            appendSshTerminal(String("> ") + line + "\n");
            if (connectedSnapshot && taskSnapshot != NULL) {
                queueSshCommand(line + "\r");
            } else {
                appendSshTerminal("[NOT CONNECTED]\n");
                setSshStatus("DISCONNECTED", false, false);
            }
            sshInputLine = "";
        }
        return;
    }

    bool hasBack = false;
    bool backspaceRequested = status.del;
    for (char c : status.word) {
        if (c == '\b' || c == 0x7f) backspaceRequested = true;
        if (c == ',' || c == '`') hasBack = true;
    }
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_MAIN_MENU;
        drawMainMenu();
        return;
    }

    if (backspaceRequested) {
        if (sshFocus == 0 && sshTarget.length() > 0) sshTarget.remove(sshTarget.length() - 1);
        if (sshFocus == 1 && sshPass.length() > 0) sshPass.remove(sshPass.length() - 1);
        return;
    }

    for (char c : status.word) {
        if (c == '\b' || c == 0x7f) continue;
        if (c < 32 || c > 126 || c == ',' || c == '`') continue;
        if (sshFocus == 0 && sshTarget.length() < 64) sshTarget += c;
        else if (sshFocus == 1 && sshPass.length() < 64) sshPass += c;
    }

    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (sshFocus == 2) {
            if (sshPort == "") sshPort = "22";
            prefs.putString("ssh_target", sshTarget);
            prefs.putString("ssh_port", sshPort);
            prefs.remove("ssh_pass");
            connectSshProfile();
            drawSshScreen();
            return;
        }
        sshFocus++;
        if (sshFocus > 2) sshFocus = 0;
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

