// NETWORK NODE / WIFI SCAN: scan nearby access points with the Bluetooth scanner visual style.

static constexpr int WIFI_SCAN_NODE_MAX_NETWORKS = 32;
static constexpr int WIFI_SCAN_NODE_LIST_VISIBLE_ROWS = 4;
static constexpr int WIFI_SCAN_NODE_DETAIL_VISIBLE_ROWS = 7;
static constexpr int WIFI_SCAN_NODE_SCAN_TIMEOUT_MS = 7000;

struct WifiScanNodeInfo {
    String ssid;
    String bssid;
    int32_t rssi = -999;
    int32_t channel = 0;
    int authMode = WIFI_AUTH_OPEN;
    bool hidden = false;
    bool connected = false;
    bool savedProfile = false;
};

static std::vector<WifiScanNodeInfo> wifiScanNodeNetworks;
static int wifiScanNodeFocus = 0;
static int wifiScanNodeListScroll = 0;
static bool wifiScanNodeDetailMode = false;
static int wifiScanNodeDetailScroll = 0;
static String wifiScanNodeStatus = "READY";
static unsigned long wifiScanNodeLastScanMs = 0;

static String wifiScanNodeShort(String s, int maxLen) {
    if ((int)s.length() <= maxLen) return s;
    if (maxLen <= 1) return s.substring(0, maxLen);
    return s.substring(0, maxLen - 1) + "~";
}

static String wifiScanNodeSsidLabel(String ssid, bool hidden) {
    if (hidden || ssid == "") return "(hidden ssid)";
    return ssid;
}

static int wifiScanNodeSignalBars(int rssi) {
    if (rssi >= -50) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -78) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

static String wifiScanNodeAuthName(int authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2 PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2 PSK";
        case WIFI_AUTH_ENTERPRISE: return "WPA2 ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3 PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI PSK";
        case WIFI_AUTH_OWE: return "OWE";
        case WIFI_AUTH_WPA3_ENT_192: return "WPA3 ENT192";
        case WIFI_AUTH_WPA3_EXT_PSK: return "WPA3 EXT";
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE: return "WPA3 MIXED";
        case WIFI_AUTH_DPP: return "DPP";
        case WIFI_AUTH_WPA3_ENTERPRISE: return "WPA3 ENT";
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "WPA2/3 ENT";
        case WIFI_AUTH_WPA_ENTERPRISE: return "WPA ENT";
        default: return "AUTH " + String(authMode);
    }
}

static String wifiScanNodeSecurityClass(int authMode) {
    if (authMode == WIFI_AUTH_OPEN) return "OPEN / NO KEY";
    if (authMode == WIFI_AUTH_WEP) return "LEGACY WEAK";
    if (authMode == WIFI_AUTH_WPA3_PSK || authMode == WIFI_AUTH_WPA3_ENTERPRISE || authMode == WIFI_AUTH_WPA3_ENT_192) return "MODERN";
    return "SECURED";
}

static void wifiScanNodeEnsureVisible() {
    if (wifiScanNodeFocus < 0) wifiScanNodeFocus = 0;
    if (wifiScanNodeFocus >= (int)wifiScanNodeNetworks.size()) {
        wifiScanNodeFocus = wifiScanNodeNetworks.empty() ? 0 : (int)wifiScanNodeNetworks.size() - 1;
    }
    if (wifiScanNodeFocus < wifiScanNodeListScroll) wifiScanNodeListScroll = wifiScanNodeFocus;
    if (wifiScanNodeFocus >= wifiScanNodeListScroll + WIFI_SCAN_NODE_LIST_VISIBLE_ROWS) {
        wifiScanNodeListScroll = wifiScanNodeFocus - WIFI_SCAN_NODE_LIST_VISIBLE_ROWS + 1;
    }
    if (wifiScanNodeListScroll < 0) wifiScanNodeListScroll = 0;
}

static void wifiScanNodeDrawVerticalScrollIndicator(int scroll, int maxScroll, int x, int y, int h) {
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

static void wifiScanNodeDrawDetailLine(const String &line, int x, int y, int maxChars) {
    String text = wifiScanNodeShort(line, maxChars);
    int colon = text.indexOf(':');
    if (colon > 0) {
        String label = text.substring(0, colon + 1);
        String value = text.substring(colon + 1);
        canvas.setTextColor(CP_CYAN);
        canvas.setCursor(x, y);
        canvas.print(label);
        canvas.setTextColor(WHITE);
        canvas.setCursor(x + canvas.textWidth(label), y);
        canvas.print(value);
        return;
    }

    canvas.setTextColor(text.startsWith("#") ? CP_CYAN : WHITE);
    canvas.setCursor(x, y);
    canvas.print(text);
}

static void wifiScanNodeBuildDetailLines(int index, std::vector<String> &lines) {
    lines.clear();
    if (index < 0 || index >= (int)wifiScanNodeNetworks.size()) {
        lines.push_back("NO NETWORK SELECTED");
        return;
    }

    const WifiScanNodeInfo &n = wifiScanNodeNetworks[index];
    lines.push_back("SSID: " + wifiScanNodeSsidLabel(n.ssid, n.hidden));
    lines.push_back("BSSID: " + n.bssid);
    lines.push_back("RSSI: " + String(n.rssi) + " dBm");
    lines.push_back("SIGNAL: " + String(wifiScanNodeSignalBars(n.rssi)) + "/4 bars");
    lines.push_back("CHANNEL: " + String(n.channel));
    lines.push_back("AUTH: " + wifiScanNodeAuthName(n.authMode));
    lines.push_back("SECURITY: " + wifiScanNodeSecurityClass(n.authMode));
    lines.push_back(String("HIDDEN: ") + (n.hidden ? "yes" : "no"));
    lines.push_back(String("CONNECTED: ") + (n.connected ? "yes" : "no"));
    lines.push_back(String("SAVED PROFILE: ") + (n.savedProfile ? "yes" : "no"));
    lines.push_back("AUTH RAW: " + String(n.authMode));
    lines.push_back("SCAN INDEX: " + String(index + 1) + "/" + String((int)wifiScanNodeNetworks.size()));
}

static void wifiScanNodeScanProgress(int progress, const String &statusText) {
    drawProgressBar(progress, statusText, CP_CYAN);
}

static void wifiScanNodePerformScan() {
    wifiScanNodeNetworks.clear();
    wifiScanNodeNetworks.reserve(WIFI_SCAN_NODE_MAX_NETWORKS);
    wifiScanNodeFocus = 0;
    wifiScanNodeListScroll = 0;
    wifiScanNodeDetailMode = false;
    wifiScanNodeDetailScroll = 0;
    wifiScanNodeStatus = "SCANNING AP BEACONS";

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    WiFi.scanDelete();
    wifiScanNodeScanProgress(5, "WIFI RF PREP");
    delay(80);

    int started = WiFi.scanNetworks(true, true);
    if (started == WIFI_SCAN_FAILED) {
        wifiScanNodeStatus = "WIFI SCAN FAILED";
        return;
    }

    unsigned long startMs = millis();
    int result = WIFI_SCAN_RUNNING;
    while ((result = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && millis() - startMs < WIFI_SCAN_NODE_SCAN_TIMEOUT_MS) {
        int progress = 5 + (int)((millis() - startMs) * 85UL / WIFI_SCAN_NODE_SCAN_TIMEOUT_MS);
        if (progress > 90) progress = 90;
        wifiScanNodeScanProgress(progress, "WIFI AP SCAN");
        delay(60);
    }

    if (result == WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        wifiScanNodeStatus = "WIFI SCAN TIMEOUT";
        return;
    }
    if (result == WIFI_SCAN_FAILED || result < 0) {
        WiFi.scanDelete();
        wifiScanNodeStatus = "WIFI SCAN FAILED";
        return;
    }

    wifiScanNodeScanProgress(95, "WIFI AP PARSE");
    int count = result;
    for (int i = 0; i < count && (int)wifiScanNodeNetworks.size() < WIFI_SCAN_NODE_MAX_NETWORKS; i++) {
        WifiScanNodeInfo info;
        info.ssid = WiFi.SSID(i);
        info.hidden = (info.ssid == "");
        info.bssid = WiFi.BSSIDstr(i);
        info.rssi = WiFi.RSSI(i);
        info.channel = WiFi.channel(i);
        info.authMode = (int)WiFi.encryptionType(i);
        info.connected = (WiFi.status() == WL_CONNECTED && WiFi.BSSIDstr() == info.bssid);
        info.savedProfile = (savedSSID != "" && info.ssid == savedSSID);
        wifiScanNodeNetworks.push_back(info);
    }
    WiFi.scanDelete();

    std::sort(wifiScanNodeNetworks.begin(), wifiScanNodeNetworks.end(), [](const WifiScanNodeInfo &a, const WifiScanNodeInfo &b) {
        return a.rssi > b.rssi;
    });

    wifiScanNodeLastScanMs = millis();
    wifiScanNodeStatus = String((int)wifiScanNodeNetworks.size()) + " WIFI AP";
    if (count > (int)wifiScanNodeNetworks.size()) {
        wifiScanNodeStatus += " / " + String(count) + " FOUND";
    }
    wifiScanNodeScanProgress(100, "WIFI AP READY");
    delay(120);
}

void enterWifiScanNode() {
    appState = STATE_WIFI_SCANNER;
    wifiScanNodePerformScan();
    drawWifiScanNodeScreen();
}

void drawWifiScanNodeScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawGlitchText("WIFI", 72, 4, 1, wifiScanNodeDetailMode ? CP_YELLOW : CP_CYAN, true, true);
    drawTopStatusIcons(132, 1);
    canvas.drawLine(5, 18, 235, 18, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    String mode = wifiScanNodeDetailMode ? "ACCESS POINT INTEL" : "WIFI ACCESS POINT SCAN";
    canvas.setCursor(7, 22);
    canvas.print(mode);

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(7, 32);
    canvas.print(wifiScanNodeShort(wifiScanNodeStatus, 30));

    if (wifiScanNodeDetailMode) {
        std::vector<String> lines;
        wifiScanNodeBuildDetailLines(wifiScanNodeFocus, lines);
        int maxScroll = (int)lines.size() - WIFI_SCAN_NODE_DETAIL_VISIBLE_ROWS;
        if (maxScroll < 0) maxScroll = 0;
        if (wifiScanNodeDetailScroll > maxScroll) wifiScanNodeDetailScroll = maxScroll;
        if (wifiScanNodeDetailScroll < 0) wifiScanNodeDetailScroll = 0;

        int y = 44;
        for (int row = 0; row < WIFI_SCAN_NODE_DETAIL_VISIBLE_ROWS; row++) {
            int idx = wifiScanNodeDetailScroll + row;
            if (idx >= (int)lines.size()) break;
            wifiScanNodeDrawDetailLine(lines[idx], 7, y, 35);
            y += 11;
        }
        wifiScanNodeDrawVerticalScrollIndicator(wifiScanNodeDetailScroll, maxScroll, 229, 44, 78);

        canvas.setTextColor(CP_DIM);
        canvas.setCursor(7, 126);
        canvas.print("UP/DN SCROLL  < BACK  R RESCAN");
    } else {
        if (wifiScanNodeNetworks.empty()) {
            canvas.setTextColor(CP_RED);
            canvas.drawCenterString("NO WIFI ACCESS POINTS FOUND", 120, 61);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString("R RESCAN  ESC BACK", 120, 88);
        } else {
            wifiScanNodeEnsureVisible();
            int y = 45;
            int listMaxScroll = (int)wifiScanNodeNetworks.size() - WIFI_SCAN_NODE_LIST_VISIBLE_ROWS;
            if (listMaxScroll < 0) listMaxScroll = 0;
            for (int row = 0; row < WIFI_SCAN_NODE_LIST_VISIBLE_ROWS; row++) {
                int idx = wifiScanNodeListScroll + row;
                if (idx >= (int)wifiScanNodeNetworks.size()) break;
                const WifiScanNodeInfo &n = wifiScanNodeNetworks[idx];
                bool selected = idx == wifiScanNodeFocus;
                uint16_t color = selected ? CP_YELLOW : CP_DIM;
                drawChippedButton(6, y - 2, 216, 20, color);
                canvas.setTextColor(color);
                canvas.setTextSize(1);
                canvas.setCursor(13, y);
                canvas.print(String(idx + 1) + " " + wifiScanNodeShort(wifiScanNodeSsidLabel(n.ssid, n.hidden), 16));
                canvas.setCursor(150, y);
                canvas.print(String(n.rssi) + "dBm");
                canvas.setTextColor(selected ? CP_CYAN : CP_DIM);
                canvas.setCursor(13, y + 10);
                canvas.print(wifiScanNodeShort("CH" + String(n.channel) + " " + wifiScanNodeAuthName(n.authMode) + " " + n.bssid, 34));
                y += 23;
            }
            wifiScanNodeDrawVerticalScrollIndicator(wifiScanNodeListScroll, listMaxScroll, 229, 45, 78);
        }
    }

    pushCanvas();
}

void handleWifiScanNodeInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasBack = false, rescan = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c == '`') hasBack = true;
        if (c == 'r' || c == 'R') rescan = true;
    }

    if (rescan || (status.enter && wifiScanNodeNetworks.empty())) {
        playSound(sound_select, sound_select_size);
        wifiScanNodePerformScan();
        return;
    }

    if (hasBack || (!wifiScanNodeDetailMode && hasLeft)) {
        playSound(sound_select, sound_select_size);
        appState = STATE_MAIN_MENU;
        mainMenuFocus = 5;
        currentMenuScroll = mainMenuFocus;
        targetMenuScroll = mainMenuFocus;
        showMenuDesc = false;
        descAnimWidth = 0.0;
        drawMainMenu();
        return;
    }

    if (wifiScanNodeDetailMode) {
        std::vector<String> lines;
        wifiScanNodeBuildDetailLines(wifiScanNodeFocus, lines);
        int maxScroll = (int)lines.size() - WIFI_SCAN_NODE_DETAIL_VISIBLE_ROWS;
        if (maxScroll < 0) maxScroll = 0;
        if (hasLeft || status.enter) {
            playSound(sound_select, sound_select_size);
            wifiScanNodeDetailMode = false;
            wifiScanNodeDetailScroll = 0;
            return;
        }
        if (hasUp && wifiScanNodeDetailScroll > 0) {
            playSound(sound_hover, sound_hover_size);
            wifiScanNodeDetailScroll--;
        }
        if (hasDown && wifiScanNodeDetailScroll < maxScroll) {
            playSound(sound_hover, sound_hover_size);
            wifiScanNodeDetailScroll++;
        }
        return;
    }

    if (status.enter || hasRight) {
        if (!wifiScanNodeNetworks.empty()) {
            playSound(sound_select, sound_select_size);
            wifiScanNodeDetailMode = true;
            wifiScanNodeDetailScroll = 0;
        }
        return;
    }

    if (!wifiScanNodeNetworks.empty()) {
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            wifiScanNodeFocus--;
            if (wifiScanNodeFocus < 0) wifiScanNodeFocus = wifiScanNodeNetworks.size() - 1;
            wifiScanNodeEnsureVisible();
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            wifiScanNodeFocus++;
            if (wifiScanNodeFocus >= (int)wifiScanNodeNetworks.size()) wifiScanNodeFocus = 0;
            wifiScanNodeEnsureVisible();
        }
    }
}
