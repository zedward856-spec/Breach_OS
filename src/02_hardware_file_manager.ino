// Hardware menu, settings, file manager, file actions, and direct file playback.

bool isSystemFile(String name) {
    String lower = name;
    lower.toLowerCase();
    if (lower == "system volume information") return true;
    if (lower.startsWith(".")) return true;
    return false;
}

bool deleteRecursive(String path) {
    if (isSDCardManager) {
        if (isSDFallback) return true;
        File dir = SD.open(path);
        if (!dir) return false;
        
        if (!dir.isDirectory()) {
            dir.close();
            return SD.remove(path);
        }
        
        std::vector<String> childPaths;
        std::vector<bool> childIsDir;
        
        File file = dir.openNextFile();
        while (file) {
            String childName = String(file.name());
            int lastSlash = childName.lastIndexOf('/');
            if (lastSlash >= 0) childName = childName.substring(lastSlash + 1);
            
            childPaths.push_back(path + (path.endsWith("/") ? "" : "/") + childName);
            childIsDir.push_back(file.isDirectory());
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
        
        for (size_t i = 0; i < childPaths.size(); i++) {
            if (childIsDir[i]) {
                deleteRecursive(childPaths[i]);
            } else {
                SD.remove(childPaths[i]);
            }
        }
        return SD.rmdir(path);
    } else {
        if (isFlashFallback) return true;
        File root = SPIFFS.open("/");
        if (!root) return false;
        
        std::vector<String> filesToDelete;
        File file = root.openNextFile();
        while (file) {
            String name = String(file.name());
            String matchPrefix = path;
            if (!matchPrefix.endsWith("/")) matchPrefix += "/";
            
            if (name.startsWith(matchPrefix) || name == path) {
                filesToDelete.push_back(name);
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        
        for (const String& f : filesToDelete) {
            SPIFFS.remove(f);
        }
        return true;
    }
}

bool compareFiles(const RealFile& a, const RealFile& b) {
    String aName = a.name;
    String bName = b.name;
    aName.toLowerCase();
    bName.toLowerCase();
    
    if (currentSortField == SORT_FIELD_NAME) {
        if (currentSortOrder == SORT_ORDER_ASC) {
            return aName < bName;
        } else {
            return aName > bName;
        }
    } else { // SORT_FIELD_TYPE
        if (a.isDir != b.isDir) {
            if (currentSortOrder == SORT_ORDER_ASC) {
                return a.isDir > b.isDir; // Directory first
            } else {
                return a.isDir < b.isDir; // Files first
            }
        }
        return aName < bName;
    }
}

void populateFileList() {
    loadedFiles.clear();
    fsStatusMessage = "";
    isSDFallback = false;
    isFlashFallback = false;
    
    // Insert parent directory ".." if we are in a subdirectory
    if (fileManagerCurrentPath != "/") {
        RealFile parentDir = {"..", "DIR", true};
        loadedFiles.push_back(parentDir);
    }
    
    if (isSDCardManager) {
        SPI.begin(40, 39, 14, 12);
        bool mountSuccess = SD.begin(12, SPI, 20000000);
        File root;
        if (mountSuccess) {
            root = SD.open(fileManagerCurrentPath);
        }
        
        if (!mountSuccess || !root || !root.isDirectory()) {
            isSDFallback = true;
            fsStatusMessage = "SD MOUNT FAIL (DEMO ACTIVE)";
            
            RealFile f1 = {"credentials.txt", "0.1 KB", false};
            RealFile f2 = {"subnet_node.sh", "0.1 KB", false};
            RealFile f3 = {"payload.bin", "1.2 KB", false};
            loadedFiles.push_back(f1);
            loadedFiles.push_back(f2);
            loadedFiles.push_back(f3);
            if (root) root.close();
        } else {
            File file = root.openNextFile();
            while (file && loadedFiles.size() < 100) {
                RealFile rf;
                rf.name = String(file.name());
                int lastSlashIdx = rf.name.lastIndexOf('/');
                if (lastSlashIdx >= 0) {
                    rf.name = rf.name.substring(lastSlashIdx + 1);
                }
                
                if (!showSystemFiles && isSystemFile(rf.name)) {
                    file = root.openNextFile();
                    continue;
                }
                
                rf.isDir = file.isDirectory();
                if (rf.isDir) {
                    rf.sizeStr = "DIR";
                } else {
                    rf.sizeStr = String(file.size() / 1024.0, 1) + " KB";
                }
                loadedFiles.push_back(rf);
                file = root.openNextFile();
            }
            root.close();
            // If the folder is empty (excluding ".." if present)
            int minSize = (fileManagerCurrentPath != "/") ? 1 : 0;
            if ((int)loadedFiles.size() <= minSize) {
                isSDFallback = true;
                fsStatusMessage = "SD CARD EMPTY (DEMO ACTIVE)";
                RealFile f1 = {"credentials.txt", "0.1 KB", false};
                RealFile f2 = {"subnet_node.sh", "0.1 KB", false};
                RealFile f3 = {"payload.bin", "1.2 KB", false};
                loadedFiles.push_back(f1);
                loadedFiles.push_back(f2);
                loadedFiles.push_back(f3);
            }
        }
    } else {
        bool mountSuccess = SPIFFS.begin(true);
        File root;
        if (mountSuccess) {
            root = SPIFFS.open("/");
        }
        
        if (!mountSuccess || !root) {
            isFlashFallback = true;
            fsStatusMessage = "FLASH MOUNT FAIL (DEMO ACTIVE)";
            RealFile f1 = {"deck_config.json", "0.1 KB", false};
            RealFile f2 = {"breach_log.txt", "0.1 KB", false};
            RealFile f3 = {"system.ini", "0.1 KB", false};
            loadedFiles.push_back(f1);
            loadedFiles.push_back(f2);
            loadedFiles.push_back(f3);
            if (root) root.close();
        } else {
            File file = root.openNextFile();
            while (file && loadedFiles.size() < 100) {
                RealFile rf;
                rf.name = String(file.name());
                if (rf.name.startsWith("/")) rf.name.remove(0, 1);
                
                if (!showSystemFiles && isSystemFile(rf.name)) {
                    file = root.openNextFile();
                    continue;
                }
                
                rf.isDir = file.isDirectory();
                if (rf.isDir) {
                    rf.sizeStr = "DIR";
                } else {
                    rf.sizeStr = String(file.size() / 1024.0, 1) + " KB";
                }
                loadedFiles.push_back(rf);
                file = root.openNextFile();
            }
            root.close();
            int minSize = (fileManagerCurrentPath != "/") ? 1 : 0;
            if ((int)loadedFiles.size() <= minSize) {
                isFlashFallback = true;
                fsStatusMessage = "FLASH EMPTY (DEMO ACTIVE)";
                RealFile f1 = {"deck_config.json", "0.1 KB", false};
                RealFile f2 = {"breach_log.txt", "0.1 KB", false};
                RealFile f3 = {"system.ini", "0.1 KB", false};
                loadedFiles.push_back(f1);
                loadedFiles.push_back(f2);
                loadedFiles.push_back(f3);
            }
        }
    }
    
    // Sort files based on settings (keeping ".." at the top if present)
    if (!loadedFiles.empty()) {
        int sortStart = (fileManagerCurrentPath != "/") ? 1 : 0;
        if ((int)loadedFiles.size() > sortStart) {
            std::sort(loadedFiles.begin() + sortStart, loadedFiles.end(), compareFiles);
        }
    }
    
    lastFileSelectionTime = millis();
    marqueeScrollOffset = 0;
}

void readSelectedFileContent(String fileName) {
    openedFileContent.clear();
    openedFileName = fileName;
    String path = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + fileName;
    
    if (isSDCardManager) {
        if (isSDFallback) {
            if (fileName == "credentials.txt") {
                openedFileContent.push_back("[system_credentials]");
                openedFileContent.push_back("admin:cYbErPuNk2077");
                openedFileContent.push_back("net_gate:ice_breaker");
            } else if (fileName == "subnet_node.sh") {
                openedFileContent.push_back("#!/bin/bash");
                openedFileContent.push_back("nmap -sS -O 10.0.2.15");
                openedFileContent.push_back("bypass_firewall --level 3");
            } else {
                openedFileContent.push_back("0xDEADBEEF 0xCAFEBABE");
                openedFileContent.push_back("0x00FF00FF 0x12345678");
                openedFileContent.push_back("VULNERABILITY DETECTED");
            }
            return;
        }
        
        SPI.begin(40, 39, 14, 12);
        if (!SD.begin(12, SPI, 20000000)) {
            openedFileContent.push_back("SD Card Mount Error");
            return;
        }
        File f = SD.open(path);
        if (!f) {
            openedFileContent.push_back("Could not open file.");
            return;
        }
        if (f.isDirectory()) {
            openedFileContent.push_back("Cannot read directory.");
            f.close();
            return;
        }
        
        int lineCount = 0;
        while (f.available() && lineCount < 4) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            if (line.length() > 25) line = line.substring(0, 25) + "...";
            openedFileContent.push_back(line);
            lineCount++;
        }
        f.close();
    } else {
        if (isFlashFallback) {
            if (fileName == "deck_config.json") {
                openedFileContent.push_back("{");
                openedFileContent.push_back("  \"deck_id\": \"CYBER_D_01\",");
                openedFileContent.push_back("  \"os\": \"cyber_os_8.0\"");
                openedFileContent.push_back("}");
            } else if (fileName == "breach_log.txt") {
                openedFileContent.push_back("BREACH ACCESS LOG:");
                openedFileContent.push_back("OP: sl01220");
                openedFileContent.push_back("STATUS: COMPLETED");
            } else {
                openedFileContent.push_back("[system]");
                openedFileContent.push_back("node=BP_X1");
                openedFileContent.push_back("security=HIGH");
            }
            return;
        }
        
        if (!SPIFFS.begin(true)) {
            openedFileContent.push_back("Flash Mount Error");
            return;
        }
        File f = SPIFFS.open(path);
        if (!f) {
            openedFileContent.push_back("Could not open file.");
            return;
        }
        if (f.isDirectory()) {
            openedFileContent.push_back("Cannot read directory.");
            f.close();
            return;
        }
        
        int lineCount = 0;
        while (f.available() && lineCount < 4) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            if (line.length() > 25) line = line.substring(0, 25) + "...";
            openedFileContent.push_back(line);
            lineCount++;
        }
        f.close();
    }
}

void drawHardwareMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw title
    drawGlitchText("HARDWARE NODE", 135, 12, 2, CP_CYAN, true, true);
    drawGlitchText("OPERATIVE: " + (isGuest ? String("GUEST") : authUser), 135, 34, 1, CP_DIM);
    
    // Draw rotating wheel arc
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    int totalItems = 4;
    std::vector<String> labels = {"FLASH MEMORY", "SD CARD", "MUSIC PLAYER", "BACK"};
    
    for (int i = 0; i < totalItems; i++) {
        float rawOffset = i - currentHardwareScroll;
        float offset = fmod(rawOffset, (float)totalItems);
        float halfItems = (float)totalItems / 2.0;
        if (offset > halfItems) offset -= (float)totalItems;
        if (offset < -halfItems) offset += (float)totalItems;
        
        if (offset < -0.1 || offset > 1.5) continue;
        
        float angle = offset * 0.391;
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == hardwareMenuFocus);
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
        
        canvas.drawRect(x, y, w, h, color);
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
    }
    
    if (hardwareDescAnimWidth >= 10.0) {
        int x = 40;
        int y = 52;
        int h = 30;
        canvas.fillRect(x, y, (int)hardwareDescAnimWidth, h, CP_BG);
        canvas.drawRect(x, y, (int)hardwareDescAnimWidth, h, CP_YELLOW);
        
        if (hardwareDescAnimWidth > 160.0) {
            canvas.setTextColor(CP_YELLOW);
            String label = labels[hardwareMenuFocus];
            String line1 = "";
            String line2 = "";
            if (label == "FLASH MEMORY") {
                line1 = "Flash";
                line2 = "memory";
            } else if (label == "SD CARD") {
                line1 = "SD card";
                line2 = "storage";
            } else if (label == "MUSIC PLAYER") {
                line1 = "Offline";
                line2 = "player";
            } else if (label == "BACK") {
                line1 = "Return to";
                line2 = "terminal";
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

void handleHardwareMenuInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    bool hasRight = false;
    bool hasLeft = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '/') hasRight = true;
        if (c == ',') hasLeft = true;
    }
    
    if (showHardwareDesc) {
        if (hasLeft || hasUp || hasDown) {
            playSound(sound_select, sound_select_size);
            showHardwareDesc = false;
            return;
        }
    } else {
        if (hasRight && (hardwareMenuFocus == 0 || hardwareMenuFocus == 1 || hardwareMenuFocus == 2)) {
            playSound(sound_select, sound_select_size);
            showHardwareDesc = true;
            return;
        }
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        showHardwareDesc = false;
        hardwareDescAnimWidth = 0.0;
        
        if (hardwareMenuFocus == 0) {
            isSDCardManager = false;
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            showFileContent = false;
        } else if (hardwareMenuFocus == 1) {
            isSDCardManager = true;
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            showFileContent = false;
        } else if (hardwareMenuFocus == 2) {
            appState = STATE_MUSIC_PLAYER;
            playlistFocus = 0;
            playlistScrollOffset = 0;
            populatePlaylist();
            drawMusicPlayer();
        } else if (hardwareMenuFocus == 3) {
            appState = STATE_SPLASH;
            drawSplash();
        }
        return;
    }
    
    if (!showHardwareDesc) {
        int maxFocus = 3;
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            hardwareMenuFocus--;
            if (hardwareMenuFocus < 0) hardwareMenuFocus = maxFocus;
            targetHardwareScroll -= 1.0;
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            hardwareMenuFocus++;
            if (hardwareMenuFocus > maxFocus) hardwareMenuFocus = 0;
            targetHardwareScroll += 1.0;
        }
    }
}

void drawHardwareSettings() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- SYSTEM CONFIG NODE ---", 120, 10);
    
    // Adjust settingsTabScrollOffset
    if (settingsTab < settingsTabScrollOffset) {
        settingsTabScrollOffset = settingsTab;
    } else if (settingsTab >= settingsTabScrollOffset + 3) {
        settingsTabScrollOffset = settingsTab - 2;
    }
    
    // Draw scrollable tabs
    int tabX[3] = {18, 86, 154};
    int tabW[3] = {64, 64, 64};
    String tabNames[5] = {"HARDWARE", "NETWORK", "OFFLINE", "  OTA  ", "APPEAR"};
    for (int t = 0; t < 3; t++) {
        int idx = settingsTabScrollOffset + t;
        if (idx >= 5) break;
        
        bool isActive = (settingsTab == idx);
        bool hasFocus = (settingsFocus == -1 && isActive);
        
        uint16_t borderCol = hasFocus ? CP_YELLOW : (isActive ? CP_CYAN : CP_DIM);
        canvas.drawRect(tabX[t], 22, tabW[t], 14, borderCol);
        if (hasFocus) {
            canvas.fillRect(tabX[t] + 1, 23, tabW[t] - 2, 12, canvas.color565(30, 30, 20));
        }
        canvas.setTextColor(isActive ? CP_YELLOW : WHITE);
        // Center text in smaller tabs: width of character size 1 is 6 pixels.
        // E.g. "NETWORK" (7 chars * 6 = 42 pixels), tab width 64 -> center offset (64 - 42) / 2 = 11 pixels!
        int textOffset = (64 - tabNames[idx].length() * 6) / 2;
        canvas.setCursor(tabX[t] + textOffset, 25);
        canvas.print(tabNames[idx]);
    }
    
    // Draw concealed indicators next to tabs on the sides
    if (settingsTabScrollOffset > 0) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawChar('<', 11, 25);
    }
    if (settingsTabScrollOffset < 2) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawChar('>', 221, 25);
    }
    
    // Determine rowCount
    int rowCount = 3;
    if (settingsTab == 1) rowCount = 2; // NETWORK: SSID, PASSWORD (scan nets removed)
    else if (settingsTab == 2) rowCount = 2; // OFFLINE: PLAY LOOP, MUSIC DIR
    else if (settingsTab == 3) rowCount = 1; // OTA: SORT BY ONLY (open list removed)
    else if (settingsTab == 4) rowCount = 3; // APPEARANCE: GLITCH TEXT, BRIGHTNESS, VOLUME
    
    int startY = 41;
    
    for (int i = 0; i < rowCount; i++) {
        bool isFocus = (settingsFocus == i);
        uint16_t borderCol = isFocus ? CP_YELLOW : CP_DIM;
        int rowY = startY + i * 17;
        
        canvas.fillRect(15, rowY, 210, 15, isFocus ? canvas.color565(30, 30, 30) : CP_BG);
        canvas.drawRect(15, rowY, 210, 15, borderCol);
        
        canvas.setTextColor(isFocus ? CP_YELLOW : WHITE);
        canvas.setCursor(22, rowY + 3);
        
        if (settingsTab == 0) { // HARDWARE
            if (i == 0) {
                canvas.print("SORT BY:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print(currentSortField == SORT_FIELD_NAME ? "< NAME >" : "< TYPE >");
            } else if (i == 1) {
                canvas.print("ORDER:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print(currentSortOrder == SORT_ORDER_ASC ? "< ASCENDING >" : "< DESCENDING >");
            } else if (i == 2) {
                canvas.print("SYS FILES:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print(showSystemFiles ? "< SHOW >" : "< HIDE >");
            }
        } else if (settingsTab == 1) { // NETWORK
            if (i == 0) {
                canvas.print("WIFI SSID:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String displaySsid = savedSSID == "" ? "< NONE >" : savedSSID;
                if (displaySsid.length() > 14) displaySsid = displaySsid.substring(0, 11) + "...";
                canvas.print(displaySsid);
            } else if (i == 1) {
                canvas.print("WIFI PASS:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String displayPass = savedWifiPass == "" ? "< NONE >" : "********";
                canvas.print(displayPass);
            }
        } else if (settingsTab == 2) { // OFFLINE
            if (i == 0) {
                canvas.print("PLAY LOOP:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                if (mp3PlayLoopMode == "random") canvas.print("< RANDOM >");
                else canvas.print("< BY NAME >");
            } else if (i == 1) {
                canvas.print("MUSIC DIR:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String displayDir = musicDir;
                if (displayDir.length() > 14) displayDir = displayDir.substring(0, 11) + "...";
                canvas.print(displayDir + " [SET]");
            }
        } else if (settingsTab == 3) { // OTA
            if (i == 0) {
                canvas.print("SORT BY:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                if (otaSortField == "downloads") canvas.print("< DOWNLOADS >");
                else if (otaSortField == "date") canvas.print("< TIME >");
                else canvas.print("< NAME >");
            }
        } else if (settingsTab == 4) { // APPEARANCE
            if (i == 0) {
                canvas.print("GLITCH TEXT:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String glitchLabel = "";
                if (insaneMode == 0) glitchLabel = "< OFF >";
                else if (insaneMode == 1) glitchLabel = "< ON >";
                else glitchLabel = "< INSANE >";
                canvas.print(glitchLabel);
            } else if (i == 1) {
                canvas.print("BRIGHTNESS:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< " + String(globalBrightness) + "% >");
            } else if (i == 2) {
                canvas.print("VOLUME:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< " + String(globalVolume) + "% >");
            }
        }
    }
    
    pushCanvas();
}

void handleHardwareSettingsInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_SPLASH;
        showSplashBootMenu = true;
        splashBootFocus = 4;
        logOffset = 0;
        drawSplash();
        return;
    }
    
    bool hasUp = false, hasDown = false;
    bool hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
    }
    
    int maxFocus = 2;
    if (settingsTab == 1) maxFocus = 1; // NETWORK: SSID, PASSWORD
    else if (settingsTab == 2) maxFocus = 1; // OFFLINE: PLAY LOOP, MUSIC DIR
    else if (settingsTab == 3) maxFocus = 0; // OTA: SORT BY
    else if (settingsTab == 4) maxFocus = 2; // APPEARANCE: GLITCH TEXT, BRIGHTNESS, VOLUME
    
    if (settingsFocus == -1) {
        if (hasLeft) {
            playSound(sound_hover, sound_hover_size);
            settingsTab = (settingsTab - 1 + 5) % 5;
            drawHardwareSettings();
        } else if (hasRight) {
            playSound(sound_hover, sound_hover_size);
            settingsTab = (settingsTab + 1) % 5;
            drawHardwareSettings();
        } else if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            settingsFocus = 0;
            drawHardwareSettings();
        }
        return;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        if (settingsFocus == 0) {
            settingsFocus = -1;
        } else {
            settingsFocus--;
        }
        drawHardwareSettings();
    } else if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        if (settingsFocus < maxFocus) {
            settingsFocus++;
            drawHardwareSettings();
        }
    }
    
    if (hasLeft || hasRight) {
        playSound(sound_hover, sound_hover_size);
        if (settingsTab == 0) { // HARDWARE
            if (settingsFocus == 0) {
                currentSortField = (currentSortField == SORT_FIELD_NAME) ? SORT_FIELD_TYPE : SORT_FIELD_NAME;
            } else if (settingsFocus == 1) {
                currentSortOrder = (currentSortOrder == SORT_ORDER_ASC) ? SORT_ORDER_DESC : SORT_ORDER_ASC;
            } else if (settingsFocus == 2) {
                showSystemFiles = !showSystemFiles;
            }
        } else if (settingsTab == 2) { // OFFLINE
            if (settingsFocus == 0) {
                if (mp3PlayLoopMode == "name") mp3PlayLoopMode = "random";
                else mp3PlayLoopMode = "name";
                prefs.putString("loopMode", mp3PlayLoopMode);
            }
        } else if (settingsTab == 3) { // OTA Sorting
            if (settingsFocus == 0) { // SORT BY is now row 0
                if (hasLeft) {
                    if (otaSortField == "downloads") otaSortField = "name";
                    else if (otaSortField == "date") otaSortField = "downloads";
                    else otaSortField = "date";
                } else {
                    if (otaSortField == "downloads") otaSortField = "date";
                    else if (otaSortField == "date") otaSortField = "name";
                    else otaSortField = "downloads";
                }
                otaCatalogLoaded = false; // refresh next fetch
            }
        } else if (settingsTab == 4) { // APPEARANCE
            if (settingsFocus == 0) {
                if (hasLeft) {
                    insaneMode = (insaneMode - 1 + 3) % 3;
                } else {
                    insaneMode = (insaneMode + 1) % 3;
                }
            } else if (settingsFocus == 1) {
                if (hasLeft) {
                    globalBrightness -= 5;
                    if (globalBrightness < 5) globalBrightness = 5;
                } else {
                    globalBrightness += 5;
                    if (globalBrightness > 100) globalBrightness = 100;
                }
                M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
                prefs.putInt("brightness", globalBrightness);
            } else if (settingsFocus == 2) {
                if (hasLeft) {
                    globalVolume -= 5;
                    if (globalVolume < 0) globalVolume = 0;
                } else {
                    globalVolume += 5;
                    if (globalVolume > 100) globalVolume = 100;
                }
                M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
                prefs.putInt("volume", globalVolume);
            }
        }
        drawHardwareSettings();
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (settingsTab == 2 && settingsFocus == 1) { // OFFLINE: MUSIC DIR -> choose folder location automatically
            isDirSelectionMode = true;
            isSDCardManager = true; // Hardcode to SD card browsing as requested
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            fileManagerCurrentPath = "/";
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            showFileContent = false;
        } else { // Exit to Splash screen (Boot Node)
            prefs.putInt("insane", insaneMode);
            populateFileList();
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            appState = STATE_SPLASH; // go directly to splash menu!
            showSplashBootMenu = true;
            splashBootFocus = 4;
            drawSplash();
        }
    }
}

void drawFileManager(bool push) {
    if (showImage) {
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        
        canvas.drawRect(5, 5, 230, 125, CP_CYAN);
        canvas.drawRect(7, 7, 226, 121, CP_DIM);
        
        String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + openedImageName;
        
        if (isSDCardManager) {
            canvas.drawJpgFile(SD, fullPath.c_str(), 8, 8, 224, 105, 0, 0, imageScale, imageScale);
        } else {
            canvas.drawJpgFile(SPIFFS, fullPath.c_str(), 8, 8, 224, 105, 0, 0, imageScale, imageScale);
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        char scaleStr[48];
        sprintf(scaleStr, "ZOOM: %.2fx | UP/DOWN: ZOOM | ESC: EXIT", imageScale);
        canvas.drawCenterString(scaleStr, 120, 115);
        
        if (push) {
            pushCanvas();
        }
        return;
    }
    
    if (isMp3Playing) {
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        
        canvas.drawRect(5, 5, 230, 125, CP_CYAN);
        canvas.drawRect(7, 7, 226, 121, CP_DIM);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        canvas.drawCenterString("--- NEURAL MUSIC LINK ---", 120, 12);
        canvas.drawLine(10, 24, 230, 24, CP_CYAN);
        
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString("NOW DECODING:", 120, 32);
        
        canvas.setTextColor(WHITE);
        String nameDisp = mp3Filename;
        if (nameDisp.length() > 22) {
            nameDisp = nameDisp.substring(0, 19) + "...";
        }
        canvas.drawCenterString(nameDisp, 120, 44);
        
        // Progress Bar
        canvas.drawRect(40, 56, 160, 4, CP_DIM);
        uint32_t elapsed = (millis() - mp3StartTime) / 1000;
        uint32_t duration = mp3DurationSeconds;
        int progressW = (elapsed * 160) / duration;
        if (progressW > 160) progressW = 160;
        canvas.fillRect(40, 56, progressW, 4, CP_CYAN);
        
        // Time Label
        char timeStr[32];
        sprintf(timeStr, "%02d:%02d / %02d:%02d", 
                (int)(elapsed / 60), (int)(elapsed % 60), 
                (int)(duration / 60), (int)(duration % 60));
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString(timeStr, 120, 64);
        
        // Graphic Equalizer
        int startX = 60;
        for (int i = 0; i < 10; i++) {
            int h = random(3, 24);
            canvas.fillRect(startX + i * 11, 102 - h, 7, h, CP_CYAN);
            canvas.drawRect(startX + i * 11, 102 - h, 7, h, CP_YELLOW);
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ANY KEY TO STOP", 120, 114);
        
        if (push) {
            pushCanvas();
        }
        return;
    }
    
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw title and outer frames
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    String titlePrefix = isDirSelectionMode ? "[DIR] " : (isSDCardManager ? "SD:" : "FLASH:");
    String title = titlePrefix + fileManagerCurrentPath;
    if (title.length() > 30) {
        title = titlePrefix + "..." + fileManagerCurrentPath.substring(fileManagerCurrentPath.length() - (30 - titlePrefix.length() - 3));
    }
    canvas.drawCenterString("--- " + title + " ---", 120, 12);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);
    
    if (!showFileContent) {
        if (fsStatusMessage != "" && loadedFiles.empty()) {
            canvas.setTextColor(CP_RED);
            canvas.drawCenterString(fsStatusMessage, 120, 65);
        } else {
            if (fsStatusMessage != "") {
                canvas.setTextColor(CP_YELLOW);
                canvas.setCursor(15, 27);
                canvas.print(fsStatusMessage);
            }
            
            // Draw file list with scroll viewport paging
            int startY = fsStatusMessage != "" ? 42 : 32;
            int maxShow = min(5, (int)loadedFiles.size() - fileManagerScrollOffset);
            for (int i = 0; i < maxShow; i++) {
                int fileIdx = fileManagerScrollOffset + i;
                bool isSel = (fileIdx == fileManagerSelected);
                uint16_t color = isSel ? CP_YELLOW : WHITE;
                
                if (isSel) {
                    canvas.fillRect(10, startY - 2, 210, 14, canvas.color565(30, 30, 30));
                    canvas.drawRect(10, startY - 2, 210, 14, CP_CYAN);
                }
                
                canvas.setTextColor(color);
                canvas.setCursor(15, startY);
                
                // Truncate name if it's too long to fit with the scrollbar, or marquee scroll if selected
                String nameToPrint = loadedFiles[fileIdx].name;
                if (isSel && nameToPrint.length() > 18) {
                    if (millis() - lastFileSelectionTime > 1000) {
                        int maxScroll = nameToPrint.length() - 18;
                        if (marqueeScrollOffset <= maxScroll) {
                            nameToPrint = nameToPrint.substring(marqueeScrollOffset);
                        } else {
                            if (millis() - lastFileSelectionTime > 1000 + maxScroll * 250 + 1000) {
                                lastFileSelectionTime = millis();
                                marqueeScrollOffset = 0;
                            }
                            nameToPrint = nameToPrint.substring(maxScroll);
                        }
                    }
                    if (nameToPrint.length() > 18) {
                        nameToPrint = nameToPrint.substring(0, 18);
                    }
                } else if (nameToPrint.length() > 18) {
                    nameToPrint = nameToPrint.substring(0, 15) + "...";
                }
                canvas.print(nameToPrint);
                
                canvas.setCursor(155, startY);
                canvas.print(loadedFiles[fileIdx].sizeStr);
                
                startY += 14;
            }
            
            // Draw vertical scrollbar on the right side (x = 224)
            int totalFiles = loadedFiles.size();
            int trackY = fsStatusMessage != "" ? 42 : 32;
            int trackH = fsStatusMessage != "" ? 66 : 70;
            canvas.drawFastVLine(224, trackY, trackH, CP_DIM);
            if (totalFiles > 5) {
                int barH = (trackH * 5) / totalFiles;
                if (barH < 10) barH = 10;
                int scrollRange = totalFiles - 5;
                int trackRange = trackH - barH;
                int barY = trackY + (fileManagerScrollOffset * trackRange) / scrollRange;
                canvas.fillRect(223, barY, 3, barH, CP_CYAN);
            } else if (totalFiles > 0) {
                canvas.fillRect(223, trackY, 3, trackH, CP_CYAN);
            }
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString(isDirSelectionMode ? "ENTER: CHOOSE FOLDER | ESC: BACK" : "ENTER: OPEN  |  ESC/COMMA: BACK", 120, 114);
    } else {
        // Draw selected file content panel
        canvas.setTextColor(CP_CYAN);
        canvas.setCursor(15, 32);
        canvas.print("FILE: " + openedFileName);
        canvas.drawLine(12, 44, 228, 44, CP_CYAN);
        
        canvas.setTextColor(WHITE);
        int startY = 50;
        for (size_t i = 0; i < openedFileContent.size(); i++) {
            canvas.setCursor(15, startY);
            canvas.print(openedFileContent[i]);
            startY += 12;
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS COMMA OR ESC TO CLOSE", 120, 114);
    }
    
    if (push) {
        pushCanvas();
    }
}

void handleFileManagerInput(Keyboard_Class::KeysState status) {
    if (showImage) {
        bool hasZoomIn = false, hasZoomOut = false;
        bool hasExit = false;
        for (char c : status.word) {
            if (c == ';') hasZoomIn = true;
            if (c == '.') hasZoomOut = true;
            if (c == ',' || c == '`') hasExit = true;
        }
        if (status.enter || status.del) hasExit = true;
        
        if (hasZoomIn) {
            playSound(sound_hover, sound_hover_size);
            imageScale += 0.25f;
            if (imageScale > 4.0f) imageScale = 4.0f;
            drawFileManager();
        } else if (hasZoomOut) {
            playSound(sound_hover, sound_hover_size);
            imageScale -= 0.25f;
            if (imageScale < 0.25f) imageScale = 0.25f;
            drawFileManager();
        } else if (hasExit) {
            playSound(sound_select, sound_select_size);
            showImage = false;
            drawFileManager();
        }
        return;
    }
    
    if (isMp3Playing) {
        // Any keypress stops the MP3 playback
        playSound(sound_select, sound_select_size);
        stopMp3();
        return;
    }
    
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (showFileContent) {
        if (hasBack || status.enter) {
            playSound(sound_select, sound_select_size);
            showFileContent = false;
            drawFileManager();
        }
        return;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (fileManagerCurrentPath != "/") {
            int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
            if (lastSlash == 0) {
                fileManagerCurrentPath = "/";
            } else if (lastSlash > 0) {
                fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
            }
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            populateFileList();
            drawFileManager();
        } else {
            if (isDirSelectionMode) {
                isDirSelectionMode = false;
                appState = STATE_HARDWARE_SETTINGS;
                drawHardwareSettings();
            } else {
                appState = STATE_HARDWARE_MENU;
                drawHardwareMenu();
            }
        }
        return;
    }
    
    if (status.enter && !loadedFiles.empty()) {
        playSound(sound_select, sound_select_size);
        if (loadedFiles[fileManagerSelected].name == "..") {
            int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
            if (lastSlash == 0) {
                fileManagerCurrentPath = "/";
            } else if (lastSlash > 0) {
                fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
            }
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            populateFileList();
            drawFileManager();
        } else if (isDirSelectionMode) {
            if (loadedFiles[fileManagerSelected].isDir) {
                pendingSelectedDir = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + loadedFiles[fileManagerSelected].name;
                appState = STATE_DIR_CONFIRM_POPUP;
                drawDirConfirmPopup();
            } else {
                playSound(sound_fail, sound_fail_size);
            }
        } else {
            appState = STATE_FILE_ACTIONS_MENU;
            fileActionsMenuSelected = 0;
            drawFileActionsMenu();
        }
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    int maxIdx = loadedFiles.empty() ? 0 : loadedFiles.size() - 1;
    
    if (hasUp && maxIdx > 0) {
        playSound(sound_hover, sound_hover_size);
        fileManagerSelected--;
        if (fileManagerSelected < 0) {
            fileManagerSelected = maxIdx;
            if (maxIdx > 4) {
                fileManagerScrollOffset = maxIdx - 4;
            } else {
                fileManagerScrollOffset = 0;
            }
        } else {
            if (fileManagerSelected < fileManagerScrollOffset) {
                fileManagerScrollOffset = fileManagerSelected;
            }
        }
        lastFileSelectionTime = millis();
        marqueeScrollOffset = 0;
        drawFileManager();
    }
    if (hasDown && maxIdx > 0) {
        playSound(sound_hover, sound_hover_size);
        fileManagerSelected++;
        if (fileManagerSelected > maxIdx) {
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
        } else {
            if (fileManagerSelected > fileManagerScrollOffset + 4) {
                fileManagerScrollOffset = fileManagerSelected - 4;
            }
        }
        lastFileSelectionTime = millis();
        marqueeScrollOffset = 0;
        drawFileManager();
    }
}

void stopMp3() {
    isMp3Playing = false;
    mp3IsPaused = false;
    if (mp3) {
        mp3->stop();
        delete mp3;
        mp3 = nullptr;
    }
    if (audioBuffer) {
        audioBuffer->close();
        delete audioBuffer;
        audioBuffer = nullptr;
    }
    if (fileSD) {
        fileSD->close();
        delete fileSD;
        fileSD = nullptr;
    }
    if (fileSPIFFS) {
        fileSPIFFS->close();
        delete fileSPIFFS;
        fileSPIFFS = nullptr;
    }
    if (audioOut) {
        audioOut->stop();
        delete audioOut;
        audioOut = nullptr;
    }
    appState = STATE_FILE_MANAGER;
    drawFileManager();
}

void startMp3(String fileName) {
    String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + fileName;
    
    audioOut = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);
    
    bool started = false;
    if (isSDCardManager) {
        fileSD = new AudioFileSourceSD(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSD, 8192);
        mp3 = new AudioGeneratorMP3();
        started = mp3->begin(audioBuffer, audioOut);
    } else {
        fileSPIFFS = new AudioFileSourceSPIFFS(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSPIFFS, 8192);
        mp3 = new AudioGeneratorMP3();
        started = mp3->begin(audioBuffer, audioOut);
    }
    
    if (started) {
        isMp3Playing = true;
        mp3Filename = fileName;
        mp3IsPaused = false;
        mp3StartTime = millis();
        mp3PausedTime = 0;
        mp3DurationSeconds = 180;
        appState = STATE_FILE_MANAGER;
        showFileContent = false;
    } else {
        if (mp3) { delete mp3; mp3 = nullptr; }
        if (audioBuffer) { delete audioBuffer; audioBuffer = nullptr; }
        if (fileSD) { delete fileSD; fileSD = nullptr; }
        if (fileSPIFFS) { delete fileSPIFFS; fileSPIFFS = nullptr; }
        if (audioOut) { delete audioOut; audioOut = nullptr; }
        
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("FILE IO ERROR", 120, 50);
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ANY KEY", 120, 80);
        pushCanvas();
        delay(1500);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
    }
}

void drawFileActionsMenu() {
    // First, draw the file manager background behind the pop-up menu!
    drawFileManager(false);
    
    // Draw pop-up overlay
    canvas.startWrite();
    
    // Pop-up border and box
    canvas.fillRect(35, 20, 170, 95, CP_BG);
    canvas.drawRect(35, 20, 170, 95, CP_CYAN);
    canvas.drawRect(37, 22, 166, 91, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- ACTION MENU ---", 120, 26);
    canvas.drawLine(40, 36, 200, 36, CP_CYAN);
    
    int startY = 41;
    bool hasPaste = (clipboardSourcePath != "");
    std::vector<String> options = {"1. OPEN", "2. RENAME", "3. DELETE", "4. COPY", "5. PASTE"};
    
    for (int i = 0; i < 5; i++) {
        bool isSel = (i == fileActionsMenuSelected);
        uint16_t color = isSel ? CP_YELLOW : WHITE;
        
        if (i == 4 && !hasPaste) { // PASTE is disabled
            color = CP_DIM;
        }
        
        if (isSel) {
            canvas.fillRect(42, startY - 2, 156, 12, canvas.color565(30, 30, 30));
            canvas.drawRect(42, startY - 2, 156, 12, CP_CYAN);
        }
        
        canvas.setTextColor(color);
        canvas.setCursor(48, startY);
        canvas.print(options[i]);
        
        if (i == 4 && !hasPaste) {
            canvas.setCursor(120, startY);
            canvas.print("[EMPTY]");
        }
        
        startY += 12;
    }
    
    pushCanvas();
}

void handleFileActionsMenuInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        fileActionsMenuSelected--;
        if (fileActionsMenuSelected < 0) fileActionsMenuSelected = 4;
        drawFileActionsMenu();
    }
    if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        fileActionsMenuSelected++;
        if (fileActionsMenuSelected > 4) fileActionsMenuSelected = 0;
        drawFileActionsMenu();
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        bool hasPaste = (clipboardSourcePath != "");
        
        if (fileActionsMenuSelected == 4 && !hasPaste) {
            // Paste is disabled
            return;
        }
        
        RealFile targetFile = loadedFiles[fileManagerSelected];
        String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + targetFile.name;
        
        if (fileActionsMenuSelected == 0) { // OPEN
            if (targetFile.name == "..") {
                // Navigate up
                int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
                if (lastSlash == 0) {
                    fileManagerCurrentPath = "/";
                } else if (lastSlash > 0) {
                    fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
                }
                fileManagerSelected = 0;
                fileManagerScrollOffset = 0;
                populateFileList();
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else if (targetFile.isDir) {
                // Navigate down
                if (fileManagerCurrentPath == "/") {
                    fileManagerCurrentPath = "/" + targetFile.name;
                } else {
                    fileManagerCurrentPath = fileManagerCurrentPath + "/" + targetFile.name;
                }
                fileManagerSelected = 0;
                fileManagerScrollOffset = 0;
                populateFileList();
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                String lowerName = targetFile.name;
                lowerName.toLowerCase();
                if (lowerName.endsWith(".mp3")) {
                    startMp3(targetFile.name);
                } else if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) {
                    playSound(sound_select, sound_select_size);
                    openedImageName = targetFile.name;
                    imageScale = 1.0f;
                    showImage = true;
                    appState = STATE_FILE_MANAGER;
                    drawFileManager();
                } else {
                    // Open file content
                    readSelectedFileContent(targetFile.name);
                    showFileContent = true;
                    appState = STATE_FILE_MANAGER;
                    drawFileManager();
                }
            }
        }
        else if (fileActionsMenuSelected == 1) { // RENAME
            if (targetFile.name != "..") {
                renameInputText = targetFile.name;
                appState = STATE_FILE_RENAME_INPUT;
                drawFileRenameInput();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 2) { // DELETE
            if (targetFile.name != "..") {
                if (isSDCardManager) {
                    if (isSDFallback) {
                        // Mock delete in demo mode
                        loadedFiles.erase(loadedFiles.begin() + fileManagerSelected);
                    } else {
                        // Physical delete
                        deleteRecursive(fullPath);
                        populateFileList();
                    }
                } else {
                    if (isFlashFallback) {
                        loadedFiles.erase(loadedFiles.begin() + fileManagerSelected);
                    } else {
                        deleteRecursive(fullPath);
                        populateFileList();
                    }
                }
                if (fileManagerSelected >= (int)loadedFiles.size()) {
                    fileManagerSelected = (int)loadedFiles.size() - 1;
                }
                if (fileManagerSelected < 0) {
                    fileManagerSelected = 0;
                }
                
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected < fileManagerScrollOffset) {
                        fileManagerScrollOffset = fileManagerSelected;
                    } else if (fileManagerSelected > fileManagerScrollOffset + 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    }
                    if (fileManagerScrollOffset + 5 > (int)loadedFiles.size()) {
                        fileManagerScrollOffset = (int)loadedFiles.size() - 5;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 3) { // COPY
            if (targetFile.name != "..") {
                clipboardSourcePath = fullPath;
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 4) { // PASTE
            String sourceFilename = clipboardSourcePath;
            int lastSlash = sourceFilename.lastIndexOf('/');
            if (lastSlash >= 0) sourceFilename = sourceFilename.substring(lastSlash + 1);
            
            String destPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + sourceFilename;
            
            if (isSDCardManager) {
                if (isSDFallback) {
                    RealFile pf = {sourceFilename, "1.0 KB", false};
                    loadedFiles.push_back(pf);
                } else {
                    // Physical paste
                    File src = SD.open(clipboardSourcePath, FILE_READ);
                    File dst = SD.open(destPath, FILE_WRITE);
                    if (src && dst) {
                        uint8_t buf[256];
                        while (src.available()) {
                            int len = src.read(buf, sizeof(buf));
                            dst.write(buf, len);
                        }
                    }
                    if (src) src.close();
                    if (dst) dst.close();
                    populateFileList();
                }
            } else {
                if (isFlashFallback) {
                    RealFile pf = {sourceFilename, "1.0 KB", false};
                    loadedFiles.push_back(pf);
                } else {
                    // Physical paste
                    File src = SPIFFS.open(clipboardSourcePath, FILE_READ);
                    File dst = SPIFFS.open(destPath, FILE_WRITE);
                    if (src && dst) {
                        uint8_t buf[256];
                        while (src.available()) {
                            int len = src.read(buf, sizeof(buf));
                            dst.write(buf, len);
                        }
                    }
                    if (src) src.close();
                    if (dst) dst.close();
                    populateFileList();
                }
            }
            int foundIdx = -1;
            for (size_t i = 0; i < loadedFiles.size(); i++) {
                if (loadedFiles[i].name == sourceFilename) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx >= 0) {
                fileManagerSelected = foundIdx;
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected > 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    } else {
                        fileManagerScrollOffset = 0;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
            }
            appState = STATE_FILE_MANAGER;
            drawFileManager();
        }
    }
}

void drawFileRenameInput() {
    // First, draw the file manager background behind the rename dialog!
    drawFileManager(false);
    
    // Draw dialog overlay
    canvas.startWrite();
    canvas.fillRect(25, 35, 190, 65, CP_BG);
    canvas.drawRect(25, 35, 190, 65, CP_CYAN);
    canvas.drawRect(27, 37, 186, 61, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- RENAME FILE ---", 120, 42);
    
    // Input box
    canvas.drawRect(35, 56, 170, 16, CP_CYAN);
    canvas.fillRect(36, 57, 168, 14, canvas.color565(30, 30, 30));
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(40, 60);
    // Draw typed name with a blink cursor
    String disp = renameInputText;
    if (disp.length() > 20) disp = disp.substring(disp.length() - 20);
    if (blinkState) disp += "_";
    canvas.print(disp);
    
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("ENTER: RENAME  |  ESC/COMMA: BACK", 120, 80);
    
    pushCanvas();
}

void handleFileRenameInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_FILE_ACTIONS_MENU;
        drawFileActionsMenu();
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (renameInputText.length() > 0) {
            RealFile targetFile = loadedFiles[fileManagerSelected];
            String oldPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + targetFile.name;
            String newPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + renameInputText;
            
            if (isSDCardManager) {
                if (isSDFallback) {
                    loadedFiles[fileManagerSelected].name = renameInputText;
                } else {
                    SD.rename(oldPath, newPath);
                    populateFileList();
                }
            } else {
                if (isFlashFallback) {
                    loadedFiles[fileManagerSelected].name = renameInputText;
                } else {
                    SPIFFS.rename(oldPath, newPath);
                    populateFileList();
                }
            }
            
            // Relocate renamed file in the sorted list
            int foundIdx = -1;
            for (size_t i = 0; i < loadedFiles.size(); i++) {
                if (loadedFiles[i].name == renameInputText) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx >= 0) {
                fileManagerSelected = foundIdx;
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected > 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    } else {
                        fileManagerScrollOffset = 0;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
            }
        }
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }
    
    if (status.del && renameInputText.length() > 0) {
        renameInputText.remove(renameInputText.length() - 1);
        drawFileRenameInput();
    }
    
    bool typed = false;
    for (char c : status.word) {
        if (c >= 32 && c <= 126 && renameInputText.length() < 24) {
            // Filter out backquote from being typed
            if (c != '`') {
                renameInputText += c;
                typed = true;
            }
        }
    }
    
    if (typed) {
        drawFileRenameInput();
    }
}

void drawFileLoading() {
    String statusText = isSDCardManager ? "READING SD CARD SCHEMA..." : "READING FLASH SCHEMA...";
    drawProgressBar(loadingProgress, statusText, CP_CYAN);
    
    loadingProgress += 4;
    if (loadingProgress >= 100) {
        loadingProgress = 100;
        populateFileList();
        fileManagerSelected = 0;
        fileManagerScrollOffset = 0;
        appState = STATE_FILE_MANAGER;
        drawFileManager();
    } else {
        delay(30);
    }
}
