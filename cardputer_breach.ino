#include "M5Cardputer.h"

// Cyberpunk Colors in RGB565
#define CP_YELLOW M5Cardputer.Display.color565(220, 244, 27)
#define CP_CYAN M5Cardputer.Display.color565(56, 190, 201)
#define CP_RED M5Cardputer.Display.color565(255, 0, 60)
#define CP_BG M5Cardputer.Display.color565(14, 17, 21)
#define CP_PANEL M5Cardputer.Display.color565(14, 17, 21)
#define CP_ACTIVE_LINE M5Cardputer.Display.color565(44, 53, 71)
#define CP_DIM M5Cardputer.Display.color565(88, 97, 10)

String hexCodes[] = {"1C", "55", "BD", "E9", "FF", "7A", "42"};

int gridSize = 5;
int targetSize = 4;

String matrix[5][5];
String targetSeq[6];
String buffer[6];
int bufferIndex = 0;

int activeRow = 0;
int activeCol = 0;
bool isRowActive = true; 
int cursorIdx = 0;

int maxTime = 3000;
int timeLeft = 3000;
unsigned long lastUpdate = 0;
bool gameOver = false;
bool hackSuccess = false;

unsigned long animStartTime = 0;
bool isAnimating = false;
bool blinkState = false;
unsigned long lastBlink = 0;

void initGame(bool keepDiff = false) {
    if (!keepDiff) {
        int sizes[] = {3, 4, 5};
        gridSize = sizes[random(3)];
        if (gridSize == 3) maxTime = 1500;
        else if (gridSize == 4) maxTime = 2000;
        else maxTime = 2500;
        
        targetSize = random(4, 7); // 4, 5, or 6
    }

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
    for(int i=0; i<targetSize; i++) buffer[i] = "";
    
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

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setRotation(1);
    
    initGame();
    drawScreen();
}

void drawTimer(bool forceRedraw = false) {
    static int lastBarWidth = -1;
    static int lastTimeLeft = -1;
    if (forceRedraw) { lastBarWidth = -1; lastTimeLeft = -1; }
    
    int barWidth = map(timeLeft, 0, maxTime, 0, 80);
    if (barWidth < 0) barWidth = 0;
    
    if (barWidth == lastBarWidth && timeLeft == lastTimeLeft) return;
    
    M5Cardputer.Display.startWrite();
    
    if (timeLeft != lastTimeLeft) {
        int secs = timeLeft / 100;
        int centis = timeLeft % 100;
        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", secs, centis);
        M5Cardputer.Display.setTextColor(CP_YELLOW, CP_PANEL);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(95, 5);
        M5Cardputer.Display.print(timeStr);
    }

    if (barWidth != lastBarWidth) {
        if (lastBarWidth == -1 || barWidth > lastBarWidth) {
            M5Cardputer.Display.fillRect(146, 6, barWidth, 6, CP_YELLOW);
            M5Cardputer.Display.fillRect(146 + barWidth, 6, 80 - barWidth, 6, CP_PANEL);
        } else {
            M5Cardputer.Display.fillRect(146 + barWidth, 6, lastBarWidth - barWidth, 6, CP_PANEL);
        }
    }
    M5Cardputer.Display.endWrite();
    
    lastBarWidth = barWidth;
    lastTimeLeft = timeLeft;
}

void drawScreen() {
    M5Cardputer.Display.startWrite();
    M5Cardputer.Display.fillScreen(CP_BG);
    
    M5Cardputer.Display.fillRect(0, 0, 240, 18, CP_PANEL);
    M5Cardputer.Display.setTextColor(CP_YELLOW, CP_PANEL);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.print("BREACH TIME");
    
    M5Cardputer.Display.drawRect(144, 4, 84, 10, CP_YELLOW);
    drawTimer(true);

    // TARGET
    M5Cardputer.Display.setTextColor(WHITE, CP_BG);
    M5Cardputer.Display.setCursor(120, 25);
    M5Cardputer.Display.print("SEQUENCE REQUIRED");
    
    int matchLen = 0;
    for (int i=0; i<bufferIndex; i++) {
        if (buffer[i] == targetSeq[i]) matchLen++;
        else break;
    }
    
    int boxW = min(22, (120 / targetSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;

    for(int i=0; i<targetSize; i++) {
        int tx = 120 + i*boxStep;
        int ty = 38;
        
        if (i < matchLen) {
            M5Cardputer.Display.drawRect(tx, ty, boxW, 18, CP_CYAN);
            M5Cardputer.Display.setTextColor(CP_CYAN, CP_BG);
        } else {
            M5Cardputer.Display.drawRect(tx, ty, boxW, 18, CP_DIM);
            M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        }
        M5Cardputer.Display.setCursor(tx + txtOff, ty + 5);
        M5Cardputer.Display.print(targetSeq[i]);
    }
    
    // BUFFER
    M5Cardputer.Display.setTextColor(CP_YELLOW, CP_BG);
    M5Cardputer.Display.setCursor(120, 70);
    M5Cardputer.Display.print("BUFFER");
    
    for(int i=0; i<targetSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        
        if (i < bufferIndex) {
            M5Cardputer.Display.fillRect(bx, by, boxW, 18, CP_CYAN);
            M5Cardputer.Display.setTextColor(BLACK, CP_CYAN);
            M5Cardputer.Display.setCursor(bx + txtOff, by + 5);
            M5Cardputer.Display.print(buffer[i]);
        } else if (i == bufferIndex) {
            M5Cardputer.Display.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
        } else {
            M5Cardputer.Display.drawRect(bx, by, boxW, 18, CP_DIM);
        }
    }
    
    if (gameOver && !isAnimating) {
        uint16_t color = hackSuccess ? M5Cardputer.Display.color565(66, 245, 84) : CP_RED;
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(color, CP_BG);
        M5Cardputer.Display.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", 170, 106);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        M5Cardputer.Display.drawCenterString("Press ENTER", 170, 125);
        M5Cardputer.Display.fillRect(120, 102, 100, 2, color);
    }
    
    // MATRIX (dynamically centered)
    int startX = 5 + (5 - gridSize) * 11;
    int startY = 30 + (5 - gridSize) * 10;
    
    if (isRowActive) {
        M5Cardputer.Display.fillRect(startX, startY + activeRow*20, gridSize*22, 18, CP_ACTIVE_LINE);
    } else {
        M5Cardputer.Display.fillRect(startX + activeCol*22, startY, 20, gridSize*20, CP_ACTIVE_LINE);
    }
    
    for(int i=0; i<gridSize; i++) {
        for(int j=0; j<gridSize; j++) {
            int cx = startX + j*22;
            int cy = startY + i*20;
            
            bool isActiveLine = (isRowActive && i == activeRow) || (!isRowActive && j == activeCol);
            uint16_t bgColor = isActiveLine ? CP_ACTIVE_LINE : CP_BG;
            
            if (matrix[i][j] == "") {
                M5Cardputer.Display.setTextColor(CP_DIM, bgColor);
                M5Cardputer.Display.setCursor(cx + 4, cy + 5);
                M5Cardputer.Display.print("[]");
                continue;
            }
            
            bool isHovered = (isRowActive && i == activeRow && j == cursorIdx) || 
                             (!isRowActive && j == activeCol && i == cursorIdx);
            
            if (isHovered) {
                M5Cardputer.Display.fillRect(cx, cy, 20, 18, CP_CYAN);
                M5Cardputer.Display.setTextColor(BLACK, CP_CYAN);
            } else if (isActiveLine) {
                M5Cardputer.Display.setTextColor(CP_YELLOW, CP_ACTIVE_LINE);
            } else {
                M5Cardputer.Display.setTextColor(WHITE, CP_BG);
            }
            M5Cardputer.Display.setCursor(cx + 4, cy + 5);
            M5Cardputer.Display.print(matrix[i][j]);
        }
    }
    M5Cardputer.Display.endWrite();
}

void updateAnimation() {
    unsigned long t = millis() - animStartTime;
    M5Cardputer.Display.startWrite();
    uint16_t color = hackSuccess ? M5Cardputer.Display.color565(66, 245, 84) : CP_RED;
    
    int boxW = min(22, (120 / targetSize) - 2);
    int boxStep = boxW + 2;
    int txtOff = (boxW - 12) / 2;
    if (txtOff < 0) txtOff = 0;

    for(int i=0; i<targetSize; i++) {
        int bx = 120 + i*boxStep;
        int by = 83;
        long delayStart = i * 100;
        if (t > delayStart) {
            int fillHeight = map(t - delayStart, 0, 300, 0, 18);
            if (fillHeight > 18) fillHeight = 18;
            M5Cardputer.Display.fillRect(bx, by + 18 - fillHeight, boxW, fillHeight, color);
            if (i < bufferIndex) {
                M5Cardputer.Display.setTextColor(BLACK);
                M5Cardputer.Display.setCursor(bx + txtOff, by + 5);
                M5Cardputer.Display.print(buffer[i]);
            }
        }
    }
    
    int barWidth = map(t, 0, 600, 0, 100);
    if (barWidth > 100) barWidth = 100;
    M5Cardputer.Display.fillRect(170 - barWidth/2, 102, barWidth, 2, color);
    
    if (t > 600 && t < 650) {
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(color, CP_BG);
        M5Cardputer.Display.drawCenterString(hackSuccess ? "SUCCESS" : "FAILED", 170, 106);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(WHITE, CP_BG);
        M5Cardputer.Display.drawCenterString("Press ENTER", 170, 125);
    }
    M5Cardputer.Display.endWrite();
}

void loop() {
    M5Cardputer.update();
    
    unsigned long now = millis();
    if (!gameOver && now - lastUpdate > 10) {
        timeLeft -= (now - lastUpdate) / 10;
        lastUpdate = now;
        if (timeLeft <= 0) {
            timeLeft = 0;
            gameOver = true;
            hackSuccess = false;
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
            M5Cardputer.Display.startWrite();
            M5Cardputer.Display.drawRect(bx, by, boxW, 18, blinkState ? CP_CYAN : CP_DIM);
            M5Cardputer.Display.endWrite();
        }
    }
    
    if (isAnimating) {
        updateAnimation();
        if (now - animStartTime > 1000) {
            isAnimating = false;
        }
    }
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        
        if (gameOver) {
            if (status.enter) {
                // If hack failed, retry with same difficulty. If success, new difficulty.
                initGame(!hackSuccess);
                drawScreen();
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
            if (isRowActive) {
                for(int j=cC_curr-1; j>=0; j--) { if (matrix[cR_curr][j] != "") { cursorIdx = j; break; } }
            } else {
                for(int i=cR_curr-1; i>=0; i--) { if (matrix[i][cC_curr] != "") { cursorIdx = i; break; } }
            }
        }
        if (hasSlash || hasD || hasDot || hasS) {
            if (isRowActive) {
                for(int j=cC_curr+1; j<gridSize; j++) { if (matrix[cR_curr][j] != "") { cursorIdx = j; break; } }
            } else {
                for(int i=cR_curr+1; i<gridSize; i++) { if (matrix[i][cC_curr] != "") { cursorIdx = i; break; } }
            }
        }
        
        if (status.enter) {
            int cR = isRowActive ? activeRow : cursorIdx;
            int cC = isRowActive ? cursorIdx : activeCol;
            
            if (matrix[cR][cC] != "") {
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
                for(int i=0; i<bufferIndex; i++) {
                    if (buffer[i] != targetSeq[i]) {
                        isPrefix = false;
                        break;
                    }
                }
                
                if (!isPrefix || bufferIndex >= targetSize) {
                    gameOver = true;
                    if (isPrefix && bufferIndex == targetSize) {
                        hackSuccess = true;
                    } else {
                        hackSuccess = false;
                    }
                    isAnimating = true;
                    animStartTime = millis();
                }
            }
        }
        drawScreen();
    }
    delay(10);
}
