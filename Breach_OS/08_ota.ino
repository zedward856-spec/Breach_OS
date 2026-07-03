// OTA catalog browsing and firmware update flow.

const int OTA_CATALOG_PAGE_SIZE = 10;
const int OTA_DETAIL_DESC_MAX_CHARS = 540;
const int OTA_DETAIL_DESC_LINES = 3;
const char OTA_M5LAUNCHER_CARDPUTER_FID[] = "967e0377b9889c7b82f059fb8a30adda";
const char OTA_M5LAUNCHER_CARDPUTER_DESC[] =
    "M5Launcher for Cardputer. Turn your device into a swiss knife: load .bin files from SD, "
    "download wirelessly from M5Burner, or send binaries from your computer or phone through its WebUI. "
    "Tutorial: https://youtu.be/odlHWZ03shI Support: https://buymeacoffee.com/bmorcelliz "
    "Wiki: https://github.com/bmorcelli/Launcher/wiki/Obtaining-binaries-to-launch";
bool otaDescTransferDataValid = false;
size_t otaDescTransferDoneBytes = 0;
size_t otaDescTransferTotalBytes = 0;
unsigned long otaDescTransferStartedAt = 0;
const unsigned long OTA_DETAIL_ANIM_FRAME_MS = 45;

void enterOtaCatalog() {
    otaDetailMode = false;
    otaCatalogLoaded = false;
    otaCatalogScrollOffset = 0;
    otaCatalogFocus = 0;
    otaCatalogPage = 1;
    otaCatalogTotal = 0;
    otaVersionFocus = 0;
    otaDescScrollOffset = 0;
    otaVersions.clear();
    appState = STATE_OTA_CATALOG;
    drawOtaCatalog();
}

String cleanOtaDescription(String text) {
    text.trim();
    text.replace("\r", " ");
    text.replace("\n", " ");
    text.replace("\t", " ");
    while (text.indexOf("  ") >= 0) text.replace("  ", " ");

    if (text == "" || text == "null") return "No description provided.";
    if (text.length() > OTA_DETAIL_DESC_MAX_CHARS) text = text.substring(0, OTA_DETAIL_DESC_MAX_CHARS - 3) + "...";
    return text;
}

String formatOtaBytes(size_t bytes) {
    if (bytes == 0) return "--";
    if (bytes >= 1048576) return String((float)bytes / 1048576.0f, 1) + " MB";
    if (bytes >= 1024) return String((float)bytes / 1024.0f, 1) + " KB";
    return String(bytes) + " B";
}

String formatOtaBytesOrUnknown(size_t bytes) {
    return bytes == 0 ? String("?") : formatOtaBytes(bytes);
}

String formatOtaEta(unsigned long seconds) {
    if (seconds == 0) return "--";
    if (seconds >= 60) return String(seconds / 60) + "m " + String(seconds % 60) + "s";
    return String(seconds) + "s";
}

String formatOtaDataRatio(size_t doneBytes, size_t totalBytes) {
    String doneText = (doneBytes == 0) ? String("0 B") : formatOtaBytes(doneBytes);
    String totalText = (totalBytes == 0) ? String(doneBytes == 0 ? "0 B" : "?") : formatOtaBytes(totalBytes);
    return doneText + "/" + totalText;
}

String formatOtaDownloadedBytes(size_t bytes) {
    return bytes == 0 ? String("0 B") : formatOtaBytes(bytes);
}

int calculateOtaStreamProgress(size_t doneBytes, size_t totalBytes) {
    if (totalBytes == 0) return 0;
    int progress = (int)(((uint64_t)doneBytes * 100ULL) / totalBytes);
    if (progress > 99) progress = 99;
    return progress;
}

void drawOtaLoadingStrip(int x, int y, int w, int h, uint16_t color, unsigned long animStartedAt) {
    canvas.drawRect(x, y, w, h, color);
    int innerX = x + 2;
    int innerY = y + 2;
    int innerW = w - 4;
    int innerH = h - 4;
    canvas.fillRect(innerX, innerY, innerW, innerH, CP_BG);

    int stripW = innerW / 4;
    if (stripW < 22) stripW = 22;
    int span = innerW + stripW;
    unsigned long elapsed = (animStartedAt > 0 && millis() > animStartedAt) ? millis() - animStartedAt : 0;
    int stripX = innerX - stripW + (int)((elapsed / 24) % span);
    int clipX = stripX;
    int clipW = stripW;
    if (clipX < innerX) {
        clipW -= innerX - clipX;
        clipX = innerX;
    }
    if (clipX + clipW > innerX + innerW) clipW = innerX + innerW - clipX;
    if (clipW > 0) canvas.fillRect(clipX, innerY, clipW, innerH, color);
}

void drawOtaTransferProgress(int progress, String statusText, size_t doneBytes, size_t totalBytes, unsigned long startedAt, uint16_t color) {
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    unsigned long elapsed = (startedAt > 0 && millis() > startedAt) ? millis() - startedAt : 0;
    size_t bytesPerSecond = (elapsed > 0 && doneBytes > 0) ? (size_t)(((uint64_t)doneBytes * 1000ULL) / elapsed) : 0;
    unsigned long etaSeconds = 0;
    if (bytesPerSecond > 0 && totalBytes > doneBytes) {
        etaSeconds = (totalBytes - doneBytes) / bytesPerSecond;
        if (etaSeconds == 0) etaSeconds = 1;
    }

    canvas.startWrite();
    canvas.fillScreen(CP_BG);

    drawChippedButton(10, 8, 220, 120, color);
    drawChippedButton(12, 10, 216, 116, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- CYBERDECK OTA STREAM ---", 120, 16);
    canvas.drawLine(18, 28, 222, 28, color);

    canvas.setTextColor(color);
    canvas.drawCenterString(statusText, 120, 38);

    canvas.drawRect(35, 55, 170, 14, color);
    int fillW = (166 * progress) / 100;
    if (fillW > 0) canvas.fillRect(37, 57, fillW, 10, color);

    canvas.setTextColor(WHITE);
    canvas.drawCenterString(String(progress) + "%", 120, 72);

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(24, 88);
    canvas.print("DATA: " + formatOtaDataRatio(doneBytes, totalBytes));
    canvas.setCursor(24, 100);
    canvas.print("RATE: " + formatOtaBytes(bytesPerSecond) + "/s");
    canvas.setCursor(24, 112);
    canvas.print("ETA : " + formatOtaEta(etaSeconds));

    pushCanvas();
}

void drawOtaDetailProgressWithData(int versionProgress, int descProgress, size_t doneBytes, size_t totalBytes, unsigned long startedAt, uint16_t color, String dataLabel, size_t dataDoneBytes, size_t dataTotalBytes) {
    if (versionProgress < 0) versionProgress = 0;
    if (versionProgress > 100) versionProgress = 100;
    if (descProgress < 0) descProgress = 0;
    if (descProgress > 100) descProgress = 100;
    if (dataLabel == "") dataLabel = "DATA";

    unsigned long elapsed = (startedAt > 0 && millis() > startedAt) ? millis() - startedAt : 0;
    size_t bytesPerSecond = (elapsed > 0 && doneBytes > 0) ? (size_t)(((uint64_t)doneBytes * 1000ULL) / elapsed) : 0;

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(10, 8, 220, 120, color);
    drawChippedButton(12, 10, 216, 116, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- FIRMWARE DETAIL STREAM ---", 120, 15);
    canvas.drawLine(18, 26, 222, 26, color);
    canvas.drawCenterString("VERSION NUMS + DESCRIPTION", 120, 31);

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(24, 43);
    canvas.print("VERSION NUMS");
    canvas.setCursor(184, 43);
    canvas.print(String(versionProgress) + "%");
    canvas.drawRect(35, 54, 170, 11, CP_CYAN);
    int versionFill = (166 * versionProgress) / 100;
    if (versionFill > 0) canvas.fillRect(37, 56, versionFill, 7, CP_CYAN);

    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(24, 70);
    canvas.print("DESCRIPTION");
    if (descProgress >= 100) {
        canvas.drawRect(35, 81, 170, 11, CP_GREEN);
        canvas.fillRect(37, 83, 166, 7, CP_GREEN);
    } else if (versionProgress < 100) {
        canvas.drawRect(35, 81, 170, 11, CP_YELLOW);
        canvas.fillRect(37, 83, 166, 7, CP_BG);
    } else {
        drawOtaLoadingStrip(35, 81, 170, 11, CP_YELLOW, startedAt);
    }

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(24, 94);
    canvas.print(dataLabel + ": " + formatOtaDownloadedBytes(dataDoneBytes));
    canvas.setCursor(24, 107);
    canvas.print("RATE: " + formatOtaBytes(bytesPerSecond) + "/s");

    pushCanvas();
}

void drawOtaDetailProgress(int versionProgress, int descProgress, size_t doneBytes, size_t totalBytes, unsigned long startedAt, uint16_t color) {
    drawOtaDetailProgressWithData(versionProgress, descProgress, doneBytes, totalBytes, startedAt, color, "DATA", doneBytes, totalBytes);
}

void resetOtaDescriptionTransferStats() {
    otaDescTransferDataValid = false;
    otaDescTransferDoneBytes = 0;
    otaDescTransferTotalBytes = 0;
    otaDescTransferStartedAt = 0;
}

void rememberOtaDescriptionTransfer(size_t doneBytes, size_t totalBytes, unsigned long startedAt) {
    otaDescTransferDataValid = true;
    otaDescTransferDoneBytes = doneBytes;
    otaDescTransferTotalBytes = totalBytes;
    otaDescTransferStartedAt = startedAt;
}

void drawOtaDescriptionStreamProgress(int versionProgress, size_t receivedBytes, size_t, unsigned long startedAt, uint16_t color) {
    size_t visibleTotalBytes = receivedBytes;
    int progress = receivedBytes > 0 ? 99 : 0;
    drawOtaDetailProgressWithData(versionProgress, progress, receivedBytes, visibleTotalBytes, startedAt, color, "DATA", receivedBytes, visibleTotalBytes);
}

String fetchOtaFirmwareDescription(String fid, int versionProgress) {
    if (fid == OTA_M5LAUNCHER_CARDPUTER_FID) {
        unsigned long streamStart = millis();
        rememberOtaDescriptionTransfer(0, 0, streamStart);
        drawOtaDetailProgressWithData(versionProgress, 100, 0, 0, streamStart, CP_GREEN, "DATA", 0, 0);
        return String(OTA_M5LAUNCHER_CARDPUTER_DESC);
    }

    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    String itemDesc = "";
    String itemFid = "";
    String key = "";
    String token = "";
    String valueKey = "";
    itemDesc.reserve(OTA_DETAIL_DESC_MAX_CHARS + 4);
    itemFid.reserve(33);
    key.reserve(16);
    token.reserve(OTA_DETAIL_DESC_MAX_CHARS + 4);
    valueKey.reserve(16);

    if (!http.begin(secureClient, "https://api.launcherhub.net/giveMeTheList")) return "";
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return "";
    }

    WiFiClient* stream = http.getStreamPtr();
    int remaining = http.getSize();
    size_t totalBytes = (remaining > 0) ? (size_t)remaining : 0;
    size_t receivedBytes = 0;
    unsigned long streamStart = millis();
    unsigned long lastDraw = 0;
    bool inString = false;
    bool escape = false;
    bool expectingKey = false;
    bool expectingValue = false;
    bool captureToken = false;
    bool tokenTruncated = false;
    bool captureDescProgress = false;
    bool foundTargetDesc = false;
    String foundDesc = "";
    unsigned long lastDescDraw = 0;
    int depth = 0;
    unsigned long lastByte = millis();
    drawOtaDescriptionStreamProgress(versionProgress, 0, totalBytes, streamStart, CP_CYAN);
    lastDraw = streamStart;

    while (http.connected() && (remaining > 0 || remaining == -1)) {
        while (stream->available()) {
            char c = stream->read();
            if (remaining > 0) remaining--;
            receivedBytes++;
            lastByte = millis();

            if (!captureDescProgress && lastByte - lastDraw > OTA_DETAIL_ANIM_FRAME_MS) {
                drawOtaDescriptionStreamProgress(versionProgress, receivedBytes, totalBytes, streamStart, CP_CYAN);
                lastDraw = lastByte;
            }

            if (inString) {
                if (escape) {
                    if (captureToken) {
                        if (captureDescProgress) {
                            if (lastByte - lastDescDraw > OTA_DETAIL_ANIM_FRAME_MS) {
                                drawOtaDescriptionStreamProgress(versionProgress, receivedBytes, totalBytes, streamStart, CP_CYAN);
                                lastDescDraw = lastByte;
                            }
                        }
                        if (c == 'n' || c == 'r' || c == 't') {
                            if (token.length() < OTA_DETAIL_DESC_MAX_CHARS) token += ' ';
                            else tokenTruncated = true;
                        } else if (token.length() < OTA_DETAIL_DESC_MAX_CHARS) token += c;
                        else tokenTruncated = true;
                    }
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == '"') {
                    inString = false;

                    if (depth == 1 && captureToken) {
                        if (expectingKey) {
                            key = token;
                            expectingKey = false;
                        } else if (expectingValue) {
                            if (valueKey == "description") {
                                itemDesc = token;
                                if (tokenTruncated) itemDesc += "...";
                                if (itemFid == fid) {
                                    foundDesc = itemDesc;
                                    foundTargetDesc = true;
                                }
                            } else if (valueKey == "fid") {
                                itemFid = token;
                                if (itemFid == fid && itemDesc != "") {
                                    foundDesc = itemDesc;
                                    foundTargetDesc = true;
                                }
                            }
                            expectingValue = false;
                        }
                    }
                    token = "";
                    captureToken = false;
                    captureDescProgress = false;
                    tokenTruncated = false;
                } else if (captureToken) {
                    if (captureDescProgress) {
                        if (lastByte - lastDescDraw > OTA_DETAIL_ANIM_FRAME_MS) {
                            drawOtaDescriptionStreamProgress(versionProgress, receivedBytes, totalBytes, streamStart, CP_CYAN);
                            lastDescDraw = lastByte;
                        }
                    }
                    if (token.length() < OTA_DETAIL_DESC_MAX_CHARS) token += c;
                    else tokenTruncated = true;
                }
                if (foundTargetDesc) break;
                continue;
            }

            if (c == '"') {
                inString = true;
                captureToken = (depth == 1 && (expectingKey || expectingValue));
                if (captureToken) {
                    token = "";
                    tokenTruncated = false;
                    captureDescProgress = (expectingValue && valueKey == "description" && itemFid == fid);
                    if (captureDescProgress) {
                        lastDescDraw = millis();
                        drawOtaDescriptionStreamProgress(versionProgress, receivedBytes, totalBytes, streamStart, CP_CYAN);
                    }
                }
            } else if (c == '{') {
                depth++;
                if (depth == 1) {
                    itemDesc = "";
                    itemFid = "";
                    key = "";
                    valueKey = "";
                    expectingKey = true;
                    expectingValue = false;
                }
            } else if (c == '}') {
                if (depth == 1 && itemFid == fid && itemDesc != "") {
                    foundDesc = itemDesc;
                    foundTargetDesc = true;
                }
                if (depth > 0) depth--;
                if (depth < 1) {
                    expectingKey = false;
                    expectingValue = false;
                }
            } else if (depth == 1) {
                if (c == ':') {
                    valueKey = key;
                    expectingValue = (valueKey == "description" || valueKey == "fid");
                } else if (c == ',') {
                    if (itemFid == fid && itemDesc != "") {
                        foundDesc = itemDesc;
                        foundTargetDesc = true;
                    }
                    key = "";
                    valueKey = "";
                    expectingKey = true;
                    expectingValue = false;
                }
            }
            if (foundTargetDesc) break;
        }
        if (foundTargetDesc) break;
        unsigned long now = millis();
        if (now - lastDraw > OTA_DETAIL_ANIM_FRAME_MS) {
            drawOtaDescriptionStreamProgress(versionProgress, receivedBytes, totalBytes, streamStart, CP_CYAN);
            lastDraw = now;
        }
        if (remaining == 0 || millis() - lastByte > 12000) break;
        delay(1);
    }

    http.end();
    if (foundTargetDesc) {
        size_t finalTotal = receivedBytes;
        rememberOtaDescriptionTransfer(receivedBytes, finalTotal, streamStart);
        drawOtaDetailProgressWithData(versionProgress, 100, finalTotal, finalTotal, streamStart, CP_GREEN, "DATA", receivedBytes, finalTotal);
        return foundDesc;
    }
    rememberOtaDescriptionTransfer(receivedBytes, receivedBytes, streamStart);
    return "";
}

std::vector<String> wrapOtaTextLines(String text, int maxWidth) {
    std::vector<String> lines;
    text.trim();

    while (text.length() > 0 && lines.size() < 48) {
        int cut = 0;
        int lastSpace = -1;
        for (int i = 0; i < text.length(); i++) {
            if (text.charAt(i) == ' ') lastSpace = i;
            String candidate = text.substring(0, i + 1);
            if (canvas.textWidth(candidate) > maxWidth) break;
            cut = i + 1;
        }

        if (cut <= 0) cut = 1;
        int nextStart = cut;
        if (cut < text.length() && lastSpace > 0) {
            cut = lastSpace;
            nextStart = lastSpace + 1;
        }

        String lineText = text.substring(0, cut);
        lineText.trim();
        text = text.substring(nextStart);
        text.trim();

        lines.push_back(lineText);
    }

    return lines;
}

int getOtaDescMaxScroll() {
    std::vector<String> lines = wrapOtaTextLines(otaDetailDesc, 216);
    int maxScroll = (int)lines.size() - OTA_DETAIL_DESC_LINES;
    return maxScroll > 0 ? maxScroll : 0;
}

int drawOtaWrappedText(String text, int x, int y, int maxWidth, int maxLines, int scrollOffset, uint16_t color) {
    std::vector<String> lines = wrapOtaTextLines(text, maxWidth);
    int lineCount = lines.size();
    int maxScroll = lineCount - maxLines;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;

    canvas.setTextColor(color);
    canvas.setTextSize(1);
    for (int line = 0; line < maxLines; line++) {
        int idx = scrollOffset + line;
        if (idx >= lineCount) break;

        String lineText = lines[idx];

        canvas.setCursor(x, y + line * 10);
        canvas.print(lineText);
    }
    return lineCount;
}

bool otaCatalogHasNextPage() {
    return !otaCatalog.empty() && (otaCatalogTotal == 0 || otaCatalogPage * OTA_CATALOG_PAGE_SIZE < otaCatalogTotal);
}

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
    
    int startY = 23;
    int rowH = 9;
    for (int idx = 0; idx < (int)otaCatalog.size(); idx++) {
        bool isFocus = (idx == otaCatalogFocus);
        int rowY = startY + idx * rowH;
        
        if (isFocus) {
            canvas.fillRect(12, rowY, 216, rowH, canvas.color565(30, 30, 30));
            canvas.drawRect(12, rowY, 216, rowH, CP_YELLOW);
            canvas.setTextColor(CP_CYAN);
        } else {
            canvas.setTextColor(WHITE);
        }
        
        canvas.setCursor(16, rowY + 1);
        canvas.print(otaCatalog[idx].name);
    }
    
    canvas.drawLine(10, 114, 230, 114, CP_CYAN);
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(12, 118);
    canvas.print("PAGE " + String(otaCatalogPage));

    if (otaCatalogHasNextPage()) {
        uint16_t nextColor = (otaCatalogFocus == (int)otaCatalog.size()) ? CP_YELLOW : CP_CYAN;
        if (otaCatalogFocus == (int)otaCatalog.size()) {
            canvas.fillRect(132, 116, 96, 13, canvas.color565(30, 30, 30));
        }
        drawChippedButton(132, 116, 96, 13, nextColor);
        canvas.setTextColor(nextColor);
        canvas.drawCenterString("NEXT PAGE >", 180, 119);
    }
    
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
        
        bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false;
        for (char c : status.word) {
            if (c == ';') hasUp = true;
            if (c == '.') hasDown = true;
            if (c == ',') hasLeft = true;
            if (c == '/') hasRight = true;
        }
        
        int maxDescScroll = getOtaDescMaxScroll();
        if (hasUp && otaDescScrollOffset > 0) {
            playSound(sound_hover, sound_hover_size);
            otaDescScrollOffset--;
            drawOtaFirmwareDetails();
        } else if (hasDown && otaDescScrollOffset < maxDescScroll) {
            playSound(sound_hover, sound_hover_size);
            otaDescScrollOffset++;
            drawOtaFirmwareDetails();
        } else if (hasLeft && !otaVersions.empty()) {
            playSound(sound_hover, sound_hover_size);
            otaVersionFocus = (otaVersionFocus - 1 + otaVersions.size()) % otaVersions.size();
            drawOtaFirmwareDetails();
        } else if (hasRight && !otaVersions.empty()) {
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
            performOtaUpdate(downloadUrl, otaVersions[otaVersionFocus].sizeBytes);
        }
        return;
    }

    bool hasEsc = false;
    for (char c : status.word) {
        if (c == '`') hasEsc = true;
    }
    
    if (hasEsc) {
        playSound(sound_select, sound_select_size);
        resumeOtaAfterWifi = false;
        appState = STATE_SPLASH;
        showSplashBootMenu = true;
        splashBootFocus = 3;
        resetSplashBootScroll();
        drawSplash();
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            resumeOtaAfterWifi = true;
            startWifiScan();
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
    
    int focusCount = otaCatalog.size() + (otaCatalogHasNextPage() ? 1 : 0);
    if (focusCount <= 0) return;

    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        otaCatalogFocus = (otaCatalogFocus - 1 + focusCount) % focusCount;
        drawOtaCatalog();
    } else if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        otaCatalogFocus = (otaCatalogFocus + 1) % focusCount;
        drawOtaCatalog();
    } else if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (otaCatalogFocus == (int)otaCatalog.size() && otaCatalogHasNextPage()) {
            otaCatalogPage++;
            otaCatalogLoaded = false;
            otaCatalog.clear();
            otaCatalogFocus = 0;
            otaCatalogScrollOffset = 0;
            drawOtaCatalog();
            return;
        }
        if (otaCatalogFocus >= (int)otaCatalog.size()) return;

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
    drawOtaTransferProgress(0, "FETCHING CATALOG DB", 0, 0, 0, CP_CYAN);
    
    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    int apiPage = ((otaCatalogPage - 1) / OTA_CATALOG_PAGE_SIZE) + 1;
    int pageOffset = ((otaCatalogPage - 1) % OTA_CATALOG_PAGE_SIZE) * OTA_CATALOG_PAGE_SIZE;
    String url = "https://api.launcherhub.net/firmwares?category=cardputer&order_by=" + otaSortField + "&page=" + String(apiPage);
    
    if (http.begin(secureClient, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            WiFiClient* stream = http.getStreamPtr();
            int contentLength = http.getSize();
            int remaining = contentLength;
            size_t totalBytes = (contentLength > 0) ? (size_t)contentLength : 0;
            size_t receivedBytes = 0;
            unsigned long streamStart = millis();
            unsigned long lastDraw = 0;
            unsigned long lastByte = millis();
            uint8_t buff[512] = { 0 };
            String payload = "";
            payload.reserve((totalBytes > 0 && totalBytes < 20000) ? totalBytes + 1 : 12000);
            
            while (http.connected() && (remaining > 0 || remaining == -1)) {
                size_t available = stream->available();
                if (available) {
                    int toRead = available > sizeof(buff) ? sizeof(buff) : available;
                    int bytesRead = stream->readBytes(buff, toRead);
                    for (int i = 0; i < bytesRead; i++) payload += (char)buff[i];
                    if (remaining > 0) remaining -= bytesRead;
                    receivedBytes += bytesRead;
                    lastByte = millis();

                    if (lastByte - lastDraw > 200) {
                        int progress = totalBytes > 0 ? (receivedBytes * 100) / totalBytes : 0;
                        if (progress > 99) progress = 99;
                        drawOtaTransferProgress(progress, "FETCHING CATALOG DB", receivedBytes, totalBytes, streamStart, CP_CYAN);
                        lastDraw = lastByte;
                    }
                }
                if (remaining == 0 || millis() - lastByte > 12000) break;
                delay(1);
            }
            drawOtaTransferProgress(100, "CATALOG DB READY", receivedBytes, totalBytes, streamStart, CP_GREEN);
            
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                otaCatalog.clear();
                otaCatalogTotal = doc["total"].as<int>();
                // Target the "items" array nested inside the root metadata object!
                JsonArray array = doc["items"].as<JsonArray>();
                int skipped = 0;
                int added = 0;
                for (JsonVariant v : array) {
                    if (skipped++ < pageOffset) continue;
                    if (added >= OTA_CATALOG_PAGE_SIZE) break;

                    FirmwareCatalogItem item;
                    item.fid = v["fid"].as<String>();
                    item.name = v["name"].as<String>();
                    item.author = "";
                    item.version = "";
                    item.binUrl = "";
                    item.desc = "";
                    
                    if (item.name.length() > 34) {
                        item.name = item.name.substring(0, 31) + "...";
                    }
                    
                    otaCatalog.push_back(item);
                    added++;
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
    resetOtaDescriptionTransferStats();
    drawOtaDetailProgress(0, 0, 0, 0, 0, CP_CYAN);
    
    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    String url = "https://api.launcherhub.net/firmwares?fid=" + fid;
    
    if (http.begin(secureClient, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            WiFiClient* stream = http.getStreamPtr();
            int remaining = http.getSize();
            size_t totalBytes = (remaining > 0) ? (size_t)remaining : 0;
            size_t receivedBytes = 0;
            unsigned long streamStart = millis();
            unsigned long lastDraw = 0;
            unsigned long lastByte = millis();
            uint8_t buff[256] = { 0 };
            String payload = "";
            payload.reserve((totalBytes > 0 && totalBytes < 8000) ? totalBytes + 1 : 2048);
            drawOtaDetailProgress(0, 0, 0, totalBytes, streamStart, CP_CYAN);

            while (http.connected() && (remaining > 0 || remaining == -1)) {
                size_t available = stream->available();
                if (available) {
                    int toRead = available > sizeof(buff) ? sizeof(buff) : available;
                    int bytesRead = stream->readBytes(buff, toRead);
                    for (int i = 0; i < bytesRead; i++) payload += (char)buff[i];
                    if (remaining > 0) remaining -= bytesRead;
                    receivedBytes += bytesRead;
                    lastByte = millis();

                    if (lastByte - lastDraw > 150) {
                        int progress = totalBytes > 0 ? (receivedBytes * 100) / totalBytes : 0;
                        if (progress > 99) progress = 99;
                        drawOtaDetailProgress(progress, 0, receivedBytes, totalBytes, streamStart, CP_CYAN);
                        lastDraw = lastByte;
                    }
                }
                if (remaining == 0 || millis() - lastByte > 12000) break;
                delay(1);
            }
            drawOtaDetailProgress(100, 0, receivedBytes, totalBytes, streamStart, CP_CYAN);

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                otaVersions.clear();
                otaDetailName = doc["name"].as<String>();
                otaDetailAuthor = doc["author"].as<String>();
                otaDetailStars = doc["star"].as<int>();
                
                String detailDesc = doc["description"].as<String>();
                if (detailDesc == "null" || detailDesc == "") {
                     detailDesc = doc["desc"].as<String>();
                }
                if (detailDesc == "null" || detailDesc == "") {
                    for (FirmwareCatalogItem &item : otaCatalog) {
                        if (item.fid == fid && item.desc != "") {
                            detailDesc = item.desc;
                            break;
                        }
                    }
                }
                
                JsonArray versions = doc["versions"].as<JsonArray>();
                for (JsonVariant v : versions) {
                    FirmwareVersionItem ver;
                    ver.version = v["version"].as<String>();
                    ver.file = v["file"].as<String>();
                    ver.publishedAt = v["published_at"].as<String>();
                    ver.sizeBytes = v["Fs"].as<size_t>();
                    if (ver.sizeBytes == 0) ver.sizeBytes = v["as"].as<size_t>();
                    otaVersions.push_back(ver);
                }
                http.end();

                if (detailDesc == "null" || detailDesc == "") {
                    detailDesc = fetchOtaFirmwareDescription(fid, 100);
                    if (detailDesc != "") {
                        for (FirmwareCatalogItem &item : otaCatalog) {
                            if (item.fid == fid) {
                                item.desc = detailDesc;
                                break;
                            }
                        }
                    }
                }
                if (otaDescTransferDataValid) {
                    drawOtaDetailProgressWithData(100, 100, otaDescTransferDoneBytes, otaDescTransferTotalBytes, otaDescTransferStartedAt, CP_GREEN, "DATA", otaDescTransferDoneBytes, otaDescTransferTotalBytes);
                } else {
                    drawOtaDetailProgress(100, 100, receivedBytes, totalBytes, streamStart, CP_GREEN);
                }
                if (otaDescTransferDataValid && otaDescTransferTotalBytes > 0) delay(700);
                otaDescScrollOffset = 0;
                otaDetailDesc = cleanOtaDescription(detailDesc);
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
    
    String authorText = "Author: " + otaDetailAuthor;
    if (authorText.length() > 24) authorText = authorText.substring(0, 21) + "...";
    int maxDescScroll = getOtaDescMaxScroll();
    if (otaDescScrollOffset > maxDescScroll) otaDescScrollOffset = maxDescScroll;

    canvas.setTextColor(WHITE);
    canvas.setCursor(12, 24);
    canvas.print(authorText);

    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(170, 24);
    canvas.print(String(otaDescScrollOffset + 1) + "/" + String(maxDescScroll + 1));
    
    drawOtaWrappedText(otaDetailDesc, 12, 36, 216, OTA_DETAIL_DESC_LINES, otaDescScrollOffset, CP_YELLOW);
    
    canvas.drawLine(10, 78, 230, 78, CP_CYAN);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("SELECT VERSION  </>", 120, 82);

    if (!otaVersions.empty()) {
        if (otaVersionFocus < 0 || otaVersionFocus >= (int)otaVersions.size()) otaVersionFocus = 0;
        String verText = otaVersions[otaVersionFocus].version;
        if (verText.length() > 22) verText = verText.substring(0, 19) + "...";
        if (otaVersions.size() > 1) verText = "< " + verText + " >";

        canvas.fillRect(22, 95, 196, 19, canvas.color565(30, 30, 30));
        drawChippedButton(22, 95, 196, 19, CP_YELLOW);
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString(verText, 120, 100);

        canvas.setTextColor(CP_YELLOW);
        String sizeText = "SIZE " + formatOtaBytesOrUnknown(otaVersions[otaVersionFocus].sizeBytes);
        String dateText = otaVersions[otaVersionFocus].publishedAt;
        if (dateText.length() > 12) dateText = dateText.substring(0, 10);
        canvas.setCursor(14, 116);
        canvas.print(sizeText);
        canvas.setCursor(138, 116);
        canvas.print(dateText);
    } else {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NO VERSIONS FOUND", 120, 100);
    }
    
    canvas.drawLine(10, 123, 230, 123, CP_CYAN);
    
    pushCanvas();
}

void performOtaUpdate(String binUrl) {
    performOtaUpdate(binUrl, 0);
}

void performOtaUpdate(String binUrl, size_t expectedBytes) {
    drawOtaTransferProgress(0, "CONNECTING SECURE ENDPOINT", 0, 0, 0, CP_CYAN);
    
    if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
    HTTPClient http;
    
    if (http.begin(secureClient, binUrl)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            size_t transferTotal = (contentLength > 0) ? (size_t)contentLength : expectedBytes;
            size_t updateSize = (transferTotal > 0) ? transferTotal : UPDATE_SIZE_UNKNOWN;
            
            bool canBegin = Update.begin(updateSize);
            if (canBegin) {
                WiFiClient* stream = http.getStreamPtr();
                size_t written = 0;
                int remaining = (contentLength > 0) ? contentLength : -1;
                uint8_t buff[2048] = { 0 };
                unsigned long lastUpdate = 0;
                unsigned long downloadStart = millis();
                drawOtaTransferProgress(0, "FLASHING OTA STREAM", 0, transferTotal, downloadStart, CP_CYAN);
                
                while (http.connected() && (remaining > 0 || remaining == -1)) {
                    size_t size = stream->available();
                    if (size) {
                        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                        Update.write(buff, c);
                        written += c;
                        if (remaining > 0) remaining -= c;
                        
                        unsigned long currentMillis = millis();
                        if (currentMillis - lastUpdate > 250) {
                            int progress = calculateOtaStreamProgress(written, transferTotal);
                            drawOtaTransferProgress(progress, "FLASHING OTA STREAM", written, transferTotal, downloadStart, CP_CYAN);
                            lastUpdate = currentMillis;
                        }
                    }
                    if (remaining == 0 || (remaining == -1 && transferTotal > 0 && written >= transferTotal)) break;
                    delay(1);
                }
                size_t finalTotal = (transferTotal > 0) ? transferTotal : written;
                drawOtaTransferProgress(100, "OTA STREAM COMPLETE", written, finalTotal, downloadStart, CP_GREEN);
                
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
