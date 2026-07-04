// HARDWARE NODE / BADUSB: execute DuckyScript from SD /Breach_OS/Ducky.

static const char* BADUSB_ROOT_DIR = "/Breach_OS";
static const char* BADUSB_DUCKY_DIR = "/Breach_OS/Ducky";
static constexpr int BADUSB_SD_CS = 12;
static constexpr uint32_t BADUSB_SD_SPI_HZ = 20000000;
static constexpr int BADUSB_MODE_BROWSER = 0;
static constexpr int BADUSB_MODE_CONFIRM = 1;
static constexpr int BADUSB_MODE_RUNNING = 2;
static constexpr int BADUSB_MODE_DONE = 3;
static constexpr uint32_t BADUSB_MAX_DELAY_MS = 60000;
static constexpr int BADUSB_MAX_REPEAT = 999;

static String badUsbBaseName(String path) {
    int slash = path.lastIndexOf('/');
    if (slash >= 0) return path.substring(slash + 1);
    return path;
}

static String badUsbShortName(String name, int maxLen) {
    if ((int)name.length() <= maxLen) return name;
    if (maxLen <= 3) return name.substring(0, maxLen);
    return name.substring(0, maxLen - 3) + "...";
}

static String badUsbUpper(String value) {
    value.trim();
    value.toUpperCase();
    return value;
}

static bool badUsbIsScriptName(String name) {
    if (name == "" || name.startsWith(".")) return false;
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".txt") || lower.endsWith(".duck") || lower.endsWith(".ducky") || lower.endsWith(".ds");
}

static bool badUsbMountSd() {
    SPI.begin(40, 39, 14, BADUSB_SD_CS);
    if (!SD.begin(BADUSB_SD_CS, SPI, BADUSB_SD_SPI_HZ) || SD.cardType() == CARD_NONE) {
        badUsbStatus = "SD MOUNT FAIL";
        return false;
    }
    if (!SD.exists(BADUSB_ROOT_DIR) && !SD.mkdir(BADUSB_ROOT_DIR)) {
        badUsbStatus = "BREACH_OS DIR FAIL";
        return false;
    }
    if (!SD.exists(BADUSB_DUCKY_DIR) && !SD.mkdir(BADUSB_DUCKY_DIR)) {
        badUsbStatus = "DUCKY DIR FAIL";
        return false;
    }
    File dir = SD.open(BADUSB_DUCKY_DIR);
    bool ok = dir && dir.isDirectory();
    if (dir) dir.close();
    if (!ok) badUsbStatus = "DUCKY DIR INVALID";
    return ok;
}

static void badUsbEnsureVisible() {
    if (badUsbFocus < 0) badUsbFocus = 0;
    if (badUsbFocus >= (int)badUsbScripts.size()) badUsbFocus = badUsbScripts.empty() ? 0 : (int)badUsbScripts.size() - 1;
    if (badUsbFocus < badUsbScrollOffset) badUsbScrollOffset = badUsbFocus;
    if (badUsbFocus >= badUsbScrollOffset + 4) badUsbScrollOffset = badUsbFocus - 3;
    if (badUsbScrollOffset < 0) badUsbScrollOffset = 0;
}

static void refreshBadUsbScripts() {
    badUsbScripts.clear();
    badUsbFocus = 0;
    badUsbScrollOffset = 0;
    if (!badUsbMountSd()) return;

    File dir = SD.open(BADUSB_DUCKY_DIR);
    if (!dir || !dir.isDirectory()) {
        badUsbStatus = "DUCKY DIR OPEN FAIL";
        if (dir) dir.close();
        return;
    }

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String name = badUsbBaseName(entry.name());
            if (badUsbIsScriptName(name)) badUsbScripts.push_back(name);
        }
        entry.close();
    }
    dir.close();
    std::sort(badUsbScripts.begin(), badUsbScripts.end());
    badUsbStatus = badUsbScripts.empty() ? "DROP .TXT/.DUCK HERE" : "READY";
}

static bool badUsbBackPressed() {
    M5Cardputer.update();
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    if (status.del) return true;
    for (char c : status.word) {
        if (c == '`' || c == ',') return true;
    }
    return false;
}

static bool badUsbDelay(uint32_t ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        if (badUsbBackPressed()) {
            badUsbAbortFlag = true;
            return false;
        }
        delay(10);
    }
    return true;
}

#if BREACH_USB_HID_AVAILABLE
static bool badUsbInitHid() {
    if (!badUsbHidReady) {
        badUsbKeyboard.begin();
        badUsbKeyboard.releaseAll();
        USB.begin();
        badUsbHidReady = true;
    }
    return true;
}

static bool badUsbModifierToken(String token, uint8_t &key) {
    token = badUsbUpper(token);
    if (token == "CTRL" || token == "CONTROL") key = KEY_LEFT_CTRL;
    else if (token == "SHIFT") key = KEY_LEFT_SHIFT;
    else if (token == "ALT") key = KEY_LEFT_ALT;
    else if (token == "GUI" || token == "WINDOWS" || token == "WIN" || token == "COMMAND" || token == "CMD") key = KEY_LEFT_GUI;
    else return false;
    return true;
}

static bool badUsbSpecialToken(String token, uint8_t &key) {
    token = badUsbUpper(token);
    if (token == "ENTER" || token == "RETURN") key = KEY_RETURN;
    else if (token == "ESC" || token == "ESCAPE") key = KEY_ESC;
    else if (token == "TAB") key = KEY_TAB;
    else if (token == "SPACE") key = KEY_SPACE;
    else if (token == "BACKSPACE") key = KEY_BACKSPACE;
    else if (token == "DELETE" || token == "DEL") key = KEY_DELETE;
    else if (token == "INSERT") key = KEY_INSERT;
    else if (token == "HOME") key = KEY_HOME;
    else if (token == "END") key = KEY_END;
    else if (token == "PAGEUP" || token == "PAGE_UP") key = KEY_PAGE_UP;
    else if (token == "PAGEDOWN" || token == "PAGE_DOWN") key = KEY_PAGE_DOWN;
    else if (token == "UP" || token == "UPARROW" || token == "UP_ARROW") key = KEY_UP_ARROW;
    else if (token == "DOWN" || token == "DOWNARROW" || token == "DOWN_ARROW") key = KEY_DOWN_ARROW;
    else if (token == "LEFT" || token == "LEFTARROW" || token == "LEFT_ARROW") key = KEY_LEFT_ARROW;
    else if (token == "RIGHT" || token == "RIGHTARROW" || token == "RIGHT_ARROW") key = KEY_RIGHT_ARROW;
    else if (token == "CAPSLOCK" || token == "CAPS_LOCK") key = KEY_CAPS_LOCK;
    else if (token == "PRINTSCREEN" || token == "PRINT_SCREEN") key = KEY_PRINT_SCREEN;
    else if (token == "SCROLLLOCK" || token == "SCROLL_LOCK") key = KEY_SCROLL_LOCK;
    else if (token == "PAUSE" || token == "BREAK") key = KEY_PAUSE;
    else if (token == "MENU" || token == "APP") key = KEY_MENU;
    else if (token.length() >= 2 && token.charAt(0) == 'F') {
        int n = token.substring(1).toInt();
        if (n >= 1 && n <= 12) key = KEY_F1 + (n - 1);
        else return false;
    } else return false;
    return true;
}

static bool badUsbNextToken(String text, int &pos, String &token) {
    token = "";
    while (pos < (int)text.length()) {
        char c = text.charAt(pos);
        if (c == ' ' || c == '\t' || c == '+' || c == '-') pos++;
        else break;
    }
    while (pos < (int)text.length()) {
        char c = text.charAt(pos);
        if (c == ' ' || c == '\t' || c == '+' || c == '-') break;
        token += c;
        pos++;
    }
    return token.length() > 0;
}

static bool badUsbTapCombo(String combo) {
    if (!badUsbInitHid()) return false;
    uint8_t mods[6];
    uint8_t keys[6];
    int modCount = 0;
    int keyCount = 0;
    int pos = 0;
    String token;

    while (badUsbNextToken(combo, pos, token)) {
        uint8_t key = 0;
        if (badUsbModifierToken(token, key)) {
            if (modCount < 6) mods[modCount++] = key;
        } else if (badUsbSpecialToken(token, key)) {
            if (keyCount < 6) keys[keyCount++] = key;
        } else if (token.length() == 1) {
            char c = token.charAt(0);
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            if (keyCount < 6) keys[keyCount++] = (uint8_t)c;
        } else {
            return false;
        }
    }

    if (modCount == 0 && keyCount == 0) return false;
    for (int i = 0; i < modCount; i++) badUsbKeyboard.press(mods[i]);
    for (int i = 0; i < keyCount; i++) badUsbKeyboard.press(keys[i]);
    delay(24);
    badUsbKeyboard.releaseAll();
    delay(24);
    return true;
}

static bool badUsbTypeString(String text, bool newline) {
    if (!badUsbInitHid()) return false;
    badUsbKeyboard.print(text);
    if (newline) badUsbKeyboard.write(KEY_RETURN);
    delay(12);
    return true;
}
#endif

static bool badUsbExecuteDuckyLine(String line, String &lastExecutableLine, bool allowRepeat) {
    line.replace("\r", "");
    String trimmed = line;
    trimmed.trim();
    if (trimmed == "") {
        badUsbSkippedLines++;
        return true;
    }

    String upper = badUsbUpper(trimmed);
    int firstSpace = trimmed.indexOf(' ');
    String cmd = firstSpace < 0 ? upper : badUsbUpper(trimmed.substring(0, firstSpace));
    String arg = firstSpace < 0 ? "" : trimmed.substring(firstSpace + 1);

    if (cmd == "REM" || cmd.startsWith("//")) {
        badUsbSkippedLines++;
        return true;
    }

    if (cmd == "DEFAULT_DELAY" || cmd == "DEFAULTDELAY") {
        long value = arg.toInt();
        if (value < 0) value = 0;
        if (value > (long)BADUSB_MAX_DELAY_MS) value = BADUSB_MAX_DELAY_MS;
        badUsbDefaultDelayMs = (uint32_t)value;
        badUsbExecutedLines++;
        lastExecutableLine = trimmed;
        return true;
    }

    if (cmd == "DELAY") {
        long value = arg.toInt();
        if (value < 0) value = 0;
        if (value > (long)BADUSB_MAX_DELAY_MS) value = BADUSB_MAX_DELAY_MS;
        bool ok = badUsbDelay((uint32_t)value);
        if (ok) {
            badUsbExecutedLines++;
            lastExecutableLine = trimmed;
        }
        return ok;
    }

    if (cmd == "REPEAT") {
        if (!allowRepeat || lastExecutableLine == "") {
            badUsbSkippedLines++;
            return true;
        }
        int repeatCount = arg.toInt();
        if (repeatCount < 1) repeatCount = 1;
        if (repeatCount > BADUSB_MAX_REPEAT) repeatCount = BADUSB_MAX_REPEAT;
        for (int i = 0; i < repeatCount; i++) {
            if (!badUsbExecuteDuckyLine(lastExecutableLine, lastExecutableLine, false)) return false;
            if (badUsbDefaultDelayMs > 0 && !badUsbDelay(badUsbDefaultDelayMs)) return false;
        }
        return true;
    }

#if BREACH_USB_HID_AVAILABLE
    bool ok = false;
    if (cmd == "STRING" || cmd == "TEXT") {
        ok = badUsbTypeString(arg, false);
    } else if (cmd == "STRINGLN" || cmd == "STRING_LINE" || cmd == "TEXTLN") {
        ok = badUsbTypeString(arg, true);
    } else {
        ok = badUsbTapCombo(trimmed);
    }

    if (ok) {
        badUsbExecutedLines++;
        lastExecutableLine = trimmed;
    } else {
        badUsbSkippedLines++;
    }
    return true;
#else
    (void)lastExecutableLine;
    badUsbStatus = "BUILD NEEDS TINYUSB";
    return false;
#endif
}

static void drawBadUsbRunStatus(String statusText) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_YELLOW);
    drawChippedButton(8, 7, 224, 120, CP_DIM);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("--- BADUSB EXECUTOR ---", 120, 12);
    canvas.drawLine(14, 26, 226, 26, CP_YELLOW);
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString(badUsbShortName(badUsbSelectedName, 26), 120, 38);
    canvas.setTextColor(WHITE);
    canvas.drawCenterString(statusText, 120, 56);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("LINE " + String((uint32_t)badUsbLineNumber), 120, 74);
    canvas.drawCenterString("OK " + String((uint32_t)badUsbExecutedLines) + " SKIP " + String((uint32_t)badUsbSkippedLines), 120, 88);
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString("DEL/ESC ABORT", 120, 112);
    pushCanvas();
}

static bool runBadUsbSelectedScript() {
#if BREACH_USB_HID_AVAILABLE
    if (!badUsbInitHid()) {
        badUsbStatus = "HID START FAIL";
        return false;
    }
#else
    badUsbStatus = "BUILD NEEDS TINYUSB";
    return false;
#endif

    if (!badUsbMountSd()) return false;
    String path = String(BADUSB_DUCKY_DIR) + "/" + badUsbSelectedName;
    File script = SD.open(path.c_str(), FILE_READ);
    if (!script) {
        badUsbStatus = "OPEN FAIL";
        return false;
    }

    badUsbAbortFlag = false;
    badUsbMode = BADUSB_MODE_RUNNING;
    badUsbLineNumber = 0;
    badUsbExecutedLines = 0;
    badUsbSkippedLines = 0;
    badUsbDefaultDelayMs = 0;

    for (int i = 3; i >= 1; i--) {
        drawBadUsbRunStatus("RUNNING IN " + String(i));
        if (!badUsbDelay(1000)) {
            script.close();
            badUsbStatus = "ABORTED";
            return false;
        }
    }

    String lastExecutableLine = "";
    while (script.available()) {
        if (badUsbBackPressed()) {
            badUsbAbortFlag = true;
            break;
        }
        String line = script.readStringUntil('\n');
        badUsbLineNumber++;
        if ((badUsbLineNumber % 3) == 1) drawBadUsbRunStatus("EXECUTING");
        if (!badUsbExecuteDuckyLine(line, lastExecutableLine, true)) break;
        if (badUsbDefaultDelayMs > 0 && !badUsbDelay(badUsbDefaultDelayMs)) break;
    }
    script.close();

#if BREACH_USB_HID_AVAILABLE
    badUsbKeyboard.releaseAll();
#endif
    badUsbStatus = badUsbAbortFlag ? "ABORTED" : "DONE";
    return !badUsbAbortFlag;
}

void enterBadUsbMode() {
    stopMp3Playback();
    appState = STATE_BADUSB;
    badUsbMode = BADUSB_MODE_BROWSER;
    badUsbSelectedName = "";
    badUsbStatus = "LOADING DUCKY DIR";
    refreshBadUsbScripts();
    drawBadUsbScreen();
}

void drawBadUsbScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_YELLOW);
    drawChippedButton(8, 7, 224, 120, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("--- BADUSB EXECUTOR ---", 120, 12);
    canvas.drawLine(14, 26, 226, 26, CP_YELLOW);

    if (badUsbMode == BADUSB_MODE_CONFIRM) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("AUTHORIZED HOSTS ONLY", 120, 36);
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString(badUsbShortName(badUsbSelectedName, 26), 120, 52);
        canvas.setTextColor(WHITE);
        canvas.drawCenterString("DIR /Breach_OS/Ducky", 120, 68);
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("ENTER RUNS IN 3 SEC", 120, 88);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("DEL/ESC BACK", 120, 112);
        pushCanvas();
        return;
    }

    if (badUsbMode == BADUSB_MODE_DONE) {
        canvas.setTextColor(badUsbStatus == "DONE" ? CP_GREEN : CP_RED);
        canvas.drawCenterString(badUsbStatus, 120, 36);
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString(badUsbShortName(badUsbSelectedName, 26), 120, 52);
        canvas.setTextColor(WHITE);
        canvas.drawCenterString("LINE " + String((uint32_t)badUsbLineNumber), 120, 70);
        canvas.drawCenterString("OK " + String((uint32_t)badUsbExecutedLines) + " SKIP " + String((uint32_t)badUsbSkippedLines), 120, 84);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("ENTER ARM AGAIN", 120, 100);
        canvas.drawCenterString("DEL/ESC BACK", 120, 114);
        pushCanvas();
        return;
    }

    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString("DIR /Breach_OS/Ducky", 120, 32);
    canvas.setTextColor(badUsbScripts.empty() ? CP_RED : CP_GREEN);
    canvas.drawCenterString(badUsbStatus, 120, 44);

#if !BREACH_USB_HID_AVAILABLE
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString("COMPILE USBMode=default", 120, 64);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("DEL/ESC BACK", 120, 112);
    pushCanvas();
    return;
#endif

    if (badUsbScripts.empty()) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("PUT .TXT/.DUCK FILES", 120, 66);
        canvas.drawCenterString("IN /Breach_OS/Ducky", 120, 80);
        canvas.drawCenterString("ENTER REFRESH", 120, 98);
        canvas.drawCenterString("DEL/ESC BACK", 120, 112);
        pushCanvas();
        return;
    }

    badUsbEnsureVisible();
    int y = 58;
    for (int row = 0; row < 4; row++) {
        int idx = badUsbScrollOffset + row;
        if (idx >= (int)badUsbScripts.size()) break;
        bool selected = (idx == badUsbFocus);
        uint16_t color = selected ? CP_YELLOW : CP_DIM;
        if (selected) drawChippedButton(18, y - 2, 204, 14, CP_YELLOW);
        canvas.setTextColor(color);
        canvas.setCursor(24, y);
        canvas.print((selected ? "> " : "  ") + badUsbShortName(badUsbScripts[idx], 24));
        y += 14;
    }
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("ENTER ARM  DEL/ESC BACK", 120, 116);
    pushCanvas();
}

void handleBadUsbInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasBack = status.del;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '`' || c == ',') hasBack = true;
    }

    if (badUsbMode == BADUSB_MODE_RUNNING) {
        if (hasBack) badUsbAbortFlag = true;
        return;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (badUsbMode == BADUSB_MODE_CONFIRM || badUsbMode == BADUSB_MODE_DONE) {
            badUsbMode = BADUSB_MODE_BROWSER;
            drawBadUsbScreen();
            return;
        }
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 3;
        currentHardwareScroll = 3;
        targetHardwareScroll = 3;
        drawHardwareMenu();
        return;
    }

    if (badUsbMode == BADUSB_MODE_BROWSER) {
        if (hasUp && !badUsbScripts.empty()) {
            playSound(sound_hover, sound_hover_size);
            badUsbFocus--;
            if (badUsbFocus < 0) badUsbFocus = badUsbScripts.size() - 1;
            badUsbEnsureVisible();
            drawBadUsbScreen();
        }
        if (hasDown && !badUsbScripts.empty()) {
            playSound(sound_hover, sound_hover_size);
            badUsbFocus++;
            if (badUsbFocus >= (int)badUsbScripts.size()) badUsbFocus = 0;
            badUsbEnsureVisible();
            drawBadUsbScreen();
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            if (badUsbScripts.empty()) {
                refreshBadUsbScripts();
            } else {
                badUsbSelectedName = badUsbScripts[badUsbFocus];
                badUsbMode = BADUSB_MODE_CONFIRM;
            }
            drawBadUsbScreen();
        }
        return;
    }

    if (badUsbMode == BADUSB_MODE_CONFIRM && status.enter) {
        playSound(sound_select, sound_select_size);
        runBadUsbSelectedScript();
        badUsbMode = BADUSB_MODE_DONE;
        drawBadUsbScreen();
        return;
    }

    if (badUsbMode == BADUSB_MODE_DONE && status.enter) {
        playSound(sound_select, sound_select_size);
        badUsbMode = BADUSB_MODE_CONFIRM;
        drawBadUsbScreen();
    }
}
