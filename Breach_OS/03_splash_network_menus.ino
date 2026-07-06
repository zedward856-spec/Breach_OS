// Splash/boot target UI, Wi-Fi/auth flows, leaderboard networking, and main menus.

void drawChippedButton(int x, int y, int w, int h, uint16_t color) {
    int chip = (h > 25) ? 8 : 5;
    if (w <= chip) return;
    canvas.drawLine(x, y, x + w, y, color);
    canvas.drawLine(x, y, x, y + h, color);
    canvas.drawLine(x, y + h, x + w - chip, y + h, color);
    canvas.drawLine(x + w, y, x + w, y + h - chip, color);
    canvas.drawLine(x + w, y + h - chip, x + w - chip, y + h, color);
}

void resetSplashBootScroll() {
    currentSplashBootScroll = splashBootFocus;
    targetSplashBootScroll = splashBootFocus;
}

void resetBreachModeScroll() {
    currentBreachScroll = breachModeFocus;
    targetBreachScroll = breachModeFocus;
}

void returnToBreachMode() {
    launchBreachAfterAuth = false;
    launchAccountAfterAuth = false;
    appState = STATE_BREACH_MODE;
    resetBreachModeScroll();
    drawBreachModePrompt();
}

void returnToBootBreach() {
    appState = STATE_SPLASH;
    showSplashBootMenu = true;
    splashBootFocus = 0;
    resetSplashBootScroll();
    drawSplash();
}

void startBreachGridSelect() {
    appState = STATE_GRID_SELECT;
    gridMenuFocus = 0;
    currentGridScroll = 0;
    targetGridScroll = 0;
    drawGridSelect();
}

void startOfflineBreach() {
    resumeOtaAfterWifi = false;
    resumeTextfilesAfterWifi = false;
    launchBreachAfterAuth = false;
    launchAccountAfterAuth = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    isGuest = true;
    authUser = "GUEST";
    startBreachGridSelect();
}

void startOnlineBreach() {
    resumeOtaAfterWifi = false;
    resumeTextfilesAfterWifi = false;
    launchBreachAfterAuth = true;
    launchAccountAfterAuth = false;
    if (WiFi.status() == WL_CONNECTED) {
        appState = STATE_AUTH_MENU;
        drawAuthMenu();
    } else {
        startWifiScan();
    }
}

void enterBreachAccount() {
    accountReturnToBreach = true;
    appState = STATE_ACCOUNT;
    accountFocus = 0;
    accountStatsFetched = false;
    drawAccountMenu();
}

void openBreachAccount() {
    resumeOtaAfterWifi = false;
    resumeTextfilesAfterWifi = false;
    launchBreachAfterAuth = false;
    if (WiFi.status() == WL_CONNECTED && !isGuest && authUser != "") {
        launchAccountAfterAuth = false;
        enterBreachAccount();
        return;
    }
    launchAccountAfterAuth = true;
    if (WiFi.status() == WL_CONNECTED) {
        appState = STATE_AUTH_MENU;
        drawAuthMenu();
    } else {
        startWifiScan();
    }
}

void openBreachLeaderboard() {
    leaderboardReturnToBreach = true;
    appState = STATE_LEADERBOARD;
    drawMessage("FETCHING DATABANK...");
    fetchLeaderboard(0, 10);
    leaderboardCursor = 0;
    leaderboardScrollOffset = 0;
    drawLeaderboard();
}

void drawBreachModePrompt() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);

    drawGlitchText("BREACH MODE", 72, 4, 1, CP_CYAN, true, true);

    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);

    int totalItems = 5;
    const char* labels[5] = {"ONLINE", "OFFLINE", "ACCOUNT", "LEADERBOARD", "BACK"};
    for (int i = 0; i < totalItems; i++) {
        float rawOffset = i - currentBreachScroll;
        float offset = fmod(rawOffset, (float)totalItems);
        float halfItems = (float)totalItems / 2.0;
        if (offset > halfItems) offset -= (float)totalItems;
        if (offset < -halfItems) offset += (float)totalItems;

        if (abs(offset) > 1.5) continue;

        float angle = offset * 0.391;
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;

        bool selected = (i == breachModeFocus);
        uint16_t tColor = selected ? CP_CYAN : CP_DIM;
        float tickEndX = -80 + cos(angle) * (selected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (selected ? 117 : 115);

        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
        if (selected) canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);

        float scale = 1.0 - abs(offset) * 0.3333;
        if (scale < 0.1) scale = 0.1;
        float h = 30.0 * scale;
        float y = tickY - h / 2.0;
        float w = 195.0 * scale;
        float x = tickX + 10;
        uint16_t color = selected ? CP_YELLOW : CP_DIM;

        drawChippedButton(x, y, w, h, color);
        canvas.setTextColor(color);
        canvas.setTextSize(selected ? 2 : 1);
        canvas.setCursor(x + 15, y + (selected ? 7 : 6));
        canvas.print(labels[i]);
    }

    drawWheelPositionIndicator(currentBreachScroll, totalItems);

    pushCanvas();
}

void handleBreachModeInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasEsc = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c == '`') hasEsc = true;
    }

    if (hasEsc) {
        playSound(sound_select, sound_select_size);
        returnToBootBreach();
        return;
    }
    if (hasUp || hasLeft) {
        playSound(sound_hover, sound_hover_size);
        breachModeFocus--;
        if (breachModeFocus < 0) breachModeFocus = 4;
        targetBreachScroll -= 1.0;
        drawBreachModePrompt();
        return;
    }
    if (hasDown || hasRight) {
        playSound(sound_hover, sound_hover_size);
        breachModeFocus++;
        if (breachModeFocus > 4) breachModeFocus = 0;
        targetBreachScroll += 1.0;
        drawBreachModePrompt();
        return;
    }
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (breachModeFocus == 0) startOnlineBreach();
        else if (breachModeFocus == 1) startOfflineBreach();
        else if (breachModeFocus == 2) openBreachAccount();
        else if (breachModeFocus == 3) openBreachLeaderboard();
        else returnToBootBreach();
    }
}

void drawWheelPositionArcBar(int cx, int cy, int radius, float startAngle, float endAngle, uint16_t color) {
    int lastX = cx + cos(startAngle) * radius;
    int lastY = cy + sin(startAngle) * radius;

    for (int i = 1; i <= 18; i++) {
        float t = (float)i / 18.0;
        float angle = startAngle + (endAngle - startAngle) * t;
        int x = cx + cos(angle) * radius;
        int y = cy + sin(angle) * radius;
        canvas.drawLine(lastX, lastY, x, y, color);
        lastX = x;
        lastY = y;
    }
}

void drawWheelPositionArcEndCap(int cx, int cy, int innerR, int outerR, float angle, bool forward, uint16_t color) {
    float midR = (innerR + outerR) / 2.0;
    float capR = (outerR - innerR) / 2.0;
    float ux = cos(angle);
    float uy = sin(angle);
    float tx = -sin(angle);
    float ty = cos(angle);
    if (!forward) {
        tx = -tx;
        ty = -ty;
    }

    int lastX = cx + ux * outerR;
    int lastY = cy + uy * outerR;
    for (int i = 1; i <= 8; i++) {
        float t = PI * (float)i / 8.0;
        float radial = cos(t) * capR;
        float tangent = sin(t) * capR;
        int x = cx + ux * midR + ux * radial + tx * tangent;
        int y = cy + uy * midR + uy * radial + ty * tangent;
        canvas.drawLine(lastX, lastY, x, y, color);
        lastX = x;
        lastY = y;
    }
}

void drawWheelPositionIndicator(float scroll, int totalItems) {
    const int cx = -80;
    const int cy = 67;
    const int outerR = 95;
    const int innerR = 89;
    const float startAngle = -0.40;
    const float endAngle = 0.40;

    drawWheelPositionArcBar(cx, cy, outerR, startAngle, endAngle, CP_DIM);
    drawWheelPositionArcBar(cx, cy, innerR, startAngle, endAngle, CP_DIM);
    drawWheelPositionArcEndCap(cx, cy, innerR, outerR, startAngle, false, CP_DIM);
    drawWheelPositionArcEndCap(cx, cy, innerR, outerR, endAngle, true, CP_DIM);

    if (totalItems <= 1) return;

    float wrapped = fmod(scroll, (float)totalItems);
    if (wrapped < 0.0) wrapped += (float)totalItems;

    float lastIndex = (float)(totalItems - 1);
    float pos = wrapped;
    if (wrapped > lastIndex) {
        pos = lastIndex * ((float)totalItems - wrapped);
    }
    float ratio = pos / (float)(totalItems - 1);
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;

    float angle = startAngle + ratio * (endAngle - startAngle);
    int dotR = (outerR + innerR) / 2;
    int dotX = cx + (int)(cos(angle) * dotR);
    int dotY = cy + (int)(sin(angle) * dotR);

    canvas.fillCircle(dotX, dotY, 5, CP_CYAN);
}

void drawSplash() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    drawDefaultGlitchText("Breach_OS", 120, 5, 2, CP_CYAN, true);
    
    int maxLogs = 7;
    int y = 35;
    canvas.setTextColor(CP_ACTIVE_LINE);
    canvas.setTextSize(1);
    for (int i = 0; i < maxLogs; i++) {
        int logIdx = (logOffset + i) % dummyLogs.size();
        canvas.setCursor(5, y);
        canvas.print(dummyLogs[logIdx]);
        y += 11;
    }

    canvas.setTextSize(1);
    String wifiStatusText = (WiFi.status() == WL_CONNECTED) ? "WIFI: CONNECTED" : "WIFI: CONNECTING";
    String versionText = "v1.3";
    String statusGap = "  ";
    int statusX = (240 - (canvas.textWidth(wifiStatusText) + canvas.textWidth(statusGap) + canvas.textWidth(versionText))) / 2;
    if (WiFi.status() == WL_CONNECTED) {
        canvas.setTextColor(CP_YELLOW);
    } else {
        canvas.setTextColor(CP_DIM);
    }
    canvas.drawString(wifiStatusText, statusX, 24);

    canvas.setTextColor(CP_YELLOW);
    canvas.drawString(versionText, statusX + canvas.textWidth(wifiStatusText) + canvas.textWidth(statusGap), 24);
    
    canvas.setTextSize(1);
    canvas.setTextColor(WHITE);
    
    if (showSplashBootMenu) {
        canvas.fillScreen(CP_BG);

        drawGlitchText("SELECT BOOT NODE", 72, 4, 1, CP_CYAN, true, true);

        canvas.drawCircle(-80, 67, 110, CP_DIM);
        canvas.drawCircle(-80, 67, 109, CP_DIM);

        int totalItems = 4;
        std::vector<String> options = {"BREACH", "HARDWARE NODE", "NETWORK NODE", "SYSTEM SETTINGS"};
        for (int i = 0; i < totalItems; i++) {
            float rawOffset = i - currentSplashBootScroll;
            float offset = fmod(rawOffset, (float)totalItems);
            float halfItems = (float)totalItems / 2.0;
            if (offset > halfItems) offset -= (float)totalItems;
            if (offset < -halfItems) offset += (float)totalItems;

            if (abs(offset) > 1.5) continue;

            float angle = offset * 0.391;
            float tickY = 67 + sin(angle) * 110;
            float tickX = -80 + cos(angle) * 110;

            bool isSelected = (i == splashBootFocus);
            uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;

            float tickEndX = -80 + cos(angle) * (isSelected ? 117 : 115);
            float tickEndY = 67 + sin(angle) * (isSelected ? 117 : 115);

            canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
            canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
            if (isSelected) {
                canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);
            }

            float scale = 1.0 - abs(offset) * 0.3333;
            if (scale < 0.1) scale = 0.1;
            float h = 30.0 * scale;
            float y = tickY - h / 2.0;
            float w = 195.0 * scale;
            float x = tickX + 10;

            int textSize = isSelected ? 2 : 1;
            uint16_t color = isSelected ? CP_YELLOW : CP_DIM;

            drawChippedButton(x, y, w, h, color);

            canvas.setTextColor(color);
            canvas.setTextSize(textSize);

            float textY = y + (isSelected ? 7 : 6);
            float textX = x + 15;
            canvas.setCursor(textX, textY);
            canvas.print(options[i]);
        }

        drawWheelPositionIndicator(currentSplashBootScroll, totalItems);
    } else {
        canvas.drawString("> Press ", 10, 115);
        int x1 = 10 + canvas.textWidth("> Press ");
        drawGlitchText("ENTER", x1, 115, 1, CP_YELLOW, false, true);
        int x2 = x1 + canvas.textWidth("ENTER");
        canvas.setTextColor(WHITE);
        canvas.drawString(" to Select Boot Target", x2, 115);
    }
    
    pushCanvas();
}

void handleSplashInput(Keyboard_Class::KeysState status) {
    if (!showSplashBootMenu) {
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            showSplashBootMenu = true;
            splashBootFocus = 0;
            resetSplashBootScroll();
            drawSplash();
        }
        return;
    }
    
    bool hasUp = false, hasDown = false;
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '`') hasEsc = true;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        splashBootFocus = (splashBootFocus - 1 + 4) % 4;
        targetSplashBootScroll -= 1.0;
        drawSplash();
    } else if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        splashBootFocus = (splashBootFocus + 1) % 4;
        targetSplashBootScroll += 1.0;
        drawSplash();
    } else if (hasEsc) {
        playSound(sound_select, sound_select_size);
        showSplashBootMenu = false;
        drawSplash();
    } else if (status.enter) {
        playSound(sound_select, sound_select_size);
        showSplashBootMenu = false;
        
        if (splashBootFocus == 0) {
            breachModeFocus = 0;
            resetBreachModeScroll();
            appState = STATE_BREACH_MODE;
            drawBreachModePrompt();
        } else if (splashBootFocus == 1) {
            appState = STATE_HARDWARE_MENU;
            hardwareMenuFocus = 0;
            currentHardwareScroll = 0;
            targetHardwareScroll = 0;
            showHardwareDesc = false;
            hardwareDescAnimWidth = 0.0;
            drawHardwareMenu();
        } else if (splashBootFocus == 2) {
            launchBreachAfterAuth = false;
            if (WiFi.status() == WL_CONNECTED) {
                appState = STATE_AUTH_MENU;
                drawAuthMenu();
            } else {
                startWifiScan();
            }
        } else if (splashBootFocus == 3) {
            appState = STATE_HARDWARE_SETTINGS;
            settingsFocus = 0;
            drawHardwareSettings();
        }
    }
}

void startWifiScan() {
    if (savedSSID != "" && savedWifiPass != "") {
        WiFi.begin(savedSSID.c_str(), savedWifiPass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40) {
            int progress = (attempts * 100) / 40;
            drawProgressBar(progress, "CONNECTING TO LINK...", CP_CYAN);
            delay(100);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            for (int p = ((attempts * 100) / 40); p <= 100; p += 10) {
                drawProgressBar(p, "LINK ONLINE!", CP_GREEN);
                delay(20);
            }
            drawProgressBar(100, "LINK ONLINE!", CP_GREEN);
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            delay(1000);
            if (resumeOtaAfterWifi) {
                resumeOtaAfterWifi = false;
                enterOtaCatalog();
            } else if (resumeTextfilesAfterWifi) {
                resumeTextfilesAfterWifi = false;
                enterTextfilesMode();
            } else if (launchAccountAfterAuth) {
                appState = STATE_AUTH_MENU;
                drawAuthMenu();
            } else if (launchBreachAfterAuth) {
                appState = STATE_AUTH_MENU;
                drawAuthMenu();
            } else {
                enterMainMenu();
            }
            return;
        }
    }

    drawMessage("SCANNING WIFI...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    wifiList.clear();
    for (int i = 0; i < n && i < 10; ++i) {
        wifiList.push_back(WiFi.SSID(i));
    }
    appState = STATE_WIFI_SCAN;
    wifiSelection = 0;
    wifiScrollOffset = 0;
    drawWifiScan();
}

void submitScore(int scoreToSubmit) {
    String key = isGuest ? ("g_hs_" + String(selectedGridSize)) : (authUser + "_hs_" + String(selectedGridSize));
    int currentHs = prefs.getInt(key.c_str(), 0);
    if (scoreToSubmit > currentHs) {
        prefs.putInt(key.c_str(), scoreToSubmit);
    }
    
    if (WiFi.status() == WL_CONNECTED && !isGuest) {
        if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
        HTTPClient http;
        String url = String(API_URL) + "/leaderboard";
        if (url.startsWith("https")) http.begin(secureClient, url);
        else http.begin(url);
        
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"username\":\"" + authUser + "\",\"score\":" + String(scoreToSubmit) + ",\"grid\":" + String(selectedGridSize) + ",\"phase\":" + String(currentPhase) + "}";
        http.POST(payload);
        http.end();
    }
}

void fetchLeaderboard(int offset, int limit) {
    if (offset == 0) {
        globalLeaderboard.clear();
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
        HTTPClient http;
        String url = String(API_URL) + "/leaderboard?offset=" + String(offset) + "&limit=" + String(limit);
        if (url.startsWith("https")) http.begin(secureClient, url);
        else http.begin(url);
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                totalLeaderboardSize = doc["total"].as<int>();
                JsonArray array = doc["data"].as<JsonArray>();
                for (JsonVariant v : array) {
                    LeaderboardEntry entry;
                    entry.username = v["username"].as<String>();
                    entry.score = v["score"].as<int>();
                    entry.grid = v["grid"].as<int>();
                    entry.phase = v["phase"].as<int>();
                    entry.date = v["date"].as<String>();
                    globalLeaderboard.push_back(entry);
                }
            }
        }
        http.end();
    }
}

void drawAuthMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(10, 10);
    canvas.print("ACCOUNT NAME:");
    
    uint16_t colorUser = (authFocus == 0) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 25, 220, 20, colorUser);
    canvas.setTextColor(colorUser);
    canvas.setCursor(15, 30);
    canvas.print(authUser + ((authFocus == 0 && blinkState) ? "_" : ""));

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(10, 55);
    canvas.print("PASSWORD:");

    uint16_t colorPass = (authFocus == 1) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 70, 220, 20, colorPass);
    canvas.setTextColor(colorPass);
    canvas.setCursor(15, 75);
    String starPass = "";
    for(int i=0; i<authPass.length(); i++) starPass += "*";
    canvas.print(starPass + ((authFocus == 1 && blinkState) ? "_" : ""));

    uint16_t colorRem = (authFocus == 2) ? CP_YELLOW : WHITE;
    canvas.setTextColor(colorRem);
    canvas.setCursor(10, 95);
    canvas.print(rememberMe ? "[X] REMEMBER ME" : "[ ] REMEMBER ME");

    uint16_t colorBtn1 = (authFocus == 3) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 110, 100, 20, colorBtn1);
    canvas.setTextColor(colorBtn1);
    drawGlitchText("LOGIN", 60, 115, 1, colorBtn1);
    
    uint16_t colorBtn2 = (authFocus == 4) ? CP_YELLOW : WHITE;
    drawChippedButton(130, 110, 100, 20, colorBtn2);
    canvas.setTextColor(colorBtn2);
    drawGlitchText("GUEST", 180, 115, 1, colorBtn2);
    pushCanvas();
}

void handleAuthInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (authFocus == 2) {
            rememberMe = !rememberMe;
            drawAuthMenu();
            return;
        } else if (authFocus == 3) {
            if (authUser == "") return;
            
            for (int p = 0; p <= 75; p += 5) {
                drawProgressBar(p, "DECRYPTING LINK SYSTEM...", CP_CYAN);
                delay(20);
            }
            
            if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
            HTTPClient http;
            String url = String(API_URL) + "/auth";
            if (url.startsWith("https")) http.begin(secureClient, url);
            else http.begin(url);
            
            http.addHeader("Content-Type", "application/json");
            String payload = "{\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\"}";
            int httpCode = http.POST(payload);
            
            if (httpCode == 200) {
                String response = http.getString();
                JsonDocument doc;
                deserializeJson(doc, response);
                String action = doc["action"].as<String>();
                String msg = (action == "signup") ? "OPERATIVE REGISTERED!" : "LOGIN SUCCESSFUL!";
                
                for (int p = 75; p <= 100; p += 5) {
                    drawProgressBar(p, msg, CP_GREEN);
                    delay(25);
                }
                drawProgressBar(100, msg, CP_GREEN);
                playSound(wifi_finished_wav, wifi_finished_wav_len);
                delay(1000);
                http.end();
                
                if (rememberMe) {
                    prefs.putString("user", authUser);
                    prefs.putString("pass", authPass);
                } else {
                    prefs.putString("user", "");
                    prefs.putString("pass", "");
                }
                
                isGuest = false;
                if (launchAccountAfterAuth) {
                    launchAccountAfterAuth = false;
                    enterBreachAccount();
                } else if (launchBreachAfterAuth) {
                    launchBreachAfterAuth = false;
                    startBreachGridSelect();
                } else {
                    enterMainMenu();
                }
            } else {
                http.end();
                playSound(sound_fail, sound_fail_size);
                drawProgressBar(100, "ACCESS DENIED", CP_RED);
                delay(2000);
                drawAuthMenu();
            }
            return;
        } else if (authFocus == 4) {
            isGuest = true;
            authUser = "GUEST";
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            if (launchAccountAfterAuth) {
                launchAccountAfterAuth = false;
                drawMessage("ACCOUNT NEEDS LOGIN");
                delay(1000);
                returnToBreachMode();
            } else if (launchBreachAfterAuth) {
                launchBreachAfterAuth = false;
                startBreachGridSelect();
            } else {
                enterMainMenu();
            }
            return;
        }
        authFocus++;
        if (authFocus > 4) authFocus = 0;
        return;
    }
    
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c >= 32 && c <= 126 && c != ';' && c != '.' && c != ',' && c != '/') {
            if (authFocus == 0 && authUser.length() < 16) authUser += c;
            if (authFocus == 1 && authPass.length() < 16) authPass += c;
        }
    }
    
    if (hasUp) { 
        playSound(sound_hover, sound_hover_size); 
        if (authFocus == 3 || authFocus == 4) authFocus = 2;
        else if (authFocus == 2) authFocus = 1;
        else if (authFocus == 1) authFocus = 0;
        else if (authFocus == 0) authFocus = 3; 
    }
    else if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        if (authFocus == 0) authFocus = 1;
        else if (authFocus == 1) authFocus = 2;
        else if (authFocus == 2) authFocus = 3;
        else if (authFocus == 3 || authFocus == 4) authFocus = 0;
    }
    else if (hasLeft || hasRight) {
        if (authFocus == 3) { authFocus = 4; playSound(sound_hover, sound_hover_size); }
        else if (authFocus == 4) { authFocus = 3; playSound(sound_hover, sound_hover_size); }
    }
    
    if (status.del) {
        if (authFocus == 0 && authUser.length() > 0) authUser.remove(authUser.length()-1);
        if (authFocus == 1 && authPass.length() > 0) authPass.remove(authPass.length()-1);
    }
}

void drawWifiScan() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawString("--- WIFI TRANSCEIVER MODULE SCAN ---", 10, 5);
    canvas.drawString("SELECT NODE FOR BREACH INTRUSION  ", 10, 15);
    canvas.drawString("-----------------------------------", 10, 25);
    
    int displayCount = 8;
    int startIdx = wifiScrollOffset;
    int endIdx = min((int)wifiList.size(), startIdx + displayCount);
    
    for (int i = startIdx; i < endIdx; i++) {
        int displayRow = i - startIdx;
        int y = 35 + displayRow * 11;
        if (i == wifiSelection) {
            canvas.fillRect(5, y - 1, 230, 10, CP_CYAN);
            canvas.setTextColor(BLACK, CP_CYAN);
        } else {
            canvas.setTextColor(CP_CYAN, CP_BG);
        }
        canvas.setCursor(10, y);
        canvas.print("> " + wifiList[i]);
    }
    pushCanvas();
}

void handleWifiScanInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp && wifiSelection > 0) {
        wifiSelection--;
        playSound(sound_hover, sound_hover_size);
        if (wifiSelection < wifiScrollOffset) {
            wifiScrollOffset = wifiSelection;
        }
    }
    if (hasDown && wifiSelection < wifiList.size() - 1) {
        wifiSelection++;
        playSound(sound_hover, sound_hover_size);
        if (wifiSelection >= wifiScrollOffset + 8) {
            wifiScrollOffset = wifiSelection - 7;
        }
    }
    
    if (status.enter && wifiList.size() > 0) {
        playSound(sound_select, sound_select_size);
        appState = STATE_WIFI_PASS;
        if (wifiList[wifiSelection] == savedSSID) {
            wifiPass = savedWifiPass;
        } else {
            wifiPass = "";
        }
        drawWifiPass();
    }
}

void drawWifiPass() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(10, 10);
    canvas.print("NETWORK: " + wifiList[wifiSelection]);
    
    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(10, 40);
    canvas.print("ENTER PASSWORD:");
    
    drawChippedButton(10, 55, 220, 20, WHITE);
    canvas.setTextColor(WHITE);
    canvas.setCursor(15, 60);
    canvas.print(wifiPass + (blinkState ? "_" : ""));
    pushCanvas();
}

void handleWifiPassInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        WiFi.begin(wifiList[wifiSelection].c_str(), wifiPass.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 50) {
            int progress = (attempts * 100) / 50;
            drawProgressBar(progress, "CONNECTING TO LINK...", CP_CYAN);
            delay(100);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            for (int p = ((attempts * 100) / 50); p <= 100; p += 10) {
                drawProgressBar(p, "LINK ONLINE!", CP_GREEN);
                delay(20);
            }
            drawProgressBar(100, "LINK ONLINE!", CP_GREEN);
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            delay(1000);
            
            prefs.putString("wifi_ssid", wifiList[wifiSelection]);
            prefs.putString("wifi_pass", wifiPass);
            savedSSID = wifiList[wifiSelection];
            savedWifiPass = wifiPass;
            if (resumeOtaAfterWifi) {
                resumeOtaAfterWifi = false;
                enterOtaCatalog();
            } else if (resumeTextfilesAfterWifi) {
                resumeTextfilesAfterWifi = false;
                enterTextfilesMode();
            } else {
                appState = STATE_AUTH_MENU;
                drawAuthMenu();
            }
        } else {
            playSound(sound_fail, sound_fail_size);
            drawProgressBar(100, "LINK ERROR: OFFLINE", CP_RED);
            delay(2000);
            appState = STATE_WIFI_SCAN;
            drawWifiScan();
        }
        return;
    }
    
    if (status.del && wifiPass.length() > 0) wifiPass.remove(wifiPass.length()-1);
    
    for (char c : status.word) {
        if (c >= 32 && c <= 126 && wifiPass.length() < 32) wifiPass += c;
    }
}

void enterMainMenu() {
    appState = STATE_MAIN_MENU;
    mainMenuFocus = 0;
    currentMenuScroll = 0;
    targetMenuScroll = 0;
    showMenuDesc = false;
    descAnimWidth = 0.0;
    showBootMenu = false;
    drawMainMenu();
}

void drawMainMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Compact header stays above the wheel buttons.
    drawGlitchText("NETWORK NODE", 72, 4, 1, CP_CYAN, true, true);
    
    // Draw rotating wheel arc on the left
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    std::vector<String> labels = {"SSH", "TELNET BBS", "OTA CATALOG", "TEXTFILES", "AP MODE", "WIFI SCAN", "BLUETOOTH", "BACK"};
    int totalItems = labels.size();
    
    for (int i = 0; i < totalItems; i++) {
        // Calculate shortest wrapping distance for seamless infinite scroll
        float rawOffset = i - currentMenuScroll;
        float offset = fmod(rawOffset, (float)totalItems);
        float halfItems = (float)totalItems / 2.0;
        if (offset > halfItems) offset -= (float)totalItems;
        if (offset < -halfItems) offset += (float)totalItems;
        
        // Keep both scroll directions visible; clipping negative offsets hides the
        // newly selected row during upward travel and makes it flash into place.
        if (abs(offset) > 1.5) continue;
        
        // Calculate tick position on the arc
        float angle = offset * 0.391; // ~22.4 degrees in radians
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == mainMenuFocus);
        
        uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;
        
        // Draw outward-pointing rotated ticks
        float tickEndX = -80 + cos(angle) * (isSelected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (isSelected ? 117 : 115);
        
        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
        if (isSelected) {
            canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);
        }
        
        // Button dynamic properties
        float scale = 1.0 - abs(offset) * 0.3333;
        if (scale < 0.1) scale = 0.1;
        float h = 30.0 * scale;
        float y = tickY - h / 2.0;
        float w = 195.0 * scale;
        float x = tickX + 10;
        
        int textSize = isSelected ? 2 : 1;
        uint16_t color = isSelected ? CP_YELLOW : CP_DIM;
        if (i == totalItems - 1 && lastBreachFailed) {
            color = CP_RED;
        }
        
        drawChippedButton(x, y, w, h, color);
        
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
    }

    drawWheelPositionIndicator(currentMenuScroll, totalItems);

    if (descAnimWidth >= 10.0) {
        int x = 40;
        int y = 52; // selected button y (67 - 15)
        int h = 30;
        
        // Fill background to cover the original button
        canvas.fillRect(x, y, (int)descAnimWidth, h, CP_BG);
        
        // Draw the chipped button outline
        drawChippedButton(x, y, (int)descAnimWidth, h, CP_YELLOW);
        
        if (descAnimWidth > 160.0) {
            canvas.setTextColor(CP_YELLOW);
            String label = labels[mainMenuFocus];
            String line1 = "";
            String line2 = "";
            
            if (label == "SSH") {
                line1 = "Remote shell";
                line2 = "terminal";
            } else if (label == "TELNET BBS") {
                line1 = "Retro BBS";
                line2 = "dial-in";
            } else if (label == "OTA CATALOG") {
                line1 = "Firmware";
                line2 = "downloads";
            } else if (label == "TEXTFILES") {
                line1 = "BBS text";
                line2 = "archive";
            } else if (label == "AP MODE") {
                line1 = "Host WiFi";
                line2 = "access point";
            } else if (label == "WIFI SCAN") {
                line1 = "WiFi AP";
                line2 = "intelligence";
            } else if (label == "BLUETOOTH") {
                line1 = "BLE device";
                line2 = "intelligence";
            }
            
            if (line1 != "") {
                canvas.setTextSize(2);
                canvas.setCursor(x + 10, y + 0);
                canvas.print(line1);
                canvas.setCursor(x + 10, y + 14);
                canvas.print(line2);
            }
        }
    }
    
    pushCanvas();
}
 
void handleMainMenuInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    bool hasRight = false;
    bool hasLeft = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '/') hasRight = true;
        if (c == ',') hasLeft = true;
    }
    std::vector<String> labels = {"SSH", "TELNET BBS", "OTA CATALOG", "TEXTFILES", "AP MODE", "WIFI SCAN", "BLUETOOTH", "BACK"};
    int maxFocus = labels.size() - 1;
    if (mainMenuFocus < 0 || mainMenuFocus > maxFocus) {
        mainMenuFocus = 0;
        currentMenuScroll = 0;
        targetMenuScroll = 0;
    }
    
    if (showMenuDesc) {
        if (hasLeft || hasUp || hasDown) {
            playSound(sound_select, sound_select_size);
            showMenuDesc = false;
            return;
        }
    } else {
        if (hasRight && mainMenuFocus < maxFocus) {
            playSound(sound_select, sound_select_size);
            showMenuDesc = true;
            return;
        }
    }
 
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        showMenuDesc = false;
        descAnimWidth = 0.0;
        
        String selectedLabel = labels[mainMenuFocus];
        if (selectedLabel == "SSH") {
            prepareSshSetupPrompt();
            appState = STATE_SSH;
            drawSshScreen();
        } else if (selectedLabel == "TELNET BBS") {
            prepareTelnetBbsSetup();
            appState = STATE_TELNET_BBS;
            drawTelnetBbsScreen();
        } else if (selectedLabel == "OTA CATALOG") {
            resumeTextfilesAfterWifi = false;
            if (WiFi.status() == WL_CONNECTED) {
                enterOtaCatalog();
            } else {
                resumeOtaAfterWifi = true;
                startWifiScan();
            }
        } else if (selectedLabel == "TEXTFILES") {
            resumeOtaAfterWifi = false;
            if (WiFi.status() == WL_CONNECTED) {
                enterTextfilesMode();
            } else {
                resumeTextfilesAfterWifi = true;
                startWifiScan();
            }
        } else if (selectedLabel == "AP MODE") {
            resumeOtaAfterWifi = false;
            resumeTextfilesAfterWifi = false;
            enterApMode();
        } else if (selectedLabel == "BLUETOOTH") {
            resumeOtaAfterWifi = false;
            resumeTextfilesAfterWifi = false;
            enterBluetoothScan();
        } else if (selectedLabel == "WIFI SCAN") {
            resumeOtaAfterWifi = false;
            resumeTextfilesAfterWifi = false;
            enterWifiScanNode();
        } else if (selectedLabel == "BACK") {
            appState = STATE_SPLASH;
            showSplashBootMenu = true;
            splashBootFocus = 2;
            resetSplashBootScroll();
            logOffset = 0;
            drawSplash();
        }
        return;
    }
    
    if (!showMenuDesc) {
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            mainMenuFocus--;
            if (mainMenuFocus < 0) mainMenuFocus = maxFocus;
            targetMenuScroll -= 1.0;
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            mainMenuFocus++;
            if (mainMenuFocus > maxFocus) mainMenuFocus = 0;
            targetMenuScroll += 1.0;
        }
    }
}

void drawRotatedText(String text, int cx, int cy, uint16_t color) {
    int w = text.length() * 6;
    int h = 8;
    M5Canvas tSpr(&canvas);
    tSpr.createSprite(w, h);
    tSpr.fillSprite(TFT_BLACK); 
    tSpr.setTextColor(color);
    tSpr.setTextSize(1);
    tSpr.drawString(text, 0, 0);
    
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = tSpr.readPixel(x, y);
            if (p != TFT_BLACK) {
                int dx = x - w/2;
                int dy = y - h/2;
                int nx = cx - dy;
                int ny = cy + dx;
                canvas.drawPixel(nx, ny, p);
            }
        }
    }
    tSpr.deleteSprite();
}

