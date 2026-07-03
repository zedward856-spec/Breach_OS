// Breach_OS gameplay, grid generation, scoring, rendering, and animations.

void drawGridSelect() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    String scoreStr = "HI_SCORE";
    if (gridMenuFocus < 3) {
        int grid = (gridMenuFocus == 0) ? 3 : ((gridMenuFocus == 1) ? 4 : 5);
        String key = isGuest ? ("g_hs_" + String(grid)) : (authUser + "_hs_" + String(grid));
        int hs = prefs.getInt(key.c_str(), 0);
        if (hs > 0) scoreStr = String(hs);
    }
    drawRotatedText(scoreStr, 10, 67, CP_YELLOW);
    
    // Draw rotating wheel arc on the left
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    String labels[4] = {"3x3", "4x4", "5x5", "BACK"};
    String descs[4] = {"Base Points Payout", "Higher Points Payout", "Maximum Points Payout", "Return to Menu"};
    
    for (int i = 0; i < 4; i++) {
        // Calculate shortest wrapping distance for seamless infinite scroll
        float rawOffset = i - currentGridScroll;
        float offset = fmod(rawOffset, 4.0);
        if (offset > 2.0) offset -= 4.0;
        if (offset < -2.0) offset += 4.0;
        
        // Don't draw items too far off screen
        if (abs(offset) > 1.5) continue;
        
        // Calculate tick position on the arc
        float angle = offset * 0.391; // ~22.4 degrees in radians
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == gridMenuFocus);
        
        uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;
        
        // Draw outward-pointing rotated ticks instead of flat rectangles
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
        if (i == 3 && lastBreachFailed) {
            color = CP_RED;
        }
        
        drawChippedButton(x, y, w, h, color);
        
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
        
        if (isSelected) {
            int labelWidth = canvas.textWidth(labels[i]);
            canvas.setTextSize(1);
            canvas.setTextColor(WHITE);
            canvas.setCursor(textX + labelWidth + 6, textY + 4); 
            canvas.print(descs[i]);
        }
    }

    drawWheelPositionIndicator(gridMenuFocus, 4);

    pushCanvas();
}

void handleGridSelectInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (gridMenuFocus == 3) {
            enterMainMenu();
            return;
        }
        
        if (gridMenuFocus == 0) selectedGridSize = 3;
        else if (gridMenuFocus == 1) selectedGridSize = 4;
        else selectedGridSize = 5;
        
        currentPhase = 1;
        accumulatedScore = 0;
        appState = STATE_PLAYING;
        initGame();
        drawScreen();
        return;
    }
    bool hasUp = false, hasDown = false;
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == '`') hasEsc = true;
    }
    if (hasEsc) {
        playSound(sound_select, sound_select_size);
        enterMainMenu();
        return;
    }
    if (hasUp) { 
        playSound(sound_hover, sound_hover_size); 
        gridMenuFocus--; 
        if (gridMenuFocus < 0) gridMenuFocus = 3; 
        targetGridScroll -= 1.0;
    }
    if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        gridMenuFocus++; 
        if (gridMenuFocus > 3) gridMenuFocus = 0; 
        targetGridScroll += 1.0;
    }
}

void drawPhaseTransition() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("PHASE " + String(currentPhase) + " COMPLETE", 120, 15);
    
    canvas.setTextSize(1);
    canvas.setTextColor(CP_DIM);
    String formulaStr = "1000*" + String(selectedGridSize) + "*" + String(currentPhase) + "*(" + String(1.0 + lastTimeRatio, 4) + ")=" + String(lastPhaseScore);
    canvas.drawCenterString(formulaStr, 120, 35);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("ACCUMULATED SCORE: " + String(accumulatedScore), 120, 50);
    
    if (currentPhase < 8) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NEXT PHASE TIME: " + String(phaseTimes[currentPhase]/1000) + "s", 120, 70);
        
        uint16_t colCont = (phaseMenuFocus == 0) ? CP_YELLOW : WHITE;
        drawChippedButton(20, 95, 90, 20, colCont);
        canvas.setTextColor(colCont);
        drawGlitchText("CONTINUE", 65, 100, 1, colCont);
        
        uint16_t colSave = (phaseMenuFocus == 1) ? CP_YELLOW : WHITE;
        drawChippedButton(130, 95, 90, 20, colSave);
        canvas.setTextColor(colSave);
        drawGlitchText("SAVE & EXIT", 175, 100, 1, colSave);
    } else {
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("ALL PHASES COMPLETE!", 120, 70);
        uint16_t colSave = CP_YELLOW;
        drawChippedButton(75, 95, 90, 20, colSave);
        canvas.setTextColor(colSave);
        drawGlitchText("SAVE SCORE", 120, 100, 1, CP_YELLOW);
    }
    
    pushCanvas();
}

void handlePhaseTransitionInput(Keyboard_Class::KeysState status) {
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (currentPhase >= 8 || phaseMenuFocus == 1) {
            drawMessage("SAVING SCORE...");
            submitScore(accumulatedScore);
            enterMainMenu();
        } else {
            currentPhase++;
            initGame();
            appState = STATE_PLAYING;
            drawScreen();
        }
        return;
    }
    if (currentPhase < 8) {
        bool hasLeft = false, hasRight = false;
        for (char c : status.word) {
            if (c == ',') hasLeft = true;
            if (c == '/') hasRight = true;
        }
        if (hasLeft || hasRight) {
            phaseMenuFocus = (phaseMenuFocus == 0) ? 1 : 0;
            playSound(sound_hover, sound_hover_size);
        }
    }
}

void drawGameOverFailed() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_RED);
    canvas.setTextSize(2);
    canvas.drawCenterString("SYSTEM LOCKED", 120, 30);
    canvas.setTextSize(1);
    canvas.drawCenterString("ALL ACCUMULATED SCORE LOST", 120, 60);
    uint16_t btnColor = blinkState ? CP_RED : CP_DIM;
    drawChippedButton(70, 95, 100, 20, btnColor);
    canvas.setTextColor(btnColor);
    canvas.setTextSize(1);
    drawGlitchText("PRESS ENTER", 120, 101, 1, btnColor);
    pushCanvas();
}

void drawLeaderboard() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("GLOBAL SCORES", 120, 10);
    
    canvas.setTextSize(1);
    
    if (globalLeaderboard.size() == 0) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("[ NO NETWORK DATA ]", 120, 60);
    } else {
        int y = 35;
        for (int i = leaderboardScrollOffset; i < min(leaderboardScrollOffset + 3, (int)globalLeaderboard.size()); i++) {
            if (i == leaderboardCursor) {
                canvas.fillRect(0, y - 2, 240, 24, canvas.color565(40, 40, 40));
            }
            canvas.setTextColor(CP_RED);
            canvas.setCursor(5, y);
            canvas.print("#" + String(i + 1));
            
            canvas.setTextColor(WHITE);
            canvas.setCursor(30, y);
            canvas.print(globalLeaderboard[i].username);
            
            canvas.setTextColor(CP_DIM);
            canvas.setCursor(135, y);
            canvas.print(globalLeaderboard[i].date);
            
            canvas.setTextColor(CP_CYAN);
            canvas.setCursor(30, y + 10);
            canvas.print(String(globalLeaderboard[i].grid) + "x" + String(globalLeaderboard[i].grid) + " Phase " + String(globalLeaderboard[i].phase));
            
            canvas.setTextColor(CP_YELLOW);
            canvas.setCursor(135, y + 10);
            canvas.print("SCORE: " + String(globalLeaderboard[i].score));
            
            y += 25;
        }
    }
    
    canvas.setTextColor(WHITE);
    canvas.drawCenterString("Press ENTER to return", 120, 115);
    pushCanvas();
}

void initGame(bool keepDiff) {
    // Generate hexCodes based on current network BSSID or hardware MAC address
    String mac = WiFi.BSSIDstr();
    if (mac == "" || mac == "00:00:00:00:00:00") {
        mac = WiFi.macAddress();
    }
    
    String octets[6];
    int oIdx = 0;
    for (int i = 0; i < mac.length(); i++) {
        if (mac[i] == ':') {
            oIdx++;
        } else {
            if (oIdx < 6) octets[oIdx] += mac[i];
        }
    }
    
    String unique[7];
    int uCount = 0;
    for (int i = 0; i < 6; i++) {
        String code = octets[i];
        code.toUpperCase();
        if (code.length() != 2) continue;
        
        bool dup = false;
        for (int j = 0; j < uCount; j++) {
            if (unique[j] == code) { dup = true; break; }
        }
        if (!dup && uCount < 7) {
            unique[uCount++] = code;
        }
    }
    
    String fallbacks[] = {"1C", "55", "BD", "E9", "FF", "7A", "42"};
    for (int i = 0; i < 7; i++) {
        if (uCount >= 7) break;
        String code = fallbacks[i];
        bool dup = false;
        for (int j = 0; j < uCount; j++) {
            if (unique[j] == code) { dup = true; break; }
        }
        if (!dup) {
            unique[uCount++] = code;
        }
    }
    
    for (int i = 0; i < 7; i++) {
        hexCodes[i] = unique[i];
    }

    lastBreachFailed = false;
    gridSize = selectedGridSize;
    maxTime = phaseTimes[currentPhase - 1];
    
    if (gridSize == 3) targetSize = 3;
    else targetSize = 4;

    for(int i=0; i<gridSize; i++) {
        for(int j=0; j<gridSize; j++) {
            matrix[i][j] = hexCodes[random(7)];
        }
    }
    
    int r = 0, c = random(gridSize);
    targetSeq[0] = matrix[r][c];
    
    bool visited[5][5] = {false};
    for(int i=0; i<5; i++) {
        for(int j=0; j<5; j++) visited[i][j] = false;
    }
    visited[r][c] = true;
    
    bool makeRow = false;
    for(int i=1; i<targetSize; i++) {
        int avail[5];
        int count = 0;
        if (makeRow) {
            for(int cc=0; cc<gridSize; cc++) if(!visited[r][cc]) avail[count++] = cc;
            if(count > 0) c = avail[random(count)];
        } else {
            for(int rr=0; rr<gridSize; rr++) if(!visited[rr][c]) avail[count++] = rr;
            if(count > 0) r = avail[random(count)];
        }
        visited[r][c] = true;
        targetSeq[i] = matrix[r][c];
        makeRow = !makeRow;
    }
    
    bufferIndex = 0;
    for(int i=0; i<gridSize; i++) buffer[i] = "";
    
    isRowActive = true;
    activeRow = 0;
    cursorIdx = 0;
    
    timeLeft = maxTime;
    gameOver = false;
    hackSuccess = false;
    lastUpdate = millis();
    isAnimating = false;
    blinkState = false;
    lastBlink = millis();
}

void initSPIFFS() {
    if (SPIFFS.begin(true)) {
        if (!SPIFFS.exists("/deck_config.json")) {
            File f = SPIFFS.open("/deck_config.json", FILE_WRITE);
            if (f) {
                f.println("{");
                f.println("  \"deck_id\": \"CYBER_D_01\",");
                f.println("  \"os\": \"Breach_OS\",");
                f.println("  \"icebreaker\": \"v4.2\"");
                f.println("}");
                f.close();
            }
        }
        if (!SPIFFS.exists("/breach_log.txt")) {
            File f = SPIFFS.open("/breach_log.txt", FILE_WRITE);
            if (f) {
                f.println("BREACH ACCESS LOG:");
                f.println("OP: sl01220");
                f.println("STATUS: COMPLETED");
                f.close();
            }
        }
        if (!SPIFFS.exists("/system.ini")) {
            File f = SPIFFS.open("/system.ini", FILE_WRITE);
            if (f) {
                f.println("[system]");
                f.println("node=BP_X1");
                f.println("security=HIGH");
                f.close();
            }
        }
    }
}

void drawTimer(bool forceRedraw = false) {
    static int lastBarWidth = -1;
    static int lastTimeLeft = -1;
    if (forceRedraw) { lastBarWidth = -1; lastTimeLeft = -1; }
    
    int barWidth = map(timeLeft, 0, maxTime, 0, 80);
    if (barWidth < 0) barWidth = 0;
    
    if (barWidth == lastBarWidth && timeLeft == lastTimeLeft) return;
    
    canvas.startWrite();
    
    if (timeLeft != lastTimeLeft) {
        int secs = timeLeft / 1000;
        int centis = (timeLeft % 1000) / 10;
        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", secs, centis);
        canvas.setTextColor(CP_YELLOW, CP_PANEL);
        canvas.setTextSize(1);
        canvas.setCursor(95, 5);
        canvas.print(timeStr);
        // Clear any ghosting after the timer string
        canvas.print("  ");
    }

    if (barWidth != lastBarWidth) {
        if (lastBarWidth == -1 || barWidth > lastBarWidth) {
            canvas.fillRect(146, 6, barWidth, 6, CP_YELLOW);
            canvas.fillRect(146 + barWidth, 6, 80 - barWidth, 6, CP_PANEL);
        } else {
            canvas.fillRect(146 + barWidth, 6, lastBarWidth - barWidth, 6, CP_PANEL);
        }
    }
    pushCanvas();
    
    lastBarWidth = barWidth;
    lastTimeLeft = timeLeft;
}

void drawScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.fillRect(0, 0, 240, 18, CP_PANEL);
    canvas.setTextColor(CP_YELLOW, CP_PANEL);
    canvas.setTextSize(1);
    canvas.setCursor(5, 5);
    canvas.print("PHASE: " + String(currentPhase));
    
    canvas.drawRect(144, 4, 84, 10, CP_YELLOW);
    drawTimer(true);

    // TARGET
    canvas.setTextColor(WHITE, CP_BG);
    canvas.setCursor(120, 25);
    canvas.print("SEQUENCE REQUIRED");
    
    int matchLen = 0;
    for (int len = min(bufferIndex, targetSize); len > 0; len--) {
        bool match = true;
        for (int i = 0; i < len; i++) {
            if (buffer[bufferIndex - len + i] != targetSeq[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            matchLen = len;
            break;
        }
    }
    
    int boxW = min(22, (120 / gridSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;

    for(int i=0; i<targetSize; i++) {
        int tx = 120 + i*boxStep;
        int ty = 38;
        
        if (i < matchLen) {
            canvas.drawRect(tx, ty, boxW, 18, CP_CYAN);
            canvas.setTextColor(CP_CYAN, CP_BG);
        } else {
            canvas.drawRect(tx, ty, boxW, 18, CP_DIM);
            canvas.setTextColor(WHITE, CP_BG);
        }
        canvas.setCursor(tx + txtOff, ty + 5);
        canvas.print(targetSeq[i]);
    }
    
    // BUFFER
    canvas.setTextColor(CP_YELLOW, CP_BG);
    canvas.setCursor(120, 70);
    canvas.print("BUFFER");
    
    for(int i=0; i<gridSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        
        if (i < bufferIndex) {
            canvas.fillRect(bx, by, boxW, 18, CP_CYAN);
            canvas.setTextColor(BLACK, CP_CYAN);
            canvas.setCursor(bx + txtOff, by + 5);
            canvas.print(buffer[i]);
        } else if (i == bufferIndex) {
            canvas.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
        } else {
            canvas.drawRect(bx, by, boxW, 18, CP_DIM);
        }
    }
    
    if (gameOver && !isAnimating) {
        uint16_t color = hackSuccess ? canvas.color565(66, 245, 84) : CP_RED;
        canvas.setTextSize(2);
        canvas.setTextColor(color, CP_BG);
        canvas.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", 170, 106);
        canvas.setTextSize(1);
        canvas.setTextColor(WHITE, CP_BG);
        canvas.drawCenterString("Press ENTER", 170, 125);
        canvas.fillRect(120, 102, 100, 2, color);
    }
    
    // MATRIX
    int startX = 5 + (5 - gridSize) * 11;
    int startY = 30 + (5 - gridSize) * 10;
    
    if (isRowActive) {
        canvas.fillRect(startX, startY + activeRow*20, gridSize*22, 18, CP_ACTIVE_LINE);
    } else {
        canvas.fillRect(startX + activeCol*22, startY, 20, gridSize*20, CP_ACTIVE_LINE);
    }
    
    for(int i=0; i<gridSize; i++) {
        for(int j=0; j<gridSize; j++) {
            int cx = startX + j*22;
            int cy = startY + i*20;
            
            bool isActiveLine = (isRowActive && i == activeRow) || (!isRowActive && j == activeCol);
            uint16_t bgColor = isActiveLine ? CP_ACTIVE_LINE : CP_BG;
            
            if (matrix[i][j] == "") {
                canvas.setTextColor(CP_DIM, bgColor);
                canvas.setCursor(cx + 4, cy + 5);
                canvas.print("[]");
                continue;
            }
            
            bool isHovered = (isRowActive && i == activeRow && j == cursorIdx) || 
                             (!isRowActive && j == activeCol && i == cursorIdx);
            
            if (isHovered) {
                canvas.fillRect(cx, cy, 20, 18, CP_CYAN);
                canvas.setTextColor(BLACK, CP_CYAN);
            } else if (isActiveLine) {
                canvas.setTextColor(CP_YELLOW, CP_ACTIVE_LINE);
            } else {
                canvas.setTextColor(WHITE, CP_BG);
            }
            canvas.setCursor(cx + 4, cy + 5);
            canvas.print(matrix[i][j]);
        }
    }
    pushCanvas();
}

void updateAnimation() {
    unsigned long t = millis() - animStartTime;
    canvas.startWrite();
    uint16_t color = hackSuccess ? canvas.color565(66, 245, 84) : CP_RED;
    
    int boxW = min(22, (120 / gridSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;
    
    int bufferCenter = 120 + (gridSize * boxStep) / 2;

    for(int i=0; i<gridSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        
        float distFromCenter = abs(i - (gridSize - 1.0) / 2.0);
        long delayStart = distFromCenter * 150;
        
        if (t > delayStart) {
            int fillHeight = map(t - delayStart, 0, 300, 0, 18);
            if (fillHeight > 18) fillHeight = 18;
            canvas.fillRect(bx, by + 18 - fillHeight, boxW, fillHeight, color);
            if (i < bufferIndex) {
                canvas.setTextColor(BLACK);
                canvas.setCursor(bx + txtOff, by + 5);
                canvas.print(buffer[i]);
            }
        }
    }
    
    int barWidth = map(t, 0, 600, 0, 100);
    if (barWidth > 100) barWidth = 100;
    canvas.fillRect(bufferCenter - barWidth/2, 105, barWidth, 2, color);
    
    if (t > 600 && t < 650) {
        canvas.setTextSize(2);
        canvas.setTextColor(color, CP_BG);
        canvas.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", bufferCenter, 110);
        canvas.setTextSize(1);
        canvas.setTextColor(WHITE, CP_BG);
        canvas.drawCenterString("Press ENTER", bufferCenter, 125);
    }
    pushCanvas();
}
