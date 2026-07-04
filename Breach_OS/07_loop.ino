// Main runtime loop and app-state input dispatch.

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!otaInit) {
            ArduinoOTA.setHostname("Cardputer-Breach");
            ArduinoOTA.begin();
            otaInit = true;
        }
        ArduinoOTA.handle();
    }

    M5Cardputer.update();
    unsigned long now = millis();
    
    if (isMp3Playing) {
        if (mp3 || wav) {
            static unsigned long lastVisualizerUpdate = 0;
            if (mp3IsPaused) {
                if (millis() - lastVisualizerUpdate > 150) {
                    if (appState == STATE_MUSIC_PLAYER) drawMusicPlayer();
                    lastVisualizerUpdate = millis();
                }
            } else {
                bool stillPlaying = mp3 ? mp3->loop() : wav->loop();
                if (!stillPlaying) {
                    if (appState == STATE_MUSIC_PLAYER) {
                        playNextTrack();
                    } else {
                        stopMp3();
                    }
                } else if (millis() - lastVisualizerUpdate > 100) {
                    if (appState == STATE_MUSIC_PLAYER) {
                        drawMusicPlayer();
                    } else if (appState == STATE_FILE_MANAGER) {
                        drawFileManager();
                    }
                    lastVisualizerUpdate = millis();
                }
            }
        } else {
            stopMp3();
        }
    }
    
    bool keyChanged = M5Cardputer.Keyboard.isChange();
    bool keyPressed = M5Cardputer.Keyboard.isPressed();
    Keyboard_Class::KeysState globalStatus;
    if (keyChanged && keyPressed) {
        globalStatus = M5Cardputer.Keyboard.keysState();
    }
    
    if (keyChanged && keyPressed) {
        Keyboard_Class::KeysState status = globalStatus;
        bool volChanged = false;
        bool brtChanged = false;
        for (auto i : status.word) {
            if (i == '-' || i == '_') {
                globalVolume -= 5;
                if (globalVolume < 0) globalVolume = 0;
                volChanged = true;
            } else if (i == '=' || i == '+') {
                globalVolume += 5;
                if (globalVolume > 100) globalVolume = 100;
                volChanged = true;
            } else if (i == '[') {
                globalBrightness -= 5;
                if (globalBrightness < 5) globalBrightness = 5;
                brtChanged = true;
            } else if (i == ']') {
                globalBrightness += 5;
                if (globalBrightness > 100) globalBrightness = 100;
                brtChanged = true;
            }
        }
        if (volChanged) {
            M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
            prefs.putInt("volume", globalVolume);
            playSound(sound_hover, sound_hover_size);
            showVolumePopup = true;
            lastVolumeChangeTime = now;
            drawCurrentScreen();
        }
        if (brtChanged) {
            M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
            prefs.putInt("brightness", globalBrightness);
            playSound(sound_hover, sound_hover_size);
            showBrightnessPopup = true;
            lastBrightnessChangeTime = now;
            drawCurrentScreen();
        }
    }
    
    if (showVolumePopup) {
        unsigned long elapsed = now - lastVolumeChangeTime;
        if (elapsed > 1000) {
            if (elapsed >= 1300) {
                showVolumePopup = false;
                drawCurrentScreen();
            } else {
                drawCurrentScreen();
            }
        }
    }
    
    if (showBrightnessPopup) {
        unsigned long elapsed = now - lastBrightnessChangeTime;
        if (elapsed > 1000) {
            if (elapsed >= 1300) {
                showBrightnessPopup = false;
                drawCurrentScreen();
            } else {
                drawCurrentScreen();
            }
        }
    }
    
    if (appState == STATE_SPLASH) {
        if (now - lastLogUpdate > 200) {
            if (logOffset < dummyLogs.size() - 7) {
                logOffset++;
                drawSplash();
            }
            lastLogUpdate = now;
        }
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawSplash();
        }
        if (keyChanged && keyPressed) {
            handleSplashInput(globalStatus);
        }

        bool needsRedraw = false;
        if (showSplashBootMenu && abs(currentSplashBootScroll - targetSplashBootScroll) > 0.01) {
            currentSplashBootScroll += (targetSplashBootScroll - currentSplashBootScroll) * 0.3;
            if (abs(currentSplashBootScroll - targetSplashBootScroll) <= 0.01) {
                currentSplashBootScroll = targetSplashBootScroll;
            }
            needsRedraw = true;
        }
        if (needsRedraw) {
            drawSplash();
        }
        delay(10);
        return;
    }
    
    if (now - lastBlink > 500) {
        blinkState = !blinkState;
        lastBlink = now;
        if (appState == STATE_AUTH_MENU) drawAuthMenu();
        if (appState == STATE_WIFI_PASS) drawWifiPass();
        if (appState == STATE_ACCOUNT) drawAccountMenu();
    }
    
    if (appState == STATE_BREACH_MODE) {
        if (keyChanged && keyPressed) {
            handleBreachModeInput(globalStatus);
            if (appState == STATE_BREACH_MODE) drawBreachModePrompt();
        }
        bool needsRedraw = false;
        if (abs(currentBreachScroll - targetBreachScroll) > 0.01) {
            currentBreachScroll += (targetBreachScroll - currentBreachScroll) * 0.3;
            if (abs(currentBreachScroll - targetBreachScroll) <= 0.01) {
                currentBreachScroll = targetBreachScroll;
            }
            needsRedraw = true;
        }
        if (needsRedraw) drawBreachModePrompt();
        delay(10);
        return;
    }

    if (appState == STATE_AUTH_MENU) {
        if (keyChanged && keyPressed) {
            handleAuthInput(globalStatus);
            if (appState == STATE_AUTH_MENU) drawAuthMenu();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_WIFI_SCAN) {
        if (keyChanged && keyPressed) {
            handleWifiScanInput(globalStatus);
            if (appState == STATE_WIFI_SCAN) drawWifiScan();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_WIFI_PASS) {
        if (keyChanged && keyPressed) {
            handleWifiPassInput(globalStatus);
            if (appState == STATE_WIFI_PASS) drawWifiPass();
        }
        delay(10);
        return;
    }
    
    if (insaneMode == 2) {
        static unsigned long lastInsane = 0;
        static unsigned long nextInsane = 500;
        if (now - lastInsane > nextInsane) {
            if (appState == STATE_AUTH_MENU) drawAuthMenu();
            else if (appState == STATE_BREACH_MODE) drawBreachModePrompt();
            else if (appState == STATE_MAIN_MENU) drawMainMenu();
            else if (appState == STATE_ACCOUNT) drawAccountMenu();
            else if (appState == STATE_SSH) drawSshScreen();
            else if (appState == STATE_TELNET_BBS) drawTelnetBbsScreen();
            else if (appState == STATE_GRID_SELECT) drawGridSelect();
            else if (appState == STATE_PHASE_TRANSITION) drawPhaseTransition();
            else if (appState == STATE_FAILED_SCREEN) drawGameOverFailed();
            else if (appState == STATE_HARDWARE_MENU) drawHardwareMenu();

            lastInsane = now;
            nextInsane = random(50, 1200);
        }
    }
    
    if (appState == STATE_MAIN_MENU) {
        if (insaneMode == 1) {
            static unsigned long lastMenuGlitch = 0;
            static unsigned long nextMenuGlitch = 500;
            if (now - lastMenuGlitch > nextMenuGlitch) {
                drawMainMenu();
                lastMenuGlitch = now;
                nextMenuGlitch = random(50, 1200);
            }
        }
        
        if (keyChanged && keyPressed) {
            handleMainMenuInput(globalStatus);
            if (appState == STATE_MAIN_MENU) drawMainMenu();
        }
        
        bool needsRedraw = false;
        if (abs(currentMenuScroll - targetMenuScroll) > 0.01) {
            currentMenuScroll += (targetMenuScroll - currentMenuScroll) * 0.3; // Smooth lerp
            if (abs(currentMenuScroll - targetMenuScroll) <= 0.01) {
                currentMenuScroll = targetMenuScroll;
            }
            needsRedraw = true;
        }
        
        if (showMenuDesc && descAnimWidth < 195.0) {
            descAnimWidth += (195.0 - descAnimWidth) * 0.4;
            if (195.0 - descAnimWidth < 1.0) descAnimWidth = 195.0;
            needsRedraw = true;
        } else if (!showMenuDesc && descAnimWidth > 0.0) {
            descAnimWidth += (0.0 - descAnimWidth) * 0.4;
            if (descAnimWidth < 1.0) descAnimWidth = 0.0;
            needsRedraw = true;
        }
        
        if (needsRedraw) {
            drawMainMenu();
        }
        
        delay(10);
        return;
    }

    if (appState == STATE_CONTROLS) {
        if (keyChanged && keyPressed) {
            handleControlsInput(globalStatus);
            if (appState == STATE_CONTROLS) drawControlsScreen();
        }
        delay(10);
        return;
    }

    if (appState == STATE_CREDITS) {
        if (keyChanged && keyPressed) {
            handleCreditsInput(globalStatus);
            if (appState == STATE_CREDITS) drawCreditsScreen();
        }
        delay(10);
        return;
    }

    if (appState == STATE_GITHUB_QR) {
        if (keyChanged && keyPressed) {
            handleGithubQrInput(globalStatus);
            if (appState == STATE_GITHUB_QR) drawGithubQrScreen();
        }
        delay(10);
        return;
    }

    if (appState == STATE_SSH) {
        pollSshTerminal();
        static unsigned long lastSshTerminalDraw = 0;
        if (sshTerminalDirty && now - lastSshTerminalDraw > 100) {
            drawSshScreen();
            sshTerminalDirty = false;
            lastSshTerminalDraw = now;
        }
        if (keyChanged && keyPressed) {
            handleSshInput(globalStatus);
            if (appState == STATE_SSH) drawSshScreen();
            sshTerminalDirty = false;
            lastSshTerminalDraw = now;
        }
        delay(10);
        return;
    }

    if (appState == STATE_TELNET_BBS) {
        pollTelnetBbs();
        static unsigned long lastTelnetDraw = 0;
        if (telnetTerminalDirty && now - lastTelnetDraw > 100) {
            drawTelnetBbsScreen();
            telnetTerminalDirty = false;
            lastTelnetDraw = now;
        }
        if (keyChanged && keyPressed) {
            handleTelnetBbsInput(globalStatus);
            if (appState == STATE_TELNET_BBS) drawTelnetBbsScreen();
            telnetTerminalDirty = false;
            lastTelnetDraw = now;
        }
        delay(10);
        return;
    }

    if (appState == STATE_HARDWARE_MENU) {
        if (insaneMode == 1) {
            static unsigned long lastHardwareGlitch = 0;
            static unsigned long nextHardwareGlitch = 500;
            if (now - lastHardwareGlitch > nextHardwareGlitch) {
                drawHardwareMenu();
                lastHardwareGlitch = now;
                nextHardwareGlitch = random(50, 1200);
            }
        }
        
        if (keyChanged && keyPressed) {
            handleHardwareMenuInput(globalStatus);
            if (appState == STATE_HARDWARE_MENU) drawHardwareMenu();
        }
        
        bool needsRedraw = false;
        if (abs(currentHardwareScroll - targetHardwareScroll) > 0.01) {
            currentHardwareScroll += (targetHardwareScroll - currentHardwareScroll) * 0.3; // Smooth lerp
            if (abs(currentHardwareScroll - targetHardwareScroll) <= 0.01) {
                currentHardwareScroll = targetHardwareScroll;
            }
            needsRedraw = true;
        }
        
        if (showHardwareDesc && hardwareDescAnimWidth < 195.0) {
            hardwareDescAnimWidth += (195.0 - hardwareDescAnimWidth) * 0.4;
            if (195.0 - hardwareDescAnimWidth < 1.0) hardwareDescAnimWidth = 195.0;
            needsRedraw = true;
        } else if (!showHardwareDesc && hardwareDescAnimWidth > 0.0) {
            hardwareDescAnimWidth += (0.0 - hardwareDescAnimWidth) * 0.4;
            if (hardwareDescAnimWidth < 1.0) hardwareDescAnimWidth = 0.0;
            needsRedraw = true;
        }
        
        if (needsRedraw) {
            drawHardwareMenu();
        }
        
        delay(10);
        return;
    }
    
    if (appState == STATE_HARDWARE_SETTINGS) {
        static unsigned long lastSettingsMarquee = 0;
        if (millis() - lastSettingsMarquee > 150) {
            drawHardwareSettings();
            lastSettingsMarquee = millis();
        }
        if (keyChanged && keyPressed) {
            handleHardwareSettingsInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_USB_DRIVE) {
        static unsigned long lastUsbDriveDraw = 0;
        if (keyChanged && keyPressed) {
            handleUsbDriveInput(globalStatus);
        }
        if (appState == STATE_USB_DRIVE && millis() - lastUsbDriveDraw > 500) {
            drawUsbDriveScreen();
            lastUsbDriveDraw = millis();
        }
        delay(10);
        return;
    }

    if (appState == STATE_BADUSB) {
        static unsigned long lastBadUsbDraw = 0;
        if (keyChanged && keyPressed) {
            handleBadUsbInput(globalStatus);
        }
        if (appState == STATE_BADUSB && badUsbMode != 2 && millis() - lastBadUsbDraw > 250) {
            drawBadUsbScreen();
            lastBadUsbDraw = millis();
        }
        delay(10);
        return;
    }

    if (appState == STATE_OTA_CATALOG) {
        static unsigned long lastOtaMarquee = 0;
        if (millis() - lastOtaMarquee > 150) {
            drawOtaCatalog();
            lastOtaMarquee = millis();
        }
        if (keyChanged && keyPressed) {
            handleOtaCatalogInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_DIR_CONFIRM_POPUP) {
        if (keyChanged && keyPressed) {
            handleDirConfirmPopupInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_MUSIC_PLAYER) {
        Keyboard_Class::KeysState musicStatus = M5Cardputer.Keyboard.keysState();
        updateMusicInputGate(musicStatus.enter);
        if (keyChanged && keyPressed) {
            handleMusicPlayerInput(globalStatus);
        }
        // Let marquee titles scroll if needed
        static unsigned long lastPlayerMarquee = 0;
        if (millis() - lastPlayerMarquee > 150) {
            drawMusicPlayer();
            lastPlayerMarquee = millis();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_LOADING) {
        drawFileLoading();
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_MANAGER) {
        if (keyChanged && keyPressed) {
            handleFileManagerInput(globalStatus);
        }
        
        if (!isMp3Playing) {
            if (!loadedFiles.empty() && loadedFiles[fileManagerSelected].name.length() > 18) {
                if (millis() - lastFileSelectionTime > 1000) {
                    if (millis() - lastMarqueeUpdate > 250) {
                        marqueeScrollOffset++;
                        drawFileManager();
                        lastMarqueeUpdate = millis();
                    }
                }
            }
        }
        
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_ACTIONS_MENU) {
        if (keyChanged && keyPressed) {
            handleFileActionsMenuInput(globalStatus);
        }
        delay(10);
        return;
    }

    if (appState == STATE_FILE_NEW_TYPE_MENU) {
        if (keyChanged && keyPressed) {
            handleFileNewTypeMenuInput(globalStatus);
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_FILE_RENAME_INPUT) {
        if (keyChanged && keyPressed) {
            handleFileRenameInput(globalStatus);
        }
        
        static unsigned long lastRenameBlink = 0;
        if (millis() - lastRenameBlink > 500) {
            blinkState = !blinkState;
            drawFileRenameInput();
            lastRenameBlink = millis();
        }
        
        delay(10);
        return;
    }

    if (appState == STATE_FILE_TEXT_EDITOR) {
        if (keyChanged && keyPressed) {
            handleFileTextEditorInput(globalStatus);
        }

        static unsigned long lastTextEditorBlink = 0;
        if (millis() - lastTextEditorBlink > 500) {
            blinkState = !blinkState;
            drawFileTextEditor();
            lastTextEditorBlink = millis();
        }

        delay(10);
        return;
    }

    if (appState == STATE_LEADERBOARD) {
        if (keyChanged && keyPressed) {
            Keyboard_Class::KeysState status = globalStatus;
            
            bool hasUp = false, hasDown = false;
            for (char c : status.word) {
                if (c == ';') hasUp = true;
                if (c == '.') hasDown = true;
            }
            
            if (hasDown && leaderboardCursor < totalLeaderboardSize - 1) {
                playSound(sound_hover, sound_hover_size);
                leaderboardCursor++;
                if (leaderboardCursor >= globalLeaderboard.size()) {
                    drawMessage("LOADING MORE...");
                    fetchLeaderboard(globalLeaderboard.size(), 10);
                }
                if (leaderboardCursor > leaderboardScrollOffset + 2) {
                    leaderboardScrollOffset = leaderboardCursor - 2;
                }
                drawLeaderboard();
            }
            
            if (hasUp && leaderboardCursor > 0) {
                playSound(sound_hover, sound_hover_size);
                leaderboardCursor--;
                if (leaderboardCursor < leaderboardScrollOffset) {
                    leaderboardScrollOffset = leaderboardCursor;
                }
                drawLeaderboard();
            }
            
            if (status.enter || status.del) {
                playSound(sound_select, sound_select_size);
                if (leaderboardReturnToBreach) {
                    leaderboardReturnToBreach = false;
                    appState = STATE_BREACH_MODE;
                    resetBreachModeScroll();
                    drawBreachModePrompt();
                } else {
                    enterMainMenu();
                }
            }
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_ACCOUNT) {
        if (keyChanged && keyPressed) {
            handleAccountInput(globalStatus);
            if (appState == STATE_ACCOUNT) drawAccountMenu();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_GRID_SELECT) {
        if (keyChanged && keyPressed) {
            handleGridSelectInput(globalStatus);
        }
        if (abs(currentGridScroll - targetGridScroll) > 0.01) {
            currentGridScroll += (targetGridScroll - currentGridScroll) * 0.3; // Smooth lerp
            if (abs(currentGridScroll - targetGridScroll) <= 0.01) {
                currentGridScroll = targetGridScroll;
            }
            drawGridSelect();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_PHASE_TRANSITION) {
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawPhaseTransition();
        }
        if (keyChanged && keyPressed) {
            handlePhaseTransitionInput(globalStatus);
            if (appState == STATE_PHASE_TRANSITION) drawPhaseTransition();
        }
        delay(10);
        return;
    }
    
    if (appState == STATE_FAILED_SCREEN) {
        if (now - lastBlink > 500) {
            blinkState = !blinkState;
            lastBlink = now;
            drawGameOverFailed();
        }
        if (keyChanged && keyPressed) {
            if (globalStatus.enter) {
                playSound(sound_select, sound_select_size);
                lastBreachFailed = true;
                returnToBreachMode();
            }
        }
        delay(10);
        return;
    }
    
    if (!gameOver && now - lastUpdate > 10) {
        timeLeft -= (now - lastUpdate);
        lastUpdate = now;
        if (timeLeft <= 0) {
            timeLeft = 0;
            gameOver = true;
            hackSuccess = false;
            playSound(sound_fail, sound_fail_size);
            isAnimating = true;
            animStartTime = now;
            drawScreen();
        } else {
            drawTimer(false);
        }
    }
    
    if (!gameOver && now - lastBlink > 600) {
        blinkState = !blinkState;
        lastBlink = now;
        if (bufferIndex < targetSize) {
            int boxW = min(22, (120 / targetSize) - 2);
            int boxStep = boxW + 2;
            int bx = 120 + bufferIndex*boxStep;
            int by = 83;
            canvas.startWrite();
            canvas.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
            pushCanvas();
        }
    }
    
    if (isAnimating) {
        updateAnimation();
        if (now - animStartTime > 1000) {
            isAnimating = false;
        }
    }
    
    if (keyChanged && keyPressed) {
        Keyboard_Class::KeysState status = globalStatus;
        
        for (char c : status.word) {
            if (c == 27 || c == '`') {
                playSound(sound_select, sound_select_size);
                appState = STATE_GRID_SELECT;
                gridMenuFocus = 0;
                currentGridScroll = 0;
                targetGridScroll = 0;
                drawGridSelect();
                return;
            }
        }
        
        if (gameOver) {
            if (status.enter) {
                playSound(sound_select, sound_select_size);
                if (hackSuccess) {
                    appState = STATE_PHASE_TRANSITION;
                    phaseMenuFocus = 0;
                    drawPhaseTransition();
                } else {
                    appState = STATE_FAILED_SCREEN;
                    drawGameOverFailed();
                }
            }
            return;
        }
        
        bool hasW = false, hasA = false, hasS = false, hasD = false;
        bool hasSemi = false, hasDot = false, hasComma = false, hasSlash = false;
        for (char c : status.word) {
            if (c == 'w') hasW = true;
            if (c == 'a') hasA = true;
            if (c == 's') hasS = true;
            if (c == 'd') hasD = true;
            if (c == ';') hasSemi = true;
            if (c == '.') hasDot = true;
            if (c == ',') hasComma = true;
            if (c == '/') hasSlash = true;
        }
        
        int cR_curr = isRowActive ? activeRow : cursorIdx;
        int cC_curr = isRowActive ? cursorIdx : activeCol;

        if (hasComma || hasA || hasSemi || hasW) {
            bool moved = false;
            if (isRowActive) {
                for(int j=cC_curr-1; j>=0; j--) { if (matrix[cR_curr][j] != "") { cursorIdx = j; moved = true; break; } }
            } else {
                for(int i=cR_curr-1; i>=0; i--) { if (matrix[i][cC_curr] != "") { cursorIdx = i; moved = true; break; } }
            }
            if (moved) playSound(sound_hover, sound_hover_size);
        }
        if (hasSlash || hasD || hasDot || hasS) {
            bool moved = false;
            if (isRowActive) {
                for(int j=cC_curr+1; j<gridSize; j++) { if (matrix[cR_curr][j] != "") { cursorIdx = j; moved = true; break; } }
            } else {
                for(int i=cR_curr+1; i<gridSize; i++) { if (matrix[i][cC_curr] != "") { cursorIdx = i; moved = true; break; } }
            }
            if (moved) playSound(sound_hover, sound_hover_size);
        }
        
        if (status.enter) {
            int cR = isRowActive ? activeRow : cursorIdx;
            int cC = isRowActive ? cursorIdx : activeCol;
            
            if (matrix[cR][cC] != "") {
                playSound(sound_buffer, sound_buffer_size);
                
                buffer[bufferIndex++] = matrix[cR][cC];
                matrix[cR][cC] = ""; 
                
                isRowActive = !isRowActive;
                if (isRowActive) {
                    activeRow = cR;
                } else {
                    activeCol = cC;
                }
                
                int oldIdx = isRowActive ? cC : cR;
                int newCursor = -1;
                int dist = 999;
                if (isRowActive) {
                    for (int j=0; j<gridSize; j++) {
                        if (matrix[activeRow][j] != "") {
                            int d = abs(j - oldIdx);
                            if (d < dist) { dist = d; newCursor = j; }
                        }
                    }
                } else {
                    for (int i=0; i<gridSize; i++) {
                        if (matrix[i][activeCol] != "") {
                            int d = abs(i - oldIdx);
                            if (d < dist) { dist = d; newCursor = i; }
                        }
                    }
                }
                cursorIdx = (newCursor != -1) ? newCursor : oldIdx;
                
                bool isPrefix = true;
                for (int i = 0; i < bufferIndex; i++) {
                    if (buffer[i] != targetSeq[i]) {
                        isPrefix = false;
                        break;
                    }
                }
                
                if (!isPrefix) {
                    gameOver = true;
                    hackSuccess = false;
                    playSound(sound_fail, sound_fail_size);
                    isAnimating = true;
                    animStartTime = millis();
                } else if (bufferIndex >= targetSize) {
                    gameOver = true;
                    hackSuccess = true;
                    lastTimeRatio = (float)timeLeft / (float)maxTime;
                    lastPhaseScore = 1000 * gridSize * currentPhase * (1.0 + lastTimeRatio);
                    accumulatedScore += lastPhaseScore;
                    playSound(sound_success, sound_success_size);
                    isAnimating = true;
                    animStartTime = millis();
                }
            }
        }
        drawScreen();
    }
    delay(10);
}
