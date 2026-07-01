// OTA catalog browsing and firmware update flow.

void drawOtaCatalog() {
    if (otaDetailMode) {
        drawOtaFirmwareDetails();
        return;
    }

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- OTA FIRMWARE CATALOG ---", 120, 10);
    canvas.drawLine(10, 20, 230, 20, CP_CYAN);
    
    if (WiFi.status() != WL_CONNECTED) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("WIFI NOT CONNECTED", 120, 48);
        canvas.setTextColor(WHITE);
        canvas.drawCenterString("PRESS ENTER TO CONNECT", 120, 72);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("ESC: BACK", 120, 105);
        pushCanvas();
        return;
    }
    
    if (!otaCatalogLoaded) {
        pushCanvas(); // Show loading indicator
        if (!fetchOtaCatalog()) {
            canvas.fillScreen(CP_BG);
            canvas.setTextColor(CP_RED);
            canvas.drawCenterString("CATALOG FETCH FAILED", 120, 50);
            canvas.setTextColor(WHITE);
            canvas.drawCenterString("PRESS ESC TO GO BACK", 120, 75);
            pushCanvas();
            return;
        }
    }
    
    if (otaCatalog.empty()) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NO FIRMWARES FOUND", 120, 60);
        pushCanvas();
        return;
    }
    
    int startY = 24;
    int maxDisplay = 6;
    for (int i = 0; i < maxDisplay; i++) {
        int idx = otaCatalogScrollOffset + i;
        if (idx >= (int)otaCatalog.size()) break;
        
        bool isFocus = (idx == otaCatalogFocus);
        int rowY = startY + i * 13;
        
        if (isFocus) {
            canvas.fillRect(15, rowY, 210, 13, canvas.color565(30, 30, 30));
            canvas.drawRect(15, rowY, 210, 13, CP_YELLOW);
            canvas.setTextColor(CP_CYAN);
        } else {
            canvas.setTextColor(WHITE);
        }
        
        canvas.setCursor(20, rowY + 2);
        canvas.print(otaCatalog[idx].name);
    }
    
    canvas.drawLine(10, 105, 230, 105, CP_CYAN);
    canvas.setTextColor(WHITE);
    canvas.setCursor(12, 109);
    
    // Show only the "by [Author Name]" at the bottom
    String authorText = "by " + otaCatalog[otaCatalogFocus].author;
    if (authorText.length() > 36) authorText = authorText.substring(0, 33) + "...";
    canvas.print(authorText);
    
    pushCanvas();
}

void handleOtaCatalogInput(Keyboard_Class::KeysState status) {
    if (otaDetailMode) {
        bool hasEsc = false;
        for (char c : status.word) {
            if (c == '`') hasEsc = true;
        }
        
        if (hasEsc) {
            playSound(sound_select, sound_select_size);
            otaDetailMode = false;
            drawOtaCatalog();
            return;
        }
        
        bool hasUp = false, hasDown = false;
        for (char c : status.word) {
            if (c == ';') hasUp = true;
            if (c == '.') hasDown = true;
        }
        
        if (hasUp && !otaVersions.empty()) {
            playSound(sound_hover, sound_hover_size);
            otaVersionFocus = (otaVersionFocus - 1 + otaVersions.size()) % otaVersions.size();
            drawOtaFirmwareDetails();
        } else if (hasDown && !otaVersions.empty()) {
            playSound(sound_hover, sound_hover_size);
            otaVersionFocus = (otaVersionFocus + 1) % otaVersions.size();
            drawOtaFirmwareDetails();
        } else if (status.enter && !otaVersions.empty()) {
            playSound(sound_select, sound_select_size);
            String file = otaVersions[otaVersionFocus].file;
            String downloadUrl = "";
            if (file.startsWith("http")) {
                downloadUrl = file;
            } else {
                downloadUrl = "https://m5burner-cdn.m5stack.com/firmware/" + file;
            }
            performOtaUpdate(downloadUrl);
        }
        return;
    }

    bool hasEsc = false;
    for (char c : status.word) {
        if (c == '`') hasEsc = true;
    }
    
    if (hasEsc) {
        playSound(sound_select, sound_select_size);
        appState = STATE_SPLASH;
        showSplashBootMenu = true;
        splashBootFocus = 3;
        drawSplash();
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            startWifiScan();
            drawOtaCatalog();
        }
        return;
    }
    
    if (!otaCatalogLoaded) {
        return; // Wait for fetch
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp && !otaCatalog.empty()) {
        playSound(sound_hover, sound_hover_size);
        otaCatalogFocus = (otaCatalogFocus - 1 + otaCatalog.size()) % otaCatalog.size();
        if (otaCatalogFocus < otaCatalogScrollOffset) {
            otaCatalogScrollOffset = otaCatalogFocus;
        } else if (otaCatalogFocus >= otaCatalogScrollOffset + 6) {
            otaCatalogScrollOffset = otaCatalogFocus - 5;
        }
        drawOtaCatalog();
    } else if (hasDown && !otaCatalog.empty()) {
        playSound(sound_hover, sound_hover_size);
        otaCatalogFocus = (otaCatalogFocus + 1) % otaCatalog.size();
        if (otaCatalogFocus < otaCatalogScrollOffset) {
            otaCatalogScrollOffset = otaCatalogFocus;
        } else if (otaCatalogFocus >= otaCatalogScrollOffset + 6) {
            otaCatalogScrollOffset = otaCatalogFocus - 5;
        }
        drawOtaCatalog();
    } else if (status.enter && !otaCatalog.empty()) {
        playSound(sound_select, sound_select_size);
        if (fetchOtaFirmwareDetails(otaCatalog[otaCatalogFocus].fid)) {
            otaDetailMode = true;
            otaVersionFocus = 0;
            drawOtaFirmwareDetails();
        } else {
            canvas.fillScreen(CP_BG);
            canvas.setTextColor(CP_RED);
            canvas.setTextSize(1);
            canvas.drawCenterString("DETAILS FETCH FAILED", 120, 50);
            pushCanvas();
            delay(2000);
            drawOtaCatalog();
        }
    }
}

bool fetchOtaCatalog() {
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("FETCHING CATALOG DATABASE...", 120, 50);
    pushCanvas();
    
    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    
    String url = "https://api.launcherhub.net/firmwares?category=cardputer&order_by=" + otaSortField + "&page=1";
    
    if (http.begin(secureClient, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                otaCatalog.clear();
                // Target the "items" array nested inside the root metadata object!
                JsonArray array = doc["items"].as<JsonArray>();
                for (JsonVariant v : array) {
                    FirmwareCatalogItem item;
                    item.fid = v["fid"].as<String>();
                    item.name = v["name"].as<String>();
                    item.author = v["author"].as<String>();
                    item.version = "LATEST";
                    item.binUrl = "";
                    
                    item.desc = "by " + item.author;
                    if (item.desc.length() > 32) {
                        item.desc = item.desc.substring(0, 29) + "...";
                    }
                    
                    // Widen name limit now that author is not shown on the right side of the row
                    if (item.name.length() > 34) {
                        item.name = item.name.substring(0, 31) + "...";
                    }
                    if (item.author.length() > 20) {
                        item.author = item.author.substring(0, 17) + "...";
                    }
                    
                    otaCatalog.push_back(item);
                }
                otaCatalogLoaded = true;
                otaCatalogFocus = 0;
                otaCatalogScrollOffset = 0;
                http.end();
                return true;
            } else {
                canvas.fillScreen(CP_BG);
                canvas.setTextColor(CP_RED);
                canvas.drawCenterString("JSON PARSE ERROR", 120, 50);
                canvas.setTextColor(WHITE);
                canvas.drawCenterString(error.c_str(), 120, 75);
                pushCanvas();
                delay(3000);
            }
        } else {
            canvas.fillScreen(CP_BG);
            canvas.setTextColor(CP_RED);
            canvas.drawCenterString("HTTP ERROR: " + String(httpCode), 120, 50);
            pushCanvas();
            delay(3000);
        }
        http.end();
    }
    return false;
}

bool fetchOtaFirmwareDetails(String fid) {
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("FETCHING FIRMWARE DETAILS...", 120, 50);
    pushCanvas();
    
    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    String url = "https://api.launcherhub.net/firmwares?fid=" + fid;
    
    if (http.begin(secureClient, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                otaVersions.clear();
                otaDetailName = doc["name"].as<String>();
                otaDetailAuthor = doc["author"].as<String>();
                otaDetailStars = doc["star"].as<int>();
                
                otaDetailDesc = doc["description"].as<String>();
                if (otaDetailDesc == "null" || otaDetailDesc == "") {
                     otaDetailDesc = doc["desc"].as<String>();
                }
                if (otaDetailDesc == "null" || otaDetailDesc == "") {
                     otaDetailDesc = "No description provided.";
                }
                
                if (otaDetailDesc.length() > 60) {
                    otaDetailDesc = otaDetailDesc.substring(0, 57) + "...";
                }
                
                JsonArray versions = doc["versions"].as<JsonArray>();
                for (JsonVariant v : versions) {
                    FirmwareVersionItem ver;
                    ver.version = v["version"].as<String>();
                    ver.file = v["file"].as<String>();
                    ver.publishedAt = v["published_at"].as<String>();
                    otaVersions.push_back(ver);
                }
                http.end();
                return true;
            }
        }
        http.end();
    }
    return false;
}

void drawOtaFirmwareDetails() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    
    String displayName = otaDetailName;
    if (displayName.length() > 22) displayName = displayName.substring(0, 19) + "...";
    canvas.drawCenterString("--- " + displayName + " ---", 120, 10);
    canvas.drawLine(10, 20, 230, 20, CP_CYAN);
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(12, 24);
    canvas.print("Author: " + otaDetailAuthor);
    
    canvas.setCursor(170, 24);
    canvas.setTextColor(CP_CYAN);
    canvas.print("Stars: " + String(otaDetailStars));
    
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(12, 36);
    canvas.print(otaDetailDesc);
    
    canvas.drawLine(10, 48, 230, 48, CP_CYAN);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("SELECT VERSION", 120, 52);
    
    int startY = 64;
    int maxVerDisplay = 2; 
    int verOffset = 0;
    if (otaVersionFocus >= 2) verOffset = otaVersionFocus - 1;
    
    for (int i = 0; i < maxVerDisplay; i++) {
        int idx = verOffset + i;
        if (idx >= (int)otaVersions.size()) break;
        
        bool isFocus = (idx == otaVersionFocus);
        int rowY = startY + i * 14;
        
        if (isFocus) {
            canvas.fillRect(15, rowY, 210, 13, canvas.color565(30, 30, 30));
            canvas.drawRect(15, rowY, 210, 13, CP_YELLOW);
            canvas.setTextColor(CP_CYAN);
        } else {
            canvas.setTextColor(WHITE);
        }
        
        canvas.setCursor(20, rowY + 2);
        canvas.print(otaVersions[idx].version);
        
        canvas.setCursor(130, rowY + 2);
        canvas.setTextColor(isFocus ? CP_YELLOW : CP_DIM);
        canvas.print(otaVersions[idx].publishedAt);
    }
    
    canvas.drawLine(10, 95, 230, 95, CP_CYAN);
    
    pushCanvas();
}

void performOtaUpdate(String binUrl) {
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("CONNECTING SECURE ENDPOINT...", 120, 40);
    pushCanvas();
    
    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    
    if (http.begin(secureClient, binUrl)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            if (contentLength <= 0) {
                contentLength = 3145728; // fallback to partition max size
            }
            
            bool canBegin = Update.begin(contentLength);
            if (canBegin) {
                WiFiClient* stream = http.getStreamPtr();
                size_t written = 0;
                uint8_t buff[2048] = { 0 };
                unsigned long lastUpdate = 0;
                
                while (http.connected() && (written < contentLength)) {
                    size_t size = stream->available();
                    if (size) {
                        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                        Update.write(buff, c);
                        written += c;
                        
                        unsigned long currentMillis = millis();
                        if (currentMillis - lastUpdate > 250) {
                            int progress = (written * 100) / contentLength;
                            if (progress > 100) progress = 100;
                            drawProgressBar(progress, "FLASHING OTA: " + String(progress) + "%", CP_CYAN);
                            lastUpdate = currentMillis;
                        }
                    }
                    delay(1);
                }
                
                if (Update.end()) {
                    if (Update.isFinished()) {
                        canvas.fillScreen(CP_BG);
                        canvas.setTextColor(CP_CYAN);
                        canvas.setTextSize(2);
                        canvas.drawCenterString("FLASH COMPLETE!", 120, 50);
                        canvas.setTextSize(1);
                        canvas.setTextColor(CP_YELLOW);
                        canvas.drawCenterString("REBOOTING SYSTEM...", 120, 80);
                        pushCanvas();
                        delay(2000);
                        ESP.restart();
                    }
                }
            } else {
                canvas.fillScreen(CP_BG);
                canvas.setTextColor(CP_RED);
                canvas.setTextSize(1);
                canvas.drawCenterString("PARTITION ERROR", 120, 50);
                canvas.drawCenterString("SIZE EXCEEDS LIMIT", 120, 70);
                pushCanvas();
                delay(3000);
            }
        } else {
            canvas.fillScreen(CP_BG);
            canvas.setTextColor(CP_RED);
            canvas.setTextSize(1);
            canvas.drawCenterString("HTTP ERROR: " + String(httpCode), 120, 50);
            pushCanvas();
            delay(3000);
        }
        http.end();
    } else {
        canvas.fillScreen(CP_BG);
        canvas.setTextColor(CP_RED);
        canvas.setTextSize(1);
        canvas.drawCenterString("CONNECT FAILED", 120, 50);
        pushCanvas();
        delay(3000);
    }
    drawOtaCatalog();
}
