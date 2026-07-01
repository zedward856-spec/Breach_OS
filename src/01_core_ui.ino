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
        case STATE_OTA_CATALOG: drawOtaCatalog(); break;
        case STATE_DIR_CONFIRM_POPUP: drawDirConfirmPopup(); break;
        case STATE_MUSIC_PLAYER: drawMusicPlayer(); break;
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

