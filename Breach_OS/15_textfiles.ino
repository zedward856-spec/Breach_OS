// NETWORK NODE / TEXTFILES: browse textfiles.com and save selected files to SD.

static constexpr const char* TEXTFILES_BASE_URL = "http://textfiles.com";
static constexpr const char* TEXTFILES_START_PATH = "/directory.html";
static constexpr const char* TEXTFILES_OS_DIR = "/Breach_OS";
static constexpr const char* TEXTFILES_SAVE_DIR = "/Breach_OS/textfiles";
static constexpr size_t TEXTFILES_HTML_MAX_BYTES = 98304;
static constexpr size_t TEXTFILES_DOWNLOAD_MAX_BYTES = 524288;
static constexpr int TEXTFILES_VISIBLE_ROWS = 7;
static constexpr int TEXTFILES_VIEW_COLS = 35;
static constexpr int TEXTFILES_VIEW_MAX_LINES = 160;
static constexpr int TEXTFILES_MAX_ENTRIES = 80;

struct TextfilesEntry {
    String name;
    String href;
    String desc;
    bool dir;
    size_t sizeBytes;
};

enum TextfilesMode {
    TEXTFILES_ACTIONS,
    TEXTFILES_BROWSER,
    TEXTFILES_SAVED,
    TEXTFILES_VIEWER
};

static TextfilesMode textfilesMode = TEXTFILES_ACTIONS;
static TextfilesMode textfilesReturnMode = TEXTFILES_ACTIONS;
static int textfilesFocus = 0;
static int textfilesScroll = 0;
static int textfilesViewScroll = 0;
static bool textfilesLoaded = false;
static String textfilesRemotePath = TEXTFILES_START_PATH;
static String textfilesStatus = "READY";
static String textfilesTitle = "TEXTFILES";
static std::vector<TextfilesEntry> textfilesEntries;
static std::vector<String> textfilesViewLines;

static String textfilesShort(String s, int maxLen) {
    if ((int)s.length() <= maxLen) return s;
    if (maxLen <= 1) return s.substring(0, maxLen);
    return s.substring(0, maxLen - 1) + "~";
}

static String textfilesCleanAnchor(String s) {
    s.replace("\r", "");
    s.replace("\n", " ");
    s.replace("&amp;", "&");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&quot;", "\"");
    s.replace("&#39;", "'");
    int tag = s.indexOf('<');
    while (tag >= 0) {
        int end = s.indexOf('>', tag);
        if (end < 0) break;
        s.remove(tag, end - tag + 1);
        tag = s.indexOf('<');
    }
    s.trim();
    return s;
}

static bool textfilesExternalHref(String href) {
    String lower = href;
    lower.toLowerCase();
    return lower.startsWith("http://") || lower.startsWith("https://") ||
           lower.startsWith("mailto:") || lower.startsWith("ftp://") ||
           lower.startsWith("javascript:") || lower.startsWith("#");
}

static bool textfilesHtmlInfoPage(String href) {
    String lower = href;
    lower.toLowerCase();
    return lower.endsWith(".html") || lower.endsWith(".htm");
}

static String textfilesBaseDir(String path) {
    if (path == TEXTFILES_START_PATH) return "/";
    int slash = path.lastIndexOf('/');
    if (slash < 0) return "/";
    return path.substring(0, slash + 1);
}

static String textfilesParentDir(String path) {
    if (path == TEXTFILES_START_PATH || path == "/") return TEXTFILES_START_PATH;
    if (path.endsWith("/")) path.remove(path.length() - 1);
    int slash = path.lastIndexOf('/');
    if (slash <= 0) return TEXTFILES_START_PATH;
    return path.substring(0, slash + 1);
}

static String textfilesResolvePath(String base, String href, bool dir) {
    int hash = href.indexOf('#');
    if (hash >= 0) href.remove(hash);
    int query = href.indexOf('?');
    if (query >= 0) href.remove(query);
    href.trim();
    String path;
    if (href.startsWith("/")) {
        path = href;
    } else {
        path = textfilesBaseDir(base) + href;
    }
    if (dir && !path.endsWith("/")) path += "/";
    return path;
}

static String textfilesUrlForPath(String path) {
    if (!path.startsWith("/")) path = "/" + path;
    return String(TEXTFILES_BASE_URL) + path;
}

static bool textfilesMountSd() {
    SPI.begin(40, 39, 14, 12);
    if (!SD.begin(12, SPI, 20000000) || SD.cardType() == CARD_NONE) {
        textfilesStatus = "SD MOUNT FAIL";
        return false;
    }
    if (!SD.exists(TEXTFILES_OS_DIR) && !SD.mkdir(TEXTFILES_OS_DIR)) {
        textfilesStatus = "SD DIR FAIL";
        return false;
    }
    if (!SD.exists(TEXTFILES_SAVE_DIR) && !SD.mkdir(TEXTFILES_SAVE_DIR)) {
        textfilesStatus = "TEXT DIR FAIL";
        return false;
    }
    return true;
}

static bool textfilesLooksLikeFileRow(String row, size_t &sizeOut) {
    String upper = row;
    upper.toUpperCase();
    int br = upper.indexOf("<BR><TD");
    if (br < 0) br = upper.indexOf("<BR> <TD");
    if (br < 0) br = upper.indexOf("<BR\n<TD");
    if (br < 0) return false;

    int i = br - 1;
    while (i >= 0 && (row[i] == ' ' || row[i] == '\r' || row[i] == '\n' || row[i] == '\t')) i--;
    int end = i;
    while (i >= 0 && row[i] >= '0' && row[i] <= '9') i--;
    if (end <= i) return false;

    sizeOut = (size_t)row.substring(i + 1, end + 1).toInt();
    return sizeOut > 0;
}

static void textfilesAddFallbackStartEntries() {
    const char* names[] = {"100", "ADVENTURE", "ANARCHY", "APPLE", "ART", "BBS", "COMPUTERS", "HACKING", "HAMRADIO", "HUMOR", "MAGAZINES", "PROGRAMMING"};
    const char* hrefs[] = {"/100/", "/adventure/", "/anarchy/", "/apple/", "/art/", "/bbs/", "/computers/", "/hacking/", "/hamradio/", "/humor/", "/magazines/", "/programming/"};
    for (int i = 0; i < 12 && textfilesEntries.size() < TEXTFILES_MAX_ENTRIES; i++) {
        TextfilesEntry entry;
        entry.name = names[i];
        entry.href = hrefs[i];
        entry.desc = "DIR";
        entry.dir = true;
        entry.sizeBytes = 0;
        textfilesEntries.push_back(entry);
    }
}

static String textfilesDescriptionFromRow(String row) {
    String upper = row;
    upper.toUpperCase();
    int descStart = upper.lastIndexOf("<TD>");
    if (descStart < 0) descStart = upper.lastIndexOf("<TD ");
    if (descStart < 0) return "";
    int gt = row.indexOf('>', descStart);
    if (gt < 0) return "";
    int descEnd = upper.indexOf("<TR", gt);
    if (descEnd < 0) descEnd = row.length();
    String desc = row.substring(gt + 1, descEnd);
    return textfilesShort(textfilesCleanAnchor(desc), 30);
}

static void textfilesParseDirectory(String html, String path) {
    textfilesEntries.clear();

    if (path != TEXTFILES_START_PATH) {
        TextfilesEntry up;
        up.name = "..";
        up.href = textfilesParentDir(path);
        up.desc = "parent dir";
        up.dir = true;
        up.sizeBytes = 0;
        textfilesEntries.push_back(up);
    }

    String upper = html;
    upper.toUpperCase();
    int pos = 0;
    while (textfilesEntries.size() < TEXTFILES_MAX_ENTRIES) {
        int a = upper.indexOf("<A", pos);
        if (a < 0) break;
        int hrefKey = upper.indexOf("HREF=", a);
        int tagEnd = upper.indexOf('>', a);
        if (hrefKey < 0 || tagEnd < 0 || hrefKey > tagEnd) {
            pos = a + 2;
            continue;
        }

        int q1 = hrefKey + 5;
        while (q1 < html.length() && html[q1] == ' ') q1++;
        char quote = html[q1];
        if (quote != '"' && quote != '\'') {
            pos = tagEnd + 1;
            continue;
        }
        int q2 = html.indexOf(quote, q1 + 1);
        int close = upper.indexOf("</A>", tagEnd);
        if (q2 < 0 || close < 0) {
            pos = tagEnd + 1;
            continue;
        }

        String href = html.substring(q1 + 1, q2);
        String label = textfilesCleanAnchor(html.substring(tagEnd + 1, close));
        if (href == "" || label == "" || textfilesExternalHref(href)) {
            pos = close + 4;
            continue;
        }
        if (path == TEXTFILES_START_PATH && textfilesHtmlInfoPage(href)) {
            pos = close + 4;
            continue;
        }

        int rowEnd = upper.indexOf("<TR", close + 4);
        if (rowEnd < 0) rowEnd = close + 220;
        if (rowEnd > html.length()) rowEnd = html.length();
        String row = html.substring(close + 4, rowEnd);

        size_t sizeBytes = 0;
        bool hasFileSize = textfilesLooksLikeFileRow(row, sizeBytes);
        bool hasDot = href.indexOf('.') >= 0;
        bool dir = href.endsWith("/") || (!hasFileSize && !hasDot);

        TextfilesEntry entry;
        entry.name = textfilesShort(label, 22);
        entry.href = textfilesResolvePath(path, href, dir);
        entry.desc = dir ? "DIR" : textfilesDescriptionFromRow(row);
        entry.dir = dir;
        entry.sizeBytes = sizeBytes;
        textfilesEntries.push_back(entry);
        pos = close + 4;
    }

    if (textfilesEntries.empty() && path == TEXTFILES_START_PATH) {
        textfilesAddFallbackStartEntries();
    }
}

static bool textfilesFetchDirectory(String path) {
    if (WiFi.status() != WL_CONNECTED) {
        textfilesStatus = "WIFI OFFLINE";
        return false;
    }

    textfilesStatus = "FETCHING DIR";
    drawTextfilesScreen();

    HTTPClient http;
    http.setTimeout(12000);
    http.setReuse(false);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("Breach_OS Cardputer textfiles/1.0");
    if (!http.begin(textfilesUrlForPath(path))) {
        textfilesStatus = "HTTP INIT FAIL";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        textfilesStatus = "HTTP " + String(code);
        http.end();
        return false;
    }

    int len = http.getSize();
    String html;
    html.reserve((len > 0 && len < 16384) ? len : 16384);
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[256];
    size_t target = (len > 0 && (size_t)len < TEXTFILES_HTML_MAX_BYTES) ? (size_t)len : TEXTFILES_HTML_MAX_BYTES;
    unsigned long lastData = millis();
    while (html.length() < target) {
        size_t avail = stream->available();
        if (avail) {
            if (avail > sizeof(buf)) avail = sizeof(buf);
            size_t remaining = target - html.length();
            if (avail > remaining) avail = remaining;
            int n = stream->readBytes((char*)buf, avail);
            if (n > 0) {
                for (int i = 0; i < n; i++) html += (char)buf[i];
                lastData = millis();
            }
        } else if (!http.connected() && (len < 0 || millis() - lastData > 1500)) {
            break;
        } else if (millis() - lastData > 2500) {
            break;
        } else {
            delay(1);
        }
    }
    http.end();

    if (html.length() >= TEXTFILES_HTML_MAX_BYTES) {
        textfilesStatus = "DIR TOO LARGE";
        return false;
    }

    textfilesParseDirectory(html, path);
    textfilesRemotePath = path;
    textfilesFocus = 0;
    textfilesScroll = 0;
    textfilesLoaded = true;
    textfilesStatus = textfilesEntries.empty() ? "NO LINKS" : ("LINKS " + String(textfilesEntries.size()));
    return !textfilesEntries.empty();
}

static String textfilesSafeFileName(String remotePath) {
    if (remotePath.endsWith("/")) remotePath.remove(remotePath.length() - 1);
    if (remotePath.startsWith("/")) remotePath.remove(0, 1);
    if (remotePath == "") remotePath = "textfile";
    String out;
    for (int i = 0; i < remotePath.length() && out.length() < 54; i++) {
        char c = remotePath[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        out += ok ? c : '_';
    }
    if (out.indexOf('.') < 0) out += ".txt";
    return out;
}

static String textfilesUniqueSavePath(String remotePath) {
    String safe = textfilesSafeFileName(remotePath);
    String base = safe;
    String ext = "";
    int dot = safe.lastIndexOf('.');
    if (dot > 0) {
        base = safe.substring(0, dot);
        ext = safe.substring(dot);
    }
    String path = String(TEXTFILES_SAVE_DIR) + "/" + safe;
    for (int i = 2; SD.exists(path.c_str()) && i < 100; i++) {
        path = String(TEXTFILES_SAVE_DIR) + "/" + base + "_" + String(i) + ext;
    }
    return path;
}

static void textfilesAppendWrappedLine(String line) {
    line.replace("\r", "");
    if (line.length() == 0) {
        textfilesViewLines.push_back("");
        return;
    }
    while (line.length() > 0 && textfilesViewLines.size() < TEXTFILES_VIEW_MAX_LINES) {
        String chunk = line.substring(0, min((int)line.length(), TEXTFILES_VIEW_COLS));
        textfilesViewLines.push_back(chunk);
        if ((int)line.length() <= TEXTFILES_VIEW_COLS) break;
        line.remove(0, TEXTFILES_VIEW_COLS);
    }
}

static bool textfilesOpenLocalFile(String path, String title, int returnMode) {
    if (!textfilesMountSd()) return false;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f || f.isDirectory()) {
        textfilesStatus = "OPEN FAIL";
        if (f) f.close();
        return false;
    }

    textfilesViewLines.clear();
    textfilesTitle = textfilesShort(title, 20);
    while (f.available() && textfilesViewLines.size() < TEXTFILES_VIEW_MAX_LINES) {
        String line = f.readStringUntil('\n');
        textfilesAppendWrappedLine(line);
    }
    if (f.available() && textfilesViewLines.size() < TEXTFILES_VIEW_MAX_LINES) {
        textfilesViewLines.push_back("...TRUNCATED...");
    }
    f.close();

    textfilesViewScroll = 0;
    textfilesReturnMode = (TextfilesMode)returnMode;
    textfilesMode = TEXTFILES_VIEWER;
    textfilesStatus = String(textfilesViewLines.size()) + " LINES";
    return true;
}

static bool textfilesDownloadEntry(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= (int)textfilesEntries.size()) return false;
    TextfilesEntry entry = textfilesEntries[entryIndex];
    if (entry.dir) return false;
    if (WiFi.status() != WL_CONNECTED) {
        textfilesStatus = "WIFI OFFLINE";
        return false;
    }
    if (entry.sizeBytes > TEXTFILES_DOWNLOAD_MAX_BYTES) {
        textfilesStatus = "FILE TOO BIG";
        return false;
    }
    if (!textfilesMountSd()) return false;

    String url = textfilesUrlForPath(entry.href);
    String savePath = textfilesUniqueSavePath(entry.href);
    HTTPClient http;
    http.setTimeout(12000);
    http.setReuse(false);
    http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("Breach_OS Cardputer textfiles/1.0");
    if (!http.begin(url)) {
        textfilesStatus = "HTTP INIT FAIL";
        return false;
    }

    textfilesStatus = "DOWNLOADING";
    drawTextfilesScreen();

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        textfilesStatus = "HTTP " + String(code);
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len > 0 && (size_t)len > TEXTFILES_DOWNLOAD_MAX_BYTES) {
        textfilesStatus = "FILE TOO BIG";
        http.end();
        return false;
    }

    SD.remove(savePath.c_str());
    File out = SD.open(savePath.c_str(), FILE_WRITE);
    if (!out) {
        textfilesStatus = "WRITE FAIL";
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[256];
    size_t total = 0;
    size_t target = (len > 0 && (size_t)len < TEXTFILES_DOWNLOAD_MAX_BYTES) ? (size_t)len : TEXTFILES_DOWNLOAD_MAX_BYTES;
    unsigned long lastDraw = 0;
    unsigned long lastData = millis();
    while (total < target) {
        size_t avail = stream->available();
        if (avail) {
            if (avail > sizeof(buf)) avail = sizeof(buf);
            size_t remaining = target - total;
            if (avail > remaining) avail = remaining;
            int n = stream->readBytes((char*)buf, avail);
            if (n > 0) {
                out.write(buf, n);
                total += n;
                lastData = millis();
                if (millis() - lastDraw > 250) {
                    textfilesStatus = String(total / 1024) + "KB";
                    drawTextfilesScreen();
                    lastDraw = millis();
                }
            }
        } else if (!http.connected() && (len < 0 || millis() - lastData > 1500)) {
            break;
        } else if (millis() - lastData > 2500) {
            break;
        } else {
            delay(1);
        }
    }
    out.close();
    http.end();

    if (total >= TEXTFILES_DOWNLOAD_MAX_BYTES) {
        SD.remove(savePath.c_str());
        textfilesStatus = "FILE TOO BIG";
        return false;
    }
    textfilesStatus = "SAVED " + String(total / 1024) + "KB";
    return textfilesOpenLocalFile(savePath, entry.name, TEXTFILES_BROWSER);
}

static void textfilesLoadSavedFiles() {
    textfilesEntries.clear();
    textfilesFocus = 0;
    textfilesScroll = 0;
    if (!textfilesMountSd()) return;

    File dir = SD.open(TEXTFILES_SAVE_DIR);
    if (!dir || !dir.isDirectory()) {
        textfilesStatus = "NO SAVE DIR";
        if (dir) dir.close();
        return;
    }

    File f = dir.openNextFile();
    while (f && textfilesEntries.size() < TEXTFILES_MAX_ENTRIES) {
        if (!f.isDirectory()) {
            TextfilesEntry entry;
            entry.name = textfilesShort(String(f.name()).substring(String(f.name()).lastIndexOf('/') + 1), 22);
            entry.href = String(f.name());
            entry.desc = String((size_t)f.size() / 1024) + "KB";
            entry.dir = false;
            entry.sizeBytes = f.size();
            textfilesEntries.push_back(entry);
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    textfilesStatus = textfilesEntries.empty() ? "NO SAVED FILES" : (String(textfilesEntries.size()) + " SAVED");
}

void enterTextfilesMode() {
    textfilesMode = TEXTFILES_ACTIONS;
    textfilesReturnMode = TEXTFILES_ACTIONS;
    textfilesFocus = 0;
    textfilesScroll = 0;
    textfilesViewScroll = 0;
    textfilesRemotePath = TEXTFILES_START_PATH;
    textfilesLoaded = false;
    textfilesTitle = "TEXTFILES";
    textfilesStatus = (WiFi.status() == WL_CONNECTED) ? "READY" : "WIFI OFFLINE";
    appState = STATE_TEXTFILES;
    drawTextfilesScreen();
}

static void textfilesDrawFrame(String title) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(title, 120, 10);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);
    canvas.setCursor(12, 27);
    canvas.setTextColor((WiFi.status() == WL_CONNECTED) ? CP_GREEN : CP_RED);
    canvas.print((WiFi.status() == WL_CONNECTED) ? "LINK:ONLINE" : "LINK:OFF");
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(105, 27);
    canvas.print(textfilesShort(textfilesStatus, 20));
}

static void textfilesDrawList() {
    if (textfilesFocus < textfilesScroll) textfilesScroll = textfilesFocus;
    if (textfilesFocus >= textfilesScroll + TEXTFILES_VISIBLE_ROWS) textfilesScroll = textfilesFocus - TEXTFILES_VISIBLE_ROWS + 1;
    if (textfilesScroll < 0) textfilesScroll = 0;

    if (textfilesEntries.empty()) {
        canvas.setTextSize(1);
        canvas.setTextColor(CP_DIM);
        canvas.setCursor(15, 58);
        canvas.print("NO ENTRIES");
        canvas.setCursor(15, 72);
        canvas.print(textfilesShort(textfilesStatus, 30));
        return;
    }

    int y = 43;
    for (int row = 0; row < TEXTFILES_VISIBLE_ROWS; row++) {
        int idx = textfilesScroll + row;
        if (idx >= (int)textfilesEntries.size()) break;
        const TextfilesEntry &entry = textfilesEntries[idx];
        bool sel = idx == textfilesFocus;
        uint16_t color = sel ? CP_YELLOW : (entry.dir ? CP_CYAN : WHITE);
        if (sel) drawChippedButton(10, y - 2, 220, 14, color);
        canvas.setTextSize(1);
        canvas.setTextColor(color);
        canvas.setCursor(15, y);
        canvas.print(String(entry.dir ? "> " : "  ") + textfilesShort(entry.name, 18));
        if (entry.desc != "") {
            canvas.setTextColor(sel ? CP_YELLOW : CP_DIM);
            canvas.setCursor(150, y);
            canvas.print(textfilesShort(entry.desc, 13));
        }
        y += 12;
    }
}

void drawTextfilesScreen() {
    if (textfilesMode == TEXTFILES_ACTIONS) {
        textfilesDrawFrame("--- TEXTFILES.COM ---");
        const char* actions[] = {"BROWSE DIR", "DOWNLOAD FILE", "READ SAVED", "BACK"};
        for (int i = 0; i < 4; i++) {
            uint16_t color = (i == textfilesFocus) ? CP_YELLOW : WHITE;
            int y = 45 + i * 18;
            drawChippedButton(20, y - 3, 200, 15, color);
            canvas.setTextColor(color);
            canvas.setCursor(28, y);
            canvas.print(actions[i]);
        }
    } else if (textfilesMode == TEXTFILES_BROWSER) {
        String title = "--- TEXTFILES DIR ---";
        textfilesDrawFrame(title);
        canvas.setTextColor(CP_DIM);
        canvas.setCursor(12, 37);
        canvas.print(textfilesShort(textfilesRemotePath, 35));
        textfilesDrawList();
    } else if (textfilesMode == TEXTFILES_SAVED) {
        textfilesDrawFrame("--- SAVED TEXT ---");
        textfilesDrawList();
    } else if (textfilesMode == TEXTFILES_VIEWER) {
        String viewerHeader = String("--- ") + textfilesShort(textfilesTitle, 18) + " ---";
        textfilesDrawFrame(viewerHeader);
        canvas.setTextColor(WHITE);
        int y = 43;
        for (int i = 0; i < TEXTFILES_VISIBLE_ROWS && textfilesViewScroll + i < (int)textfilesViewLines.size(); i++) {
            canvas.setCursor(12, y);
            canvas.print(textfilesViewLines[textfilesViewScroll + i]);
            y += 12;
        }
        canvas.setTextColor(CP_DIM);
        canvas.setCursor(12, 118);
        canvas.print("LINES " + String(textfilesViewScroll + 1) + "/" + String(max(1, (int)textfilesViewLines.size())));
    }
    pushCanvas();
}

void handleTextfilesInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasBack = status.del;
    for (char c : status.word) {
        if (c == ';' || c == ',') hasUp = true;
        if (c == '.' || c == '/') hasDown = true;
        if (c == '`') hasBack = true;
    }

    if (textfilesMode == TEXTFILES_VIEWER) {
        if (hasBack) {
            playSound(sound_select, sound_select_size);
            textfilesMode = textfilesReturnMode;
            return;
        }
        if (hasUp && textfilesViewScroll > 0) {
            playSound(sound_hover, sound_hover_size);
            textfilesViewScroll--;
        }
        if (hasDown && textfilesViewScroll + TEXTFILES_VISIBLE_ROWS < (int)textfilesViewLines.size()) {
            playSound(sound_hover, sound_hover_size);
            textfilesViewScroll++;
        }
        return;
    }

    int count = (textfilesMode == TEXTFILES_ACTIONS) ? 4 : (int)textfilesEntries.size();
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (textfilesMode == TEXTFILES_ACTIONS) {
            appState = STATE_MAIN_MENU;
            mainMenuFocus = 3;
            currentMenuScroll = mainMenuFocus;
            targetMenuScroll = mainMenuFocus;
            drawMainMenu();
        } else {
            textfilesMode = TEXTFILES_ACTIONS;
            textfilesFocus = 0;
            textfilesScroll = 0;
        }
        return;
    }

    if (count > 0 && hasUp) {
        playSound(sound_hover, sound_hover_size);
        textfilesFocus = (textfilesFocus - 1 + count) % count;
    }
    if (count > 0 && hasDown) {
        playSound(sound_hover, sound_hover_size);
        textfilesFocus = (textfilesFocus + 1) % count;
    }

    if (!status.enter) return;
    playSound(sound_select, sound_select_size);

    if (textfilesMode == TEXTFILES_ACTIONS) {
        if (textfilesFocus == 0 || textfilesFocus == 1) {
            textfilesMode = TEXTFILES_BROWSER;
            textfilesFocus = 0;
            textfilesScroll = 0;
            textfilesFetchDirectory(TEXTFILES_START_PATH);
        } else if (textfilesFocus == 2) {
            textfilesMode = TEXTFILES_SAVED;
            textfilesLoadSavedFiles();
        } else {
            appState = STATE_MAIN_MENU;
            mainMenuFocus = 3;
            currentMenuScroll = mainMenuFocus;
            targetMenuScroll = mainMenuFocus;
            drawMainMenu();
        }
        return;
    }

    if (textfilesMode == TEXTFILES_BROWSER && textfilesFocus >= 0 && textfilesFocus < (int)textfilesEntries.size()) {
        TextfilesEntry entry = textfilesEntries[textfilesFocus];
        if (entry.dir) textfilesFetchDirectory(entry.href);
        else textfilesDownloadEntry(textfilesFocus);
        return;
    }

    if (textfilesMode == TEXTFILES_SAVED && textfilesFocus >= 0 && textfilesFocus < (int)textfilesEntries.size()) {
        TextfilesEntry entry = textfilesEntries[textfilesFocus];
        textfilesOpenLocalFile(entry.href, entry.name, TEXTFILES_SAVED);
        return;
    }
}

bool updateTextfilesUi() {
    return false;
}
