// NETWORK NODE / AP MODE: host one normal Cardputer Wi-Fi access point.
// This is intentionally a single SoftAP for lab/field connectivity, not beacon spam.

static constexpr int AP_MODE_FOCUS_COUNT = 7;
static constexpr int AP_MODE_VISIBLE_ROWS = 4;
static constexpr int AP_MODE_MIN_CHANNEL = 1;
static constexpr int AP_MODE_MAX_CHANNEL = 13;
static constexpr int AP_MODE_MAX_SSID_LEN = 32;
static constexpr int AP_MODE_MAX_PASS_LEN = 63;

static int apModeListScroll = 0;

static String apModeShort(String s, int maxLen) {
    if ((int)s.length() <= maxLen) return s;
    if (maxLen <= 1) return s.substring(0, maxLen);
    return s.substring(0, maxLen - 1) + "~";
}

static void apModeClampChannel() {
    if (apModeChannel < AP_MODE_MIN_CHANNEL) apModeChannel = AP_MODE_MAX_CHANNEL;
    if (apModeChannel > AP_MODE_MAX_CHANNEL) apModeChannel = AP_MODE_MIN_CHANNEL;
}

static String apModeMaskedPass() {
    if (apModeOpen) return "<OPEN>";
    String out = "";
    int count = apModePass.length();
    if (count == 0) return "<EMPTY>";
    if (count > 12) count = 12;
    for (int i = 0; i < count; i++) out += "*";
    if (apModePass.length() > count) out += "~";
    return out;
}

static void apModeSavePrefs() {
    prefs.putString("ap_ssid", apModeSsid);
    prefs.putString("ap_pass", apModePass);
    prefs.putInt("ap_ch", apModeChannel);
    prefs.putBool("ap_open", apModeOpen);
}

static void apModeResetDefaults() {
    apModeSsid = "Breach_OS_AP";
    apModePass = "breach123";
    apModeChannel = 6;
    apModeOpen = false;
    apModeStatus = "DEFAULTS READY";
    apModeSavePrefs();
}

static bool apModeStartAp() {
    apModeSsid.trim();
    apModePass.trim();
    if (apModeSsid == "") apModeSsid = "Breach_OS_AP";
    if (apModeSsid.length() > AP_MODE_MAX_SSID_LEN) apModeSsid = apModeSsid.substring(0, AP_MODE_MAX_SSID_LEN);
    apModeClampChannel();

    if (!apModeOpen && apModePass.length() < 8) {
        apModeStatus = "PASS MIN 8 CHARS";
        return false;
    }

    apModeStaWasConnected = (WiFi.status() == WL_CONNECTED);
    WiFi.mode(apModeStaWasConnected ? WIFI_AP_STA : WIFI_AP);
    const char* pass = apModeOpen ? NULL : apModePass.c_str();
    bool ok = WiFi.softAP(apModeSsid.c_str(), pass, apModeChannel, 0, 4);
    if (!ok) {
        apModeRunning = false;
        apModeIp = "";
        apModeStatus = "AP START FAIL";
        return false;
    }

    apModeRunning = true;
    apModeIp = WiFi.softAPIP().toString();
    apModeStatus = "AP ONLINE";
    apModeSavePrefs();
    return true;
}

static void apModeStopAp() {
    WiFi.softAPdisconnect(true);
    apModeRunning = false;
    apModeIp = "";
    apModeStatus = "AP STOPPED";
    if (apModeStaWasConnected || WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
    } else {
        WiFi.mode(WIFI_OFF);
    }
}

static void apModeReturnToNetworkNode() {
    appState = STATE_MAIN_MENU;
    mainMenuFocus = 4;
    currentMenuScroll = mainMenuFocus;
    targetMenuScroll = mainMenuFocus;
    showMenuDesc = false;
    descAnimWidth = 0.0;
    drawMainMenu();
}

static void apModeEnsureVisible() {
    if (apModeFocus < 0) apModeFocus = 0;
    if (apModeFocus >= AP_MODE_FOCUS_COUNT) apModeFocus = AP_MODE_FOCUS_COUNT - 1;
    if (apModeFocus < apModeListScroll) apModeListScroll = apModeFocus;
    if (apModeFocus >= apModeListScroll + AP_MODE_VISIBLE_ROWS) {
        apModeListScroll = apModeFocus - AP_MODE_VISIBLE_ROWS + 1;
    }
    int maxScroll = AP_MODE_FOCUS_COUNT - AP_MODE_VISIBLE_ROWS;
    if (maxScroll < 0) maxScroll = 0;
    if (apModeListScroll > maxScroll) apModeListScroll = maxScroll;
    if (apModeListScroll < 0) apModeListScroll = 0;
}

static void apModeDrawVerticalScrollIndicator(int scroll, int maxScroll, int x, int y, int h) {
    const int railW = 6;
    const int railR = railW / 2;
    const uint16_t railColor = CP_CYAN;
    const uint16_t knobColor = CP_YELLOW;
    int midX = x + railR;
    int topY = y + railR;
    int bottomY = y + h - railR;

    canvas.drawLine(x, topY, x, bottomY, railColor);
    canvas.drawLine(x + railW, topY, x + railW, bottomY, railColor);
    canvas.drawLine(x, topY - 1, x, topY - 1, railColor);
    canvas.drawLine(x + 1, topY - 2, x + 1, topY - 2, railColor);
    canvas.drawLine(midX - 1, topY - railR, midX + 1, topY - railR, railColor);
    canvas.drawLine(x + railW - 1, topY - 2, x + railW - 1, topY - 2, railColor);
    canvas.drawLine(x + railW, topY - 1, x + railW, topY - 1, railColor);
    canvas.drawLine(x, bottomY + 1, x, bottomY + 1, railColor);
    canvas.drawLine(x + 1, bottomY + 2, x + 1, bottomY + 2, railColor);
    canvas.drawLine(midX - 1, bottomY + railR, midX + 1, bottomY + railR, railColor);
    canvas.drawLine(x + railW - 1, bottomY + 2, x + railW - 1, bottomY + 2, railColor);
    canvas.drawLine(x + railW, bottomY + 1, x + railW, bottomY + 1, railColor);

    if (maxScroll <= 0) return;
    if (scroll < 0) scroll = 0;
    if (scroll > maxScroll) scroll = maxScroll;
    float ratio = (float)scroll / (float)maxScroll;
    int dotY = topY + (int)((bottomY - topY) * ratio);
    canvas.fillCircle(midX, dotY, 5, knobColor);
}

static void apModeRowText(int index, String &label, String &value) {
    switch (index) {
        case 0:
            label = "SSID";
            value = apModeSsid;
            if (value == "") value = "<EMPTY>";
            break;
        case 1:
            label = "PASS";
            value = apModeMaskedPass();
            break;
        case 2:
            label = "CHANNEL";
            value = String(apModeChannel);
            break;
        case 3:
            label = "SECURITY";
            value = apModeOpen ? "OPEN" : "WPA2 PSK";
            break;
        case 4:
            label = "START";
            value = apModeRunning ? "RESTART AP" : "START AP";
            break;
        case 5:
            label = "STOP";
            value = apModeRunning ? "STOP AP" : "OFFLINE";
            break;
        default:
            label = "BACK";
            value = apModeRunning ? apModeIp : "NETWORK NODE";
            break;
    }
}

static void apModeDrawRow(int index, int y) {
    bool selected = (apModeFocus == index);
    uint16_t color = selected ? CP_YELLOW : CP_DIM;
    String label;
    String value;
    apModeRowText(index, label, value);
    if (selected && (index == 0 || index == 1) && blinkState) value += "_";
    if (selected && (index == 2 || index == 3)) value = "< " + value + " >";

    drawChippedButton(6, y - 2, 216, 20, color);
    canvas.setTextSize(1);
    canvas.setTextColor(color);
    canvas.setCursor(13, y);
    canvas.print(label);
    canvas.setTextColor(selected ? CP_CYAN : CP_DIM);
    canvas.setCursor(13, y + 10);
    canvas.print(apModeShort(value, 34));
}

void enterApMode() {
    if (apModeSsid == "") apModeSsid = "Breach_OS_AP";
    if (apModePass == "") apModePass = "breach123";
    apModeClampChannel();
    apModeFocus = 0;
    apModeListScroll = 0;
    apModeStatus = apModeRunning ? "AP ONLINE" : "READY";
    if (apModeRunning) apModeIp = WiFi.softAPIP().toString();
    appState = STATE_AP_MODE;
    drawApModeScreen();
}

void drawApModeScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawGlitchText("AP MODE", 72, 4, 1, apModeRunning ? CP_YELLOW : CP_CYAN, true, true);
    canvas.drawLine(5, 18, 235, 18, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(8, 22);
    canvas.print("TYPE EDIT  <> CH/SEC  R DEFAULT");

    canvas.setTextColor(apModeRunning ? CP_GREEN : CP_CYAN);
    canvas.setCursor(8, 32);
    String statusLine = apModeStatus;
    if (apModeRunning) {
        statusLine += " C" + String(WiFi.softAPgetStationNum());
    }
    canvas.print(apModeShort(statusLine, 31));

    apModeEnsureVisible();
    int y = 45;
    for (int row = 0; row < AP_MODE_VISIBLE_ROWS; row++) {
        int idx = apModeListScroll + row;
        if (idx >= AP_MODE_FOCUS_COUNT) break;
        apModeDrawRow(idx, y);
        y += 23;
    }
    int maxScroll = AP_MODE_FOCUS_COUNT - AP_MODE_VISIBLE_ROWS;
    if (maxScroll < 0) maxScroll = 0;
    apModeDrawVerticalScrollIndicator(apModeListScroll, maxScroll, 229, 45, 78);
    pushCanvas();
}

void handleApModeInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasBack = status.del;
    bool resetDefaults = false;
    bool typed = false;

    for (char c : status.word) {
        if (c == ';') hasUp = true;
        else if (c == '.') hasDown = true;
        else if (c == ',') hasLeft = true;
        else if (c == '/') hasRight = true;
        else if (c == '`') hasBack = true;
        else if (c == 'r' || c == 'R') resetDefaults = true;
        else if (c >= 32 && c <= 126) {
            if (apModeFocus == 0 && apModeSsid.length() < AP_MODE_MAX_SSID_LEN) {
                apModeSsid += c;
                typed = true;
            } else if (apModeFocus == 1 && apModePass.length() < AP_MODE_MAX_PASS_LEN) {
                apModePass += c;
                typed = true;
            }
        }
    }

    if (resetDefaults) {
        playSound(sound_select, sound_select_size);
        apModeResetDefaults();
        return;
    }

    if (status.del) {
        if (apModeFocus == 0 && apModeSsid.length() > 0) {
            apModeSsid.remove(apModeSsid.length() - 1);
            typed = true;
        } else if (apModeFocus == 1 && apModePass.length() > 0) {
            apModePass.remove(apModePass.length() - 1);
            typed = true;
        } else {
            hasBack = true;
        }
    }

    if (typed) {
        apModeStatus = "EDITING";
        return;
    }

    if (hasBack || (hasLeft && apModeFocus != 2 && apModeFocus != 3)) {
        playSound(sound_select, sound_select_size);
        apModeSavePrefs();
        apModeReturnToNetworkNode();
        return;
    }

    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        apModeFocus = (apModeFocus - 1 + AP_MODE_FOCUS_COUNT) % AP_MODE_FOCUS_COUNT;
    }
    if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        apModeFocus = (apModeFocus + 1) % AP_MODE_FOCUS_COUNT;
    }

    if (apModeFocus == 2 && (hasLeft || hasRight)) {
        playSound(sound_hover, sound_hover_size);
        apModeChannel += hasRight ? 1 : -1;
        apModeClampChannel();
        apModeStatus = "CHANNEL " + String(apModeChannel);
    } else if (apModeFocus == 3 && (hasLeft || hasRight)) {
        playSound(sound_hover, sound_hover_size);
        apModeOpen = !apModeOpen;
        apModeStatus = apModeOpen ? "OPEN SECURITY" : "WPA2 SECURITY";
    }

    if (!status.enter) return;
    playSound(sound_select, sound_select_size);

    if (apModeFocus == 2) {
        apModeChannel++;
        apModeClampChannel();
        apModeStatus = "CHANNEL " + String(apModeChannel);
    } else if (apModeFocus == 3) {
        apModeOpen = !apModeOpen;
        apModeStatus = apModeOpen ? "OPEN SECURITY" : "WPA2 SECURITY";
    } else if (apModeFocus == 4) {
        apModeStartAp();
    } else if (apModeFocus == 5) {
        apModeStopAp();
    } else if (apModeFocus == 6) {
        apModeSavePrefs();
        apModeReturnToNetworkNode();
    } else {
        apModeFocus = (apModeFocus + 1) % AP_MODE_FOCUS_COUNT;
    }
}
