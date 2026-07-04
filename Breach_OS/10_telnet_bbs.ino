// Telnet BBS quick-connect setup and raw terminal client.

static constexpr uint8_t TELNET_IAC = 255;
static constexpr uint8_t TELNET_DONT = 254;
static constexpr uint8_t TELNET_DO = 253;
static constexpr uint8_t TELNET_WONT = 252;
static constexpr uint8_t TELNET_WILL = 251;
static constexpr uint8_t TELNET_SB = 250;
static constexpr uint8_t TELNET_SE = 240;
static constexpr uint8_t TELNET_OPT_ECHO = 1;
static constexpr uint8_t TELNET_OPT_SUPPRESS_GO_AHEAD = 3;
static constexpr uint8_t TELNET_OPT_TERMINAL_TYPE = 24;
static constexpr uint8_t TELNET_OPT_NAWS = 31;
static constexpr uint8_t TELNET_TTYPE_IS = 0;
static constexpr uint8_t TELNET_TTYPE_SEND = 1;

int maxTelnetPanCol() {
    int maxCol = BREACH_TELNET_BBS_COLS - BREACH_TELNET_VISIBLE_COLS;
    return maxCol > 0 ? maxCol : 0;
}

int maxTelnetPanRow() {
    int maxRow = BREACH_TELNET_BBS_ROWS - BREACH_TELNET_VISIBLE_ROWS;
    return maxRow > 0 ? maxRow : 0;
}

void clampTelnetView() {
    if (telnetPanCol < 0) telnetPanCol = 0;
    if (telnetPanRow < 0) telnetPanRow = 0;
    int maxCol = maxTelnetPanCol();
    int maxRow = maxTelnetPanRow();
    if (telnetPanCol > maxCol) telnetPanCol = maxCol;
    if (telnetPanRow > maxRow) telnetPanRow = maxRow;
}

void resetTelnetBbsScreen() {
    for (int row = 0; row < BREACH_TELNET_BBS_ROWS; row++) {
        memset(telnetScreen[row], ' ', BREACH_TELNET_BBS_COLS);
        telnetScreen[row][BREACH_TELNET_BBS_COLS] = '\0';
    }
    telnetCursorCol = 0;
    telnetCursorRow = 0;
    telnetPanCol = 0;
    telnetPanRow = 0;
}

void clearTelnetLinePart(int row, int startCol, int endCol) {
    if (row < 0 || row >= BREACH_TELNET_BBS_ROWS) return;
    if (startCol < 0) startCol = 0;
    if (endCol >= BREACH_TELNET_BBS_COLS) endCol = BREACH_TELNET_BBS_COLS - 1;
    for (int col = startCol; col <= endCol; col++) telnetScreen[row][col] = ' ';
}

void scrollTelnetBbsScreen() {
    for (int row = 0; row < BREACH_TELNET_BBS_ROWS - 1; row++) {
        memcpy(telnetScreen[row], telnetScreen[row + 1], BREACH_TELNET_BBS_COLS + 1);
    }
    memset(telnetScreen[BREACH_TELNET_BBS_ROWS - 1], ' ', BREACH_TELNET_BBS_COLS);
    telnetScreen[BREACH_TELNET_BBS_ROWS - 1][BREACH_TELNET_BBS_COLS] = '\0';
    telnetCursorRow = BREACH_TELNET_BBS_ROWS - 1;
}

void setTelnetCursor(int row, int col) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= BREACH_TELNET_BBS_ROWS) row = BREACH_TELNET_BBS_ROWS - 1;
    if (col >= BREACH_TELNET_BBS_COLS) col = BREACH_TELNET_BBS_COLS - 1;
    telnetCursorRow = row;
    telnetCursorCol = col;
}

void advanceTelnetRow() {
    telnetCursorCol = 0;
    telnetCursorRow++;
    if (telnetCursorRow >= BREACH_TELNET_BBS_ROWS) scrollTelnetBbsScreen();
}

void putTelnetScreenChar(char c) {
    if (c == '\r') {
        telnetCursorCol = 0;
        return;
    }
    if (c == '\n') {
        advanceTelnetRow();
        return;
    }
    if (c == '\b' || c == 0x7f) {
        if (telnetCursorCol > 0) telnetCursorCol--;
        return;
    }
    if ((uint8_t)c < 32) return;

    telnetScreen[telnetCursorRow][telnetCursorCol] = c;
    telnetCursorCol++;
    if (telnetCursorCol >= BREACH_TELNET_BBS_COLS) advanceTelnetRow();
}

int telnetCsiParam(const String &params, int index, int fallback) {
    int current = 0;
    int value = 0;
    bool hasValue = false;
    for (int i = 0; i <= params.length(); i++) {
        char c = (i < params.length()) ? params.charAt(i) : ';';
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            hasValue = true;
        } else if (c == ';') {
            if (current == index) return hasValue ? value : fallback;
            current++;
            value = 0;
            hasValue = false;
        }
    }
    return fallback;
}

void clearTelnetFromCursor() {
    clearTelnetLinePart(telnetCursorRow, telnetCursorCol, BREACH_TELNET_BBS_COLS - 1);
    for (int row = telnetCursorRow + 1; row < BREACH_TELNET_BBS_ROWS; row++) {
        clearTelnetLinePart(row, 0, BREACH_TELNET_BBS_COLS - 1);
    }
}

void handleTelnetCsi(char command) {
    int first = telnetCsiParam(telnetAnsiParams, 0, 0);
    if (command == 'H' || command == 'f') {
        int row = telnetCsiParam(telnetAnsiParams, 0, 1) - 1;
        int col = telnetCsiParam(telnetAnsiParams, 1, 1) - 1;
        setTelnetCursor(row, col);
    } else if (command == 'A') {
        setTelnetCursor(telnetCursorRow - (first == 0 ? 1 : first), telnetCursorCol);
    } else if (command == 'B') {
        setTelnetCursor(telnetCursorRow + (first == 0 ? 1 : first), telnetCursorCol);
    } else if (command == 'C') {
        setTelnetCursor(telnetCursorRow, telnetCursorCol + (first == 0 ? 1 : first));
    } else if (command == 'D') {
        setTelnetCursor(telnetCursorRow, telnetCursorCol - (first == 0 ? 1 : first));
    } else if (command == 'J') {
        if (first == 2 || first == 3) resetTelnetBbsScreen();
        else clearTelnetFromCursor();
    } else if (command == 'K') {
        if (first == 1) clearTelnetLinePart(telnetCursorRow, 0, telnetCursorCol);
        else if (first == 2) clearTelnetLinePart(telnetCursorRow, 0, BREACH_TELNET_BBS_COLS - 1);
        else clearTelnetLinePart(telnetCursorRow, telnetCursorCol, BREACH_TELNET_BBS_COLS - 1);
    } else if (command == 'G') {
        setTelnetCursor(telnetCursorRow, (first == 0 ? 1 : first) - 1);
    }
    telnetAnsiParams = "";
}

void clearTelnetTerminalLog() {
    telnetTerminalLog = "";
    telnetScrollOffset = 0;
    resetTelnetBbsScreen();
    telnetAnsiEsc = false;
    telnetAnsiCsi = false;
    telnetAnsiParams = "";
    telnetTerminalDirty = true;
}

void appendTelnetTerminalByte(char c) {
    telnetTerminalLog += c;
    if (telnetTerminalLog.length() > BREACH_TELNET_LOG_MAX) {
        telnetTerminalLog.remove(0, telnetTerminalLog.length() - BREACH_TELNET_LOG_MAX);
    }
    putTelnetScreenChar(c);
}

String telnetCp437Fallback(uint8_t b) {
    switch (b) {
        case 0xB0: case 0xB1: case 0xB2: case 0xDB: return "#";
        case 0xB3: case 0xBA: return "|";
        case 0xC4: case 0xCD: return "-";
        case 0xBF: case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC5: case 0xD9: case 0xDA:
        case 0xB4: case 0xBB: case 0xBC: case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCE:
            return "+";
        case 0xF9: return ".";
        case 0xFA: case 0xFE: return "*";
        case 0x10: case 0x11: return ">";
        case 0x1E: return "^";
        case 0x1F: return "v";
        default: return "?";
    }
}

void appendTelnetTerminal(String text) {
    for (int i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        if (telnetAnsiEsc) {
            if (!telnetAnsiCsi) {
                if (c == '[') {
                    telnetAnsiCsi = true;
                } else {
                    telnetAnsiEsc = false;
                }
                continue;
            }

            if (c >= '@' && c <= '~') {
                handleTelnetCsi(c);
                telnetAnsiEsc = false;
                telnetAnsiCsi = false;
                telnetAnsiParams = "";
            } else {
                telnetAnsiParams += c;
            }
            continue;
        }
        if (c == 27) {
            telnetAnsiEsc = true;
            telnetAnsiCsi = false;
            telnetAnsiParams = "";
            continue;
        }
        if (c != '\n' && c != '\r' && (uint8_t)c < 32) continue;
        if ((uint8_t)c >= 0x80) {
            appendTelnetTerminalByte(telnetCp437Fallback((uint8_t)c)[0]);
        } else {
            appendTelnetTerminalByte(c);
        }
        if (telnetTerminalLog.length() > BREACH_TELNET_LOG_MAX) {
            telnetTerminalLog.remove(0, telnetTerminalLog.length() - BREACH_TELNET_LOG_MAX);
        }
    }
    if (telnetScrollOffset < 0) telnetScrollOffset = 0;
    telnetTerminalDirty = true;
}

int countTelnetTerminalLines(const String &view) {
    if (view.length() == 0) return 0;
    int lines = 1;
    for (int i = 0; i < view.length() - 1; i++) {
        if (view.charAt(i) == '\n') lines++;
    }
    return lines;
}

int findTelnetLineStart(const String &view, int targetLine) {
    if (targetLine <= 0) return 0;
    int line = 0;
    for (int i = 0; i < view.length(); i++) {
        if (view.charAt(i) == '\n') {
            line++;
            if (line >= targetLine) return i + 1;
        }
    }
    return view.length();
}

int maxTelnetScrollOffset() {
    int maxOffset = countTelnetTerminalLines(telnetTerminalLog) - BREACH_TELNET_VISIBLE_ROWS;
    return maxOffset > 0 ? maxOffset : 0;
}

void sendTelnetTriplet(uint8_t command, uint8_t option) {
    uint8_t response[] = {TELNET_IAC, command, option};
    telnetClient.write(response, sizeof(response));
}

void sendTelnetNaws() {
    uint8_t response[] = {
        TELNET_IAC, TELNET_SB, TELNET_OPT_NAWS,
        0, (uint8_t)BREACH_TELNET_BBS_COLS,
        0, (uint8_t)BREACH_TELNET_BBS_ROWS,
        TELNET_IAC, TELNET_SE
    };
    telnetClient.write(response, sizeof(response));
}

void sendTelnetTerminalType() {
    const char term[] = "ANSI";
    uint8_t head[] = {TELNET_IAC, TELNET_SB, TELNET_OPT_TERMINAL_TYPE, TELNET_TTYPE_IS};
    telnetClient.write(head, sizeof(head));
    telnetClient.write((const uint8_t*)term, strlen(term));
    uint8_t tail[] = {TELNET_IAC, TELNET_SE};
    telnetClient.write(tail, sizeof(tail));
}

void handleTelnetNegotiation(uint8_t command, uint8_t option) {
    if (command == TELNET_DO) {
        if (option == TELNET_OPT_SUPPRESS_GO_AHEAD || option == TELNET_OPT_TERMINAL_TYPE || option == TELNET_OPT_NAWS) {
            sendTelnetTriplet(TELNET_WILL, option);
            if (option == TELNET_OPT_NAWS) sendTelnetNaws();
        } else {
            sendTelnetTriplet(TELNET_WONT, option);
        }
    } else if (command == TELNET_WILL) {
        if (option == TELNET_OPT_ECHO || option == TELNET_OPT_SUPPRESS_GO_AHEAD) {
            sendTelnetTriplet(TELNET_DO, option);
        } else {
            sendTelnetTriplet(TELNET_DONT, option);
        }
    } else if (command == TELNET_DONT) {
        sendTelnetTriplet(TELNET_WONT, option);
    } else if (command == TELNET_WONT) {
        sendTelnetTriplet(TELNET_DONT, option);
    }
}

void finishTelnetSubnegotiation() {
    if (telnetSbOption == TELNET_OPT_TERMINAL_TYPE && telnetSbData.length() > 0 && (uint8_t)telnetSbData.charAt(0) == TELNET_TTYPE_SEND) {
        sendTelnetTerminalType();
    }
    telnetSbOption = -1;
    telnetSbData = "";
}

void handleTelnetByte(uint8_t b) {
    if (telnetIacState == 1) {
        if (b == TELNET_IAC) {
            appendTelnetTerminalByte((char)b);
        } else if (b == TELNET_DO || b == TELNET_DONT || b == TELNET_WILL || b == TELNET_WONT) {
            telnetIacCommand = b;
            telnetIacState = 2;
            return;
        } else if (b == TELNET_SB) {
            telnetSbOption = -1;
            telnetSbData = "";
            telnetIacState = 3;
            return;
        }
        telnetIacState = 0;
        return;
    }

    if (telnetIacState == 2) {
        handleTelnetNegotiation((uint8_t)telnetIacCommand, b);
        telnetIacState = 0;
        return;
    }

    if (telnetIacState == 3) {
        if (b == TELNET_IAC) {
            telnetIacState = 4;
        } else if (telnetSbOption < 0) {
            telnetSbOption = b;
        } else if (telnetSbData.length() < 24) {
            telnetSbData += (char)b;
        }
        return;
    }

    if (telnetIacState == 4) {
        if (b == TELNET_SE) {
            finishTelnetSubnegotiation();
            telnetIacState = 0;
        } else {
            telnetIacState = 3;
        }
        return;
    }

    if (b == TELNET_IAC) {
        telnetIacState = 1;
        return;
    }

    String one = "";
    one += (char)b;
    appendTelnetTerminal(one);
}

void pollTelnetBbs() {
    if (!telnetTerminalMode) return;
    if (!telnetClient.connected() && telnetConnected) {
        telnetConnected = false;
        telnetStatus = "CLOSED";
        telnetTerminalDirty = true;
    }

    int guard = 0;
    while (telnetClient.available() && guard < 512) {
        int b = telnetClient.read();
        if (b >= 0) handleTelnetByte((uint8_t)b);
        guard++;
    }
}

void drawTelnetBbsTerminal() {
    pollTelnetBbs();
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("--- TELNET BBS ---", 120, 10);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);

    String title = telnetTarget == "" ? telnetHost : telnetTarget;
    if (telnetPort != "" && telnetPort != "23") title += ":" + telnetPort;
    if (title.length() > 18) title = title.substring(0, 17) + "~";
    canvas.setTextColor(telnetConnected ? CP_GREEN : CP_RED);
    canvas.setCursor(12, 28);
    canvas.print(telnetConnected ? "CONNECTED" : "OFFLINE");
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(82, 28);
    canvas.print(title);

    clampTelnetView();
    canvas.setTextColor(WHITE);
    int y = 42;
    for (int row = 0; row < BREACH_TELNET_VISIBLE_ROWS; row++) {
        int screenRow = telnetPanRow + row;
        String line = "";
        for (int col = 0; col < BREACH_TELNET_VISIBLE_COLS; col++) {
            line += telnetScreen[screenRow][telnetPanCol + col];
        }
        canvas.setCursor(7, y);
        canvas.print(line);
        y += 11;
    }

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(7, 112);
    canvas.print("FN+PAN X" + String(telnetPanCol + 1) + " Y" + String(telnetPanRow + 1) + " `:EXIT");
    pushCanvas();
}

void drawTelnetBbsScreen() {
    if (telnetTerminalMode) {
        drawTelnetBbsTerminal();
        return;
    }

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- TELNET BBS CONNECT ---", 120, 10);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);

    bool wifiOnline = WiFi.status() == WL_CONNECTED;
    canvas.setTextColor(wifiOnline ? CP_GREEN : CP_RED);
    canvas.setCursor(12, 27);
    canvas.print(wifiOnline ? "LINK: ONLINE" : "LINK: OFFLINE");
    canvas.setTextColor(telnetConnected ? CP_GREEN : CP_DIM);
    canvas.setCursor(120, 27);
    String statusText = telnetStatus == "" ? String("READY") : telnetStatus;
    if (statusText.length() > 13) statusText = statusText.substring(0, 12) + "~";
    canvas.print("BBS:" + statusText);

    String targetText = telnetTarget;
    String portText = telnetPort == "" ? String("23") : telnetPort;
    String rememberText = telnetRememberMe ? String("ON") : String("OFF");
    if (targetText.length() > 22) targetText = targetText.substring(0, 21) + "~";

    String values[3] = {targetText, portText, rememberText};
    String labels[3] = {"TARGET", "PORT", "REMEMBER"};
    for (int i = 0; i < 3; i++) {
        int y = 45 + i * 20;
        uint16_t color = (telnetFocus == i) ? CP_YELLOW : WHITE;
        canvas.setTextColor(color);
        drawChippedButton(10, y - 2, 220, 16, color);
        canvas.setCursor(16, y + 1);
        canvas.print(labels[i] + ": " + values[i] + ((telnetFocus == i && blinkState) ? "_" : ""));
    }

    uint16_t connectColor = (telnetFocus == 3) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 110, 100, 17, connectColor);
    canvas.setTextColor(connectColor);
    drawGlitchText("CONNECT", 60, 113, 1, connectColor);

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(122, 114);
    canvas.print("FN+ARROWS MOVE");
    pushCanvas();
}

void prepareTelnetBbsSetup() {
    telnetFocus = 0;
    telnetTerminalMode = false;
    telnetTerminalDirty = false;
    telnetTarget.trim();
    telnetPort.trim();
    if (telnetPort == "") telnetPort = "23";
    telnetStatus = "READY";
}

bool applyKnownTelnetBbsShortcut(String &target) {
    String lower = target;
    lower.trim();
    lower.toLowerCase();
    if (lower.indexOf("telnetbbsguide.com/bbs/after-hours-bbs") >= 0 ||
        lower == "after-hours-bbs" || lower == "after hours bbs" || lower == "afterhours") {
        target = "ah-bbs.com:2333";
        telnetTarget = target;
        return true;
    }
    return false;
}

bool parseTelnetBbsTarget() {
    telnetTarget.trim();
    telnetPort.trim();
    if (telnetPort == "") telnetPort = "23";

    String target = telnetTarget;
    target.trim();
    applyKnownTelnetBbsShortcut(target);
    if (target.startsWith("telnet ")) {
        target = target.substring(7);
        target.trim();
    }
    int scheme = target.indexOf("://");
    if (scheme >= 0) {
        target = target.substring(scheme + 3);
        target.trim();
    }
    int slash = target.indexOf('/');
    if (slash >= 0) target = target.substring(0, slash);
    int space = target.indexOf(' ');
    if (space >= 0) target = target.substring(0, space);
    if (target == "") return false;

    int colon = target.lastIndexOf(':');
    if (colon > 0 && colon < target.length() - 1) {
        String maybePort = target.substring(colon + 1);
        bool digits = true;
        for (int i = 0; i < maybePort.length(); i++) {
            if (!isDigit(maybePort.charAt(i))) digits = false;
        }
        if (digits) {
            telnetPort = maybePort;
            target = target.substring(0, colon);
        }
    }

    telnetHost = target;
    telnetHost.trim();
    return telnetHost != "";
}

void saveTelnetBbsPrefs() {
    prefs.putBool("telnet_remember", telnetRememberMe);
    if (telnetRememberMe) {
        prefs.putString("telnet_target", telnetTarget);
        prefs.putString("telnet_host", telnetHost);
        prefs.putString("telnet_port", telnetPort);
    } else {
        prefs.remove("telnet_target");
        prefs.remove("telnet_host");
        prefs.remove("telnet_port");
    }
}

bool connectTelnetBbs() {
    if (WiFi.status() != WL_CONNECTED) {
        telnetStatus = "NO WIFI";
        drawProgressBar(100, "TELNET: NO WIFI", CP_RED);
        delay(1200);
        return false;
    }
    if (!parseTelnetBbsTarget()) {
        telnetStatus = "HOST REQUIRED";
        drawProgressBar(100, "BBS HOST REQUIRED", CP_RED);
        delay(1200);
        return false;
    }

    long portValue = telnetPort.toInt();
    if (portValue < 1 || portValue > 65535) {
        telnetStatus = "BAD PORT";
        drawProgressBar(100, "TELNET PORT INVALID", CP_RED);
        delay(1200);
        return false;
    }

    saveTelnetBbsPrefs();
    closeTelnetBbs();
    clearTelnetTerminalLog();
    telnetInputLine = "";
    telnetIacState = 0;
    telnetIacCommand = 0;
    telnetSbOption = -1;
    telnetSbData = "";
    telnetStatus = "CONNECTING";
    drawProgressBar(35, "DIALING BBS...", CP_CYAN);

    telnetClient.setTimeout(4000);
    if (!telnetClient.connect(telnetHost.c_str(), (uint16_t)portValue)) {
        telnetConnected = false;
        telnetTerminalMode = false;
        telnetStatus = "CONNECT FAIL";
        playSound(sound_fail, sound_fail_size);
        drawProgressBar(100, "BBS CONNECT FAIL", CP_RED);
        delay(1400);
        return false;
    }

    telnetClient.setNoDelay(true);
    telnetConnected = true;
    telnetTerminalMode = true;
    telnetTerminalDirty = true;
    telnetStatus = "CONNECTED";
    drawProgressBar(100, "BBS LINK OPEN", CP_GREEN);
    delay(500);
    return true;
}

void closeTelnetBbs() {
    if (telnetClient.connected()) telnetClient.stop();
    telnetConnected = false;
    telnetStatus = "CLOSED";
    telnetIacState = 0;
    telnetSbOption = -1;
    telnetSbData = "";
    telnetTerminalDirty = true;
}

void sendTelnetText(const String &text) {
    if (!telnetClient.connected()) {
        telnetConnected = false;
        telnetStatus = "DISCONNECTED";
        telnetTerminalDirty = true;
        return;
    }
    telnetClient.write((const uint8_t*)text.c_str(), text.length());
}

void handleTelnetBbsInput(Keyboard_Class::KeysState status) {
    if (telnetTerminalMode) {
        bool exitTerminal = false;
        bool panLeft = false;
        bool panRight = false;
        bool panUp = false;
        bool panDown = false;
        bool backspaceRequested = status.del;
        for (char c : status.word) {
            if (c == '\b' || c == 0x7f) backspaceRequested = true;
            if (status.fn && c == ',') panLeft = true;
            if (status.fn && c == '/') panRight = true;
            if (status.fn && c == ';') panUp = true;
            if (status.fn && c == '.') panDown = true;
            if (!status.fn && c == '`') exitTerminal = true;
        }

        if (panLeft || panRight || panUp || panDown) {
            if (panLeft) telnetPanCol -= BREACH_TELNET_PAN_COL_STEP;
            if (panRight) telnetPanCol += BREACH_TELNET_PAN_COL_STEP;
            if (panUp) telnetPanRow -= BREACH_TELNET_PAN_ROW_STEP;
            if (panDown) telnetPanRow += BREACH_TELNET_PAN_ROW_STEP;
            clampTelnetView();
            telnetTerminalDirty = true;
            return;
        }
        if (exitTerminal) {
            playSound(sound_select, sound_select_size);
            closeTelnetBbs();
            telnetTerminalMode = false;
            return;
        }
        if (backspaceRequested) {
            uint8_t bs = 0x08;
            if (telnetClient.connected()) telnetClient.write(&bs, 1);
            return;
        }
        if (status.enter) {
            sendTelnetText("\r\n");
            return;
        }
        for (char c : status.word) {
            if (status.fn && (c == ';' || c == '.' || c == ',' || c == '/')) continue;
            if (c == '\b' || c == 0x7f || c == '`') continue;
            if (c < 32 || c > 126) continue;
            uint8_t b = (uint8_t)c;
            if (telnetClient.connected()) telnetClient.write(&b, 1);
        }
        return;
    }

    bool hasBack = false;
    bool movePrev = false;
    bool moveNext = false;
    bool backspaceRequested = status.del;
    for (char c : status.word) {
        if (c == '\b' || c == 0x7f) backspaceRequested = true;
        if (status.fn && (c == ';' || c == ',')) movePrev = true;
        if (status.fn && (c == '.' || c == '/')) moveNext = true;
        if (!status.fn && (c == ',' || c == '`')) hasBack = true;
    }
    if (movePrev || moveNext) {
        playSound(sound_hover, sound_hover_size);
        if (movePrev) telnetFocus = (telnetFocus + 3) % 4;
        if (moveNext) telnetFocus = (telnetFocus + 1) % 4;
        drawTelnetBbsScreen();
        return;
    }
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_MAIN_MENU;
        drawMainMenu();
        return;
    }
    if (backspaceRequested) {
        if (telnetFocus == 0 && telnetTarget.length() > 0) telnetTarget.remove(telnetTarget.length() - 1);
        if (telnetFocus == 1 && telnetPort.length() > 0) telnetPort.remove(telnetPort.length() - 1);
        return;
    }

    for (char c : status.word) {
        if (status.fn && (c == ';' || c == '.' || c == ',' || c == '/')) continue;
        if (c == '\b' || c == 0x7f) continue;
        if (c < 32 || c > 126 || c == ',' || c == '`') continue;
        if (telnetFocus == 0 && telnetTarget.length() < 64) telnetTarget += c;
        else if (telnetFocus == 1 && c >= '0' && c <= '9' && telnetPort.length() < 5) telnetPort += c;
    }

    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (telnetFocus == 2) {
            telnetRememberMe = !telnetRememberMe;
            prefs.putBool("telnet_remember", telnetRememberMe);
            drawTelnetBbsScreen();
            return;
        }
        if (telnetFocus == 3) {
            if (telnetPort == "") telnetPort = "23";
            connectTelnetBbs();
            drawTelnetBbsScreen();
            return;
        }
        telnetFocus++;
        if (telnetFocus > 3) telnetFocus = 0;
    }
}
