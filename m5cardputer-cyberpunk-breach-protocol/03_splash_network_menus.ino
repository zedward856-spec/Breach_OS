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

void drawSplash() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    drawGlitchText("Breach_Protocol", 120, 5, 2, CP_CYAN, true, true);
    
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
    canvas.setCursor(10, 24);
    if (WiFi.status() == WL_CONNECTED) {
        canvas.setTextColor(CP_YELLOW);
        canvas.print("WIFI: CONNECTED");
    } else {
        canvas.setTextColor(CP_DIM);
        canvas.print("WIFI: CONNECTING");
    }
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(116, 24);
    canvas.print("VERSION: v9.0split");
    
    canvas.setTextSize(1);
    canvas.setTextColor(WHITE);
    
    if (showSplashBootMenu) {
        canvas.fillRect(2, 33, 236, 94, canvas.color565(15, 15, 15));
        canvas.drawRect(2, 33, 236, 94, CP_CYAN);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("--- SELECT BOOT NODE ---", 120, 36);
        
        std::vector<String> options = {"HARDWARE NODE", "NETWORK NODE", "OFFLINE PLAY", "OTA CATALOG", "SYSTEM SETTINGS"};
        for (int i = 0; i < 5; i++) {
            bool isSelected = (i == splashBootFocus);
            canvas.setTextColor(isSelected ? CP_CYAN : CP_DIM);
            if (isSelected) {
                canvas.drawCenterString("> [ " + options[i] + " ] <", 120, 48 + i * 13);
            } else {
                canvas.drawCenterString(options[i], 120, 48 + i * 13);
            }
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("UP/DN: MOVE | ENTER: SELECT | ESC: BACK", 120, 115);
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
        splashBootFocus = (splashBootFocus - 1 + 5) % 5;
        drawSplash();
    } else if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        splashBootFocus = (splashBootFocus + 1) % 5;
        drawSplash();
    } else if (hasEsc) {
        playSound(sound_select, sound_select_size);
        showSplashBootMenu = false;
        drawSplash();
    } else if (status.enter) {
        playSound(sound_select, sound_select_size);
        showSplashBootMenu = false;
        
        if (splashBootFocus == 0) {
            appState = STATE_HARDWARE_MENU;
            hardwareMenuFocus = 0;
            currentHardwareScroll = 0;
            targetHardwareScroll = 0;
            showHardwareDesc = false;
            hardwareDescAnimWidth = 0.0;
            drawHardwareMenu();
        } else if (splashBootFocus == 1) {
            if (WiFi.status() == WL_CONNECTED) {
                appState = STATE_AUTH_MENU;
                drawAuthMenu();
            } else {
                startWifiScan();
            }
        } else if (splashBootFocus == 2) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            isGuest = true;
            authUser = "GUEST";
            enterMainMenu();
        } else if (splashBootFocus == 3) {
            resumeOtaAfterWifi = false;
            enterOtaCatalog();
        } else if (splashBootFocus == 4) {
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
    wifiList.push_back("[PLAY OFFLINE]");
    for (int i = 0; i < n && i < 9; ++i) {
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
                enterMainMenu();
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
            playSound(wifi_finished_wav, wifi_finished_wav_len);
            enterMainMenu();
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
        if (wifiList[wifiSelection] == "[PLAY OFFLINE]") {
            resumeOtaAfterWifi = false;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            isGuest = true;
            authUser = "GUEST";
            enterMainMenu();
            return;
        }
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
    
    // Compact header stays above the wheel buttons; username sits on the right.
    String headerUser = isGuest ? String("GUEST") : authUser;
    if (headerUser.length() > 10) headerUser = headerUser.substring(0, 9) + "~";
    drawGlitchText("NETWORK NODE", 72, 4, 1, CP_CYAN, true, true);
    drawGlitchText(headerUser, 188, 4, 1, CP_DIM, true, true);
    
    // Draw rotating wheel arc on the left
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    int totalItems = isGuest ? 3 : 5;
    std::vector<String> labels;
    if (isGuest) {
        labels = {"HACK", "CREDITS", "BACK"};
    } else {
        labels = {"HACK", "LEADERBOARD", "ACCOUNT", "CREDITS", "BACK"};
    }
    
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
            
            if (label == "HACK") {
                line1 = "Access subnet";
                line2 = "gateways";
            } else if (label == "LEADERBOARD") {
                line1 = "View global";
                line2 = "scores";
            } else if (label == "ACCOUNT") {
                line1 = "Operative";
                line2 = "profile";
            } else if (label == "CONTROLS") {
                line1 = "Keyboard";
                line2 = "bindings";
            } else if (label == "CREDITS") {
                line1 = "System";
                line2 = "developers";
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
    int maxFocus = isGuest ? 2 : 4;
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
        
        std::vector<String> labels;
        if (isGuest) {
            labels = {"HACK", "CREDITS", "BACK"};
        } else {
            labels = {"HACK", "LEADERBOARD", "ACCOUNT", "CREDITS", "BACK"};
        }
        
        String selectedLabel = labels[mainMenuFocus];
        if (selectedLabel == "HACK") {
            appState = STATE_GRID_SELECT;
            gridMenuFocus = 0;
            currentGridScroll = 0;
            targetGridScroll = 0;
            drawGridSelect();
        } else if (selectedLabel == "LEADERBOARD") {
            appState = STATE_LEADERBOARD;
            drawMessage("FETCHING DATABANK...");
            fetchLeaderboard(0, 10);
            leaderboardCursor = 0;
            leaderboardScrollOffset = 0;
            drawLeaderboard();
        } else if (selectedLabel == "ACCOUNT") {
            appState = STATE_ACCOUNT;
            accountFocus = 0;
            accountStatsFetched = false;
        } else if (selectedLabel == "CONTROLS") {
            appState = STATE_CONTROLS;
            drawControlsScreen();
        } else if (selectedLabel == "CREDITS") {
            appState = STATE_CREDITS;
            drawCreditsScreen();
        } else if (selectedLabel == "BACK") {
            appState = STATE_SPLASH;
            showSplashBootMenu = true;
            splashBootFocus = 1;
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

