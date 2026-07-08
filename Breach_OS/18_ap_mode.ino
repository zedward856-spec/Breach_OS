// NETWORK NODE / AP MODE: host one normal Cardputer Wi-Fi access point.
// This is intentionally a single SoftAP for lab/field connectivity, not beacon spam.

static constexpr int AP_MODE_ROW_COUNT = 5;
static constexpr int AP_MODE_FOCUS_COUNT = AP_MODE_ROW_COUNT;
static constexpr int AP_MODE_VISIBLE_ROWS = 4;
static constexpr int AP_MODE_MIN_CHANNEL = 1;
static constexpr int AP_MODE_MAX_CHANNEL = 13;
static constexpr int AP_MODE_MAX_SSID_LEN = 32;
static constexpr int AP_MODE_MAX_PASS_LEN = 63;

static constexpr int AP_MODE_SOURCE_COUNT = 3;
static constexpr int AP_MODE_REMOTE_KEY_QUEUE_MAX = 16;
static constexpr int AP_MODE_MIRROR_W = 240;
static constexpr int AP_MODE_MIRROR_H = 135;
static constexpr size_t AP_MODE_MIRROR_FRAME_BYTES = AP_MODE_MIRROR_W * AP_MODE_MIRROR_H * 2;
static constexpr int AP_MODE_WEB_CLIENT_SLOTS = 4;
static constexpr unsigned long AP_MODE_WEB_CLIENT_ACTIVE_MS = 15000UL;

static int apModeListScroll = 0;
static int apModeSourceFocus = 0;
static float currentApModeSourceScroll = 0.0;
static float targetApModeSourceScroll = 0.0;
static bool apModePromptActive = false;
static bool apModeLanWebActive = false;
static bool apModeUseLanSource = false;
static WebServer apModeWebServer(80);
static File apModeWebUploadFile;
static bool apModeWebServerStarted = false;
static bool apModeWebRoutesConfigured = false;
static String apModeWebUploadDir = "/";
static String apModeWebLastStatus = "";
static int apModeWebSortField = 0;
static bool apModeWebSortDesc = false;
static String apModeRemoteWord = "";
static bool apModeRemoteEnter = false;
static bool apModeRemoteDel = false;
static unsigned long apModeRemoteLastMs = 0;
static String apModeRemoteLastLabel = "NONE";
static String apModeWebClientIp[AP_MODE_WEB_CLIENT_SLOTS];
static unsigned long apModeWebClientSeen[AP_MODE_WEB_CLIENT_SLOTS] = {0, 0, 0, 0};

struct ApModeWebEntry {
    String name;
    String path;
    String ext;
    bool isDir;
    size_t size;
};

static String apModeShort(String s, int maxLen) {
    if ((int)s.length() <= maxLen) return s;
    if (maxLen <= 1) return s.substring(0, maxLen);
    return s.substring(0, maxLen - 1) + "~";
}

static void apModeClampChannel() {
    if (apModeChannel < AP_MODE_MIN_CHANNEL) apModeChannel = AP_MODE_MAX_CHANNEL;
    if (apModeChannel > AP_MODE_MAX_CHANNEL) apModeChannel = AP_MODE_MIN_CHANNEL;
}

static String apModeMaskedPass() {
    if (apModeOpen) return "<OPEN>";
    String out = "";
    int count = apModePass.length();
    if (count == 0) return "<EMPTY>";
    if (count > 12) count = 12;
    for (int i = 0; i < count; i++) out += "*";
    if (apModePass.length() > count) out += "~";
    return out;
}

static bool apModeWebActive() {
    return apModeRunning || apModeLanWebActive;
}

static String apModeWebGb(uint64_t bytes) {
    return String((double)bytes / 1073741824.0, 1);
}

static String apModeWebSdGbPair(uint64_t used, uint64_t total) {
    return apModeWebGb(used) + "/" + apModeWebGb(total) + "GB";
}

static void apModeWebClearClients() {
    for (int i = 0; i < AP_MODE_WEB_CLIENT_SLOTS; i++) {
        apModeWebClientIp[i] = "";
        apModeWebClientSeen[i] = 0;
    }
}

static void apModeWebTouchClient() {
    WiFiClient client = apModeWebServer.client();
    String remoteIp = client.remoteIP().toString();
    if (remoteIp == "" || remoteIp == "0.0.0.0") return;

    unsigned long now = millis();
    int freeSlot = -1;
    int oldestSlot = 0;
    for (int i = 0; i < AP_MODE_WEB_CLIENT_SLOTS; i++) {
        if (apModeWebClientIp[i] == remoteIp) {
            apModeWebClientSeen[i] = now;
            return;
        }
        if (apModeWebClientIp[i] == "" && freeSlot < 0) freeSlot = i;
        if (apModeWebClientSeen[i] < apModeWebClientSeen[oldestSlot]) oldestSlot = i;
    }

    int slot = freeSlot >= 0 ? freeSlot : oldestSlot;
    apModeWebClientIp[slot] = remoteIp;
    apModeWebClientSeen[slot] = now;
}

static int apModeWebRecentClientCount() {
    unsigned long now = millis();
    int count = 0;
    for (int i = 0; i < AP_MODE_WEB_CLIENT_SLOTS; i++) {
        if (apModeWebClientIp[i] == "") continue;
        if (now - apModeWebClientSeen[i] <= AP_MODE_WEB_CLIENT_ACTIVE_MS) {
            count++;
        } else {
            apModeWebClientIp[i] = "";
            apModeWebClientSeen[i] = 0;
        }
    }

    int apClients = apModeRunning ? WiFi.softAPgetStationNum() : 0;
    return count > apClients ? count : apClients;
}

static String apModeRemoteSafeLabel(String key) {
    if (key == "ArrowUp") return "UP";
    if (key == "ArrowDown") return "DOWN";
    if (key == "ArrowLeft") return "LEFT";
    if (key == "ArrowRight") return "RIGHT";
    if (key == "Enter") return "ENTER";
    if (key == "Backspace") return "BACKSPACE";
    if (key == "Delete") return "DELETE";
    if (key == "Escape") return "ESC";
    if (key.length() == 1 && key[0] >= 32 && key[0] <= 126) return "TEXT";
    return "UNKNOWN";
}

static void apModeQueueRemoteChar(char c) {
    if ((int)apModeRemoteWord.length() >= AP_MODE_REMOTE_KEY_QUEUE_MAX) return;
    apModeRemoteWord += c;
    apModeRemoteLastMs = millis();
}

static void apModeQueueRemoteKey(String key) {
    apModeRemoteLastLabel = apModeRemoteSafeLabel(key);
    if (key == "ArrowUp") apModeQueueRemoteChar(';');
    else if (key == "ArrowDown") apModeQueueRemoteChar('.');
    else if (key == "ArrowLeft") apModeQueueRemoteChar(',');
    else if (key == "ArrowRight") apModeQueueRemoteChar('/');
    else if (key == "Enter") { apModeRemoteEnter = true; apModeRemoteLastMs = millis(); }
    else if (key == "Backspace" || key == "Delete") { apModeRemoteDel = true; apModeRemoteLastMs = millis(); }
    else if (key == "Escape") apModeQueueRemoteChar('`');
    else if (key.length() == 1 && key[0] >= 32 && key[0] <= 126) apModeQueueRemoteChar(key[0]);
}

bool consumeApModeRemoteInput(Keyboard_Class::KeysState &status) {
    bool consumed = false;
    for (int i = 0; i < (int)apModeRemoteWord.length(); i++) {
        status.word.push_back(apModeRemoteWord[i]);
        consumed = true;
    }
    apModeRemoteWord = "";
    if (apModeRemoteEnter) {
        status.enter = true;
        apModeRemoteEnter = false;
        consumed = true;
    }
    if (apModeRemoteDel) {
        status.del = true;
        apModeRemoteDel = false;
        consumed = true;
    }
    return consumed;
}

static void apModeClearRemoteInput() {
    apModeRemoteWord = "";
    apModeRemoteEnter = false;
    apModeRemoteDel = false;
}

static String apModeConnectedNetworkLabel() {
    if (WiFi.status() != WL_CONNECTED) return "NO CONNECTED WIFI";
    String label = WiFi.SSID();
    if (label == "") label = "CONNECTED NETWORK";
    return label;
}

static String apModeWebHostLabel() {
    if (apModeLanWebActive) return String("NETWORK ") + apModeConnectedNetworkLabel();
    return String("AP ") + apModeSsid;
}

static void apModeSavePrefs() {
    prefs.putString("ap_ssid", apModeSsid);
    prefs.putString("ap_pass", apModePass);
    prefs.putInt("ap_ch", apModeChannel);
    prefs.putBool("ap_open", apModeOpen);
}

static void apModeResetDefaults() {
    apModeSsid = "Breach_OS_AP";
    apModePass = "breach123";
    apModeChannel = 6;
    apModeOpen = false;
    apModeStatus = "DEFAULTS READY";
    apModeSavePrefs();
}

static String apModeWebHtml(String text) {
    text.replace("&", "&amp;");
    text.replace("<", "&lt;");
    text.replace(">", "&gt;");
    text.replace("\"", "&quot;");
    text.replace("'", "&#39;");
    return text;
}

static String apModeWebJson(String text) {
    text.replace("\\", "\\\\");
    text.replace("\"", "\\\"");
    text.replace("\n", " ");
    text.replace("\r", " ");
    return text;
}

static String apModeWebUrlEncode(String text) {
    const char *hex = "0123456789ABCDEF";
    String out = "";
    for (int i = 0; i < (int)text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

static String apModeWebCleanPath(String raw) {
    raw = WebServer::urlDecode(raw);
    raw.replace("\\", "/");
    if (raw == "") raw = "/";
    if (!raw.startsWith("/")) raw = "/" + raw;

    std::vector<String> parts;
    int start = 0;
    while (start <= (int)raw.length()) {
        int slash = raw.indexOf('/', start);
        if (slash < 0) slash = raw.length();
        String part = raw.substring(start, slash);
        part.trim();
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (part != "" && part != ".") {
            String clean = "";
            for (int i = 0; i < (int)part.length(); i++) {
                char c = part[i];
                if (c >= 32 && c != '/' && c != '\\') clean += c;
            }
            if (clean != "") parts.push_back(clean);
        }
        start = slash + 1;
        if (slash >= (int)raw.length()) break;
    }

    String out = "/";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) out += "/";
        out += parts[i];
    }
    return out;
}

static String apModeWebBaseName(String path) {
    path = apModeWebCleanPath(path);
    while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
    int slash = path.lastIndexOf('/');
    if (slash < 0) return path;
    String name = path.substring(slash + 1);
    return name == "" ? String("sd-root") : name;
}

static String apModeWebCleanName(String name) {
    name = WebServer::urlDecode(name);
    name.trim();
    String out = "";
    for (int i = 0; i < (int)name.length() && out.length() < 64; i++) {
        char c = name[i];
        if (c >= 32 && c != '/' && c != '\\') out += c;
    }
    while (out.startsWith(".")) out.remove(0, 1);
    out.replace("..", "");
    out.trim();
    return out;
}

static String apModeWebJoinPath(String dir, String name) {
    dir = apModeWebCleanPath(dir);
    name = apModeWebCleanName(name);
    if (name == "") return "";
    return dir == "/" ? "/" + name : dir + "/" + name;
}

static String apModeWebParentPath(String path) {
    path = apModeWebCleanPath(path);
    if (path == "/") return "/";
    while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
    int slash = path.lastIndexOf('/');
    if (slash <= 0) return "/";
    return path.substring(0, slash);
}

static String apModeWebSize(size_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    if (bytes < 1024 * 1024) return String(bytes / 1024.0f, 1) + " KB";
    return String(bytes / 1048576.0f, 2) + " MB";
}

static String apModeWebSize64(uint64_t bytes) {
    if (bytes < 1024ULL) return String((unsigned long)bytes) + " B";
    if (bytes < 1048576ULL) return String((double)bytes / 1024.0, 1) + " KB";
    if (bytes < 1073741824ULL) return String((double)bytes / 1048576.0, 1) + " MB";
    return String((double)bytes / 1073741824.0, 2) + " GB";
}

static String apModeWebExtension(String name, bool isDir) {
    if (isDir) return "";
    int dot = name.lastIndexOf('.');
    if (dot <= 0 || dot >= (int)name.length() - 1) return "";
    String ext = name.substring(dot + 1);
    ext.toLowerCase();
    return ext;
}

static bool apModeWebSortText(String aKey, String bKey, String aName, String bName) {
    if (aKey == bKey) return apModeWebSortDesc ? aName > bName : aName < bName;
    return apModeWebSortDesc ? aKey > bKey : aKey < bKey;
}

static String apModeWebSortLink(String path, const char *sort, const char *dir, const char *label, String currentSort, String currentDir) {
    String cls = (currentSort == sort && currentDir == dir) ? "sort active" : "sort";
    return String("<a class='") + cls + "' href='/sd?path=" + apModeWebUrlEncode(path) + "&sort=" + sort + "&dir=" + dir + "'>" + label + "</a>";
}

static bool apModeWebMountSd() {
    SPI.begin(40, 39, 14, 12);
    return SD.begin(12, SPI, 20000000) && SD.cardType() != CARD_NONE;
}

static String apModeMirrorSdInfo() {
    static String cached = "0.0/0.0GB";
    static unsigned long lastRead = 0;
    unsigned long now = millis();
    if (lastRead != 0 && now - lastRead < 3000) return cached;
    lastRead = now;

    if (!apModeWebMountSd()) {
        cached = "0.0/0.0GB";
        return cached;
    }
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    uint64_t card = SD.cardSize();
    if (total == 0) total = card;
    cached = apModeWebSdGbPair(used, total);
    return cached;
}

static void apModeWebRedirect(String path) {
    apModeWebServer.sendHeader("Location", "/sd?path=" + apModeWebUrlEncode(apModeWebCleanPath(path)), true);
    apModeWebServer.send(303, "text/plain", "SEE OTHER");
}

static bool apModeWebDeleteRecursive(String path) {
    path = apModeWebCleanPath(path);
    if (path == "/" || path == "") return false;
    File entry = SD.open(path);
    if (!entry) return false;
    if (!entry.isDirectory()) {
        entry.close();
        return SD.remove(path);
    }

    File child = entry.openNextFile();
    while (child) {
        String childName = String(child.name());
        bool childIsDir = child.isDirectory();
        int slash = childName.lastIndexOf('/');
        if (slash >= 0) childName = childName.substring(slash + 1);
        child.close();
        String childPath = apModeWebJoinPath(path, childName);
        if (childPath != "") {
            if (childIsDir) apModeWebDeleteRecursive(childPath);
            else SD.remove(childPath);
        }
        child = entry.openNextFile();
    }
    entry.close();
    return SD.rmdir(path);
}

static void apModeWebSendSdPage(String path) {
    path = apModeWebCleanPath(path);
    String sortMode = apModeWebServer.hasArg("sort") ? apModeWebServer.arg("sort") : "name";
    String sortDir = apModeWebServer.hasArg("dir") ? apModeWebServer.arg("dir") : "asc";
    if (sortMode != "ext") sortMode = "name";
    if (sortDir != "desc") sortDir = "asc";
    apModeWebSortField = (sortMode == "ext") ? 1 : 0;
    apModeWebSortDesc = (sortDir == "desc");

    String html;
    html.reserve(10000);
    html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Breach_OS SD</title><style>");
    html += F("body{background:#0e1115;color:#dceb1b;font-family:monospace;margin:0;padding:14px}a{color:#38bec9;text-decoration:none}h1{font-size:22px;margin:0 0 8px}");
    html += F(".shell,.panel,input,button,.sort,.filepick,.filehint{clip-path:polygon(0 0,100% 0,100% calc(100% - 14px),calc(100% - 14px) 100%,0 100%)}");
    html += F(".shell{position:relative;max-width:980px;margin:0 auto;padding:14px;border:0;background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#111722}");
    html += F(".panel{position:relative;border:0;padding:10px;margin:10px 0;background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#0e1115}");
    html += F("input,.sort,.filepick,.filehint{display:inline-block;position:relative;box-sizing:border-box;border:0;border-radius:0;outline:0;vertical-align:middle;background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#0e1115;color:#dceb1b;padding:7px 14px;margin:3px;font-family:monospace;-webkit-appearance:none;appearance:none}");
    html += F("button{display:inline-block;position:relative;border:0;background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#38bec9;color:#0e1115;padding:7px 14px;margin:3px;font-family:monospace;font-weight:bold}");
    html += F(".sort,.filepick,.filehint{min-width:96px;text-align:left;color:#dceb1b}.topnav{margin:10px 0}.topnav .sort{font-weight:bold}.filepick{cursor:pointer}.filehint{min-width:150px}.foldername{min-width:150px}.nativefile,input[type=hidden]{display:none}.sort.active{background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#dceb1b;color:#0e1115}");
    html += F("button.danger{background:linear-gradient(#ff003c,#ff003c) top left/100% 1px no-repeat,linear-gradient(#ff003c,#ff003c) top left/1px 100% no-repeat,linear-gradient(#ff003c,#ff003c) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#ff003c,#ff003c) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#ff003c 47%,#ff003c 53%,transparent 54%) bottom right/14px 14px no-repeat,#ff003c;color:#0e1115;font-weight:bold}");
    html += F(".dim{color:#87905e}.bad{color:#ff003c}.ok{color:#00ff75}table{width:100%;border-collapse:collapse}td,th{border-bottom:1px solid #26313c;padding:7px 4px;text-align:left}.act{white-space:nowrap}</style></head><body><main class='shell'>");
    html += F("<h1>BREACH_OS SD FILE MANAGER</h1>");
    html += F("<div class='topnav'><a class='sort active' href='/mirror'>START MIRROR</a><a class='sort' href='/sd?path="); html += apModeWebUrlEncode(path); html += F("&sort="); html += sortMode; html += F("&dir="); html += sortDir; html += F("'>SD FILES</a></div>");
    html += F("<div class='dim'>WEB: "); html += apModeWebHtml(apModeWebHostLabel()); html += F(" / http://"); html += apModeWebHtml(apModeIp); html += F("</div>");
    if (apModeWebLastStatus != "") {
        html += F("<div class='panel ok'>"); html += apModeWebHtml(apModeWebLastStatus); html += F("</div>");
    }

    if (!apModeWebMountSd()) {
        html += F("<div class='panel bad'>SD MOUNT FAIL. Insert an SD card and refresh.</div></main></body></html>");
        apModeWebServer.send(200, "text/html", html);
        return;
    }

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        html += F("<div class='panel bad'>Path not found: "); html += apModeWebHtml(path); html += F("</div><a href='/sd?path=%2F'>Back to /</a></main></body></html>");
        apModeWebServer.send(404, "text/html", html);
        return;
    }

    html += F("<div class='panel'><b>PATH</b> "); html += apModeWebHtml(path); html += F("<br>");
    if (path != "/") {
        html += F("<a href='/sd?path="); html += apModeWebUrlEncode(apModeWebParentPath(path)); html += F("&sort="); html += sortMode; html += F("&dir="); html += sortDir; html += F("'>[.. parent]</a>");
    } else {
        html += F("<span class='dim'>SD root</span>");
    }
    html += F("</div>");

    html += F("<div class='panel'><b>SORT</b> ");
    html += apModeWebSortLink(path, "name", "asc", "NAME A-Z", sortMode, sortDir);
    html += apModeWebSortLink(path, "name", "desc", "NAME Z-A", sortMode, sortDir);
    html += apModeWebSortLink(path, "ext", "asc", "EXT A-Z", sortMode, sortDir);
    html += apModeWebSortLink(path, "ext", "desc", "EXT Z-A", sortMode, sortDir);
    html += F("</div>");

    html += F("<div class='panel'><form method='POST' action='/upload' enctype='multipart/form-data'><input type='hidden' name='path' value='"); html += apModeWebHtml(path); html += F("'><input id='upfile' class='nativefile' type='file' name='file' onchange=\"document.getElementById('upname').textContent=this.files.length?this.files[0].name:'NO FILE CHOSEN'\"><label class='filepick' for='upfile'>CHOOSE FILE</label><span id='upname' class='filehint'>NO FILE CHOSEN</span><button>UPLOAD</button></form>");
    html += F("<form method='POST' action='/mkdir'><input type='hidden' name='path' value='"); html += apModeWebHtml(path); html += F("'><input class='foldername' name='name' maxlength='64' placeholder='new folder'><button>MKDIR</button></form></div>");

    html += F("<table><tr><th>NAME</th><th>TYPE</th><th>SIZE</th><th class='act'>ACTION</th></tr>");
    std::vector<ApModeWebEntry> entries;
    File file = dir.openNextFile();
    while (file && entries.size() < 160) {
        String name = String(file.name());
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        bool isDir = file.isDirectory();
        size_t size = file.size();
        file.close();
        if (!isSystemFile(name)) {
            ApModeWebEntry entry;
            entry.name = name;
            entry.path = apModeWebJoinPath(path, name);
            entry.ext = apModeWebExtension(name, isDir);
            entry.isDir = isDir;
            entry.size = size;
            entries.push_back(entry);
        }
        file = dir.openNextFile();
    }
    dir.close();
    std::sort(entries.begin(), entries.end(), [](const ApModeWebEntry &a, const ApModeWebEntry &b) {
        if (a.isDir != b.isDir) return a.isDir;
        String aName = a.name;
        String bName = b.name;
        aName.toLowerCase();
        bName.toLowerCase();
        String aKey = aName;
        String bKey = bName;
        if (apModeWebSortField == 1 && !a.isDir && !b.isDir) {
            aKey = a.ext;
            bKey = b.ext;
            if (aKey == bKey) {
                aKey = aName;
                bKey = bName;
            }
        }
        return apModeWebSortText(aKey, bKey, aName, bName);
    });

    for (const ApModeWebEntry &entry : entries) {
        html += F("<tr><td>");
        if (entry.isDir) {
            html += F("<a href='/sd?path="); html += apModeWebUrlEncode(entry.path); html += F("&sort="); html += sortMode; html += F("&dir="); html += sortDir; html += F("'>[DIR] "); html += apModeWebHtml(entry.name); html += F("</a>");
        } else {
            html += F("<a href='/download?path="); html += apModeWebUrlEncode(entry.path); html += F("'>"); html += apModeWebHtml(entry.name); html += F("</a>");
        }
        String typeText = entry.isDir ? String("DIR") : (entry.ext == "" ? String("FILE") : entry.ext);
        typeText.toUpperCase();
        html += F("</td><td>"); html += typeText;
        html += F("</td><td>"); html += entry.isDir ? String("--") : apModeWebSize(entry.size);
        html += F("</td><td class='act'><form method='POST' action='/delete' onsubmit=\"return confirm('DELETE?')\"><input type='hidden' name='path' value='"); html += apModeWebHtml(entry.path); html += F("'><button class='danger'>DEL</button></form></td></tr>");
    }
    if (entries.empty()) html += F("<tr><td colspan='4' class='dim'>EMPTY</td></tr>");
    html += F("</table></main></body></html>");
    apModeWebServer.send(200, "text/html", html);
}

static void apModeWebHandleRoot() {
    apModeWebRedirect("/");
}

static void apModeWebHandleSd() {
    String path = apModeWebServer.hasArg("path") ? apModeWebServer.arg("path") : "/";
    apModeWebSendSdPage(path);
}

static void apModeWebHandleMkdir() {
    String dir = apModeWebServer.hasArg("path") ? apModeWebServer.arg("path") : "/";
    String target = apModeWebJoinPath(dir, apModeWebServer.arg("name"));
    if (target == "") {
        apModeWebLastStatus = "MKDIR FAIL: BAD NAME";
    } else if (!apModeWebMountSd()) {
        apModeWebLastStatus = "MKDIR FAIL: SD MOUNT";
    } else if (SD.exists(target)) {
        apModeWebLastStatus = "MKDIR FAIL: EXISTS";
    } else {
        apModeWebLastStatus = SD.mkdir(target) ? "MKDIR OK: " + apModeWebBaseName(target) : "MKDIR FAIL";
    }
    apModeWebRedirect(dir);
}

static void apModeWebHandleDelete() {
    String path = apModeWebCleanPath(apModeWebServer.arg("path"));
    String parent = apModeWebParentPath(path);
    if (path == "/") {
        apModeWebLastStatus = "DELETE FAIL: ROOT LOCKED";
    } else if (!apModeWebMountSd()) {
        apModeWebLastStatus = "DELETE FAIL: SD MOUNT";
    } else {
        apModeWebLastStatus = apModeWebDeleteRecursive(path) ? "DELETED: " + apModeWebBaseName(path) : "DELETE FAIL";
    }
    apModeWebRedirect(parent);
}

static void apModeWebHandleDownload() {
    String path = apModeWebCleanPath(apModeWebServer.arg("path"));
    if (!apModeWebMountSd()) {
        apModeWebServer.send(503, "text/plain", "SD MOUNT FAIL");
        return;
    }
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        apModeWebServer.send(404, "text/plain", "FILE NOT FOUND");
        return;
    }
    apModeWebServer.sendHeader("Content-Disposition", String("attachment; filename=\"") + apModeWebBaseName(path) + "\"");
    apModeWebServer.streamFile(file, "application/octet-stream");
    file.close();
}

static void apModeWebHandleUploadDone() {
    if (apModeWebUploadFile) apModeWebUploadFile.close();
    apModeWebRedirect(apModeWebUploadDir);
}

static void apModeWebHandleUploadData() {
    HTTPUpload &upload = apModeWebServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        apModeWebUploadDir = apModeWebServer.hasArg("path") ? apModeWebCleanPath(apModeWebServer.arg("path")) : "/";
        String target = apModeWebJoinPath(apModeWebUploadDir, upload.filename);
        if (target == "" || !apModeWebMountSd()) {
            apModeWebLastStatus = "UPLOAD FAIL: SD/NAME";
            return;
        }
        if (SD.exists(target)) SD.remove(target);
        apModeWebUploadFile = SD.open(target, FILE_WRITE);
        apModeWebLastStatus = apModeWebUploadFile ? "UPLOADING: " + apModeWebBaseName(target) : "UPLOAD OPEN FAIL";
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (apModeWebUploadFile) apModeWebUploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (apModeWebUploadFile) {
            apModeWebUploadFile.close();
            apModeWebLastStatus = "UPLOAD OK: " + apModeWebCleanName(upload.filename) + " (" + apModeWebSize(upload.totalSize) + ")";
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (apModeWebUploadFile) apModeWebUploadFile.close();
        apModeWebLastStatus = "UPLOAD ABORTED";
    }
}

static void apModeWebHandleMirrorPage() {
    String html;
    html.reserve(9000);
    html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Breach_OS Mirror</title><style>");
    html += F("body{background:#0e1115;color:#dceb1b;font-family:monospace;margin:0;padding:14px}a{color:#38bec9;text-decoration:none}h1{font-size:22px;margin:0 0 8px}");
    html += F(".shell,.panel,.sort{clip-path:polygon(0 0,100% 0,100% calc(100% - 14px),calc(100% - 14px) 100%,0 100%)}");
    html += F(".shell{position:relative;max-width:1040px;margin:0 auto;padding:14px;border:0;background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#111722}");
    html += F(".panel,.sort{display:inline-block;position:relative;box-sizing:border-box;border:0;background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#0e1115;color:#dceb1b;padding:7px 14px;margin:3px;font-family:monospace}");
    html += F(".sort.active{background:linear-gradient(#38bec9,#38bec9) top left/100% 1px no-repeat,linear-gradient(#38bec9,#38bec9) top left/1px 100% no-repeat,linear-gradient(#38bec9,#38bec9) top right/1px calc(100% - 14px) no-repeat,linear-gradient(#38bec9,#38bec9) bottom left/calc(100% - 14px) 1px no-repeat,linear-gradient(135deg,transparent 46%,#38bec9 47%,#38bec9 53%,transparent 54%) bottom right/14px 14px no-repeat,#dceb1b;color:#0e1115;font-weight:bold}.topnav{margin:10px 0}.dim{color:#87905e}.warn{color:#ff003c}.stage{margin:12px 0;text-align:center}.telemetry{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:6px;margin:10px 0}.metric{display:block;margin:0}.metric b{display:block;color:#38bec9}.metric span{display:block;color:#dceb1b;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}");
    html += F("#screen{display:block;width:min(96vw,960px);height:auto;max-width:100%;margin:10px auto;background:#000;image-rendering:pixelated;image-rendering:crisp-edges;outline:1px solid #38bec9;cursor:crosshair}</style></head><body><main class='shell'>");
    html += F("<h1>BREACH_OS REMOTE MIRROR</h1><div class='topnav'><a class='sort' href='/sd?path=%2F'>SD FILES</a><a class='sort active' href='/mirror'>MIRROR ACTIVE</a></div>");
    html += F("<div class='dim'>WEB: "); html += apModeWebHtml(apModeWebHostLabel()); html += F(" / http://"); html += apModeWebHtml(apModeIp); html += F("</div>");
    html += F("<div class='panel warn'>REMOTE KEYBOARD ACTIVE AFTER CLICKING SCREEN</div><div class='stage'><canvas id='screen' width='240' height='135' tabindex='0'></canvas></div>");
    html += F("<div class='telemetry'><div class='panel metric'><b>PING</b><span id='ping'>-- ms</span></div><div class='panel metric'><b>SD INFO</b><span id='sdinfo'>--</span></div><div class='panel metric'><b>LAST BUTTON</b><span id='lastkey'>NONE</span></div><div class='panel metric'><b>UPTIME</b><span id='uptime'>-- s</span></div><div class='panel metric'><b>CLIENTS</b><span id='clients'>--</span></div><div class='panel metric'><b>HEAP</b><span id='heap'>--</span></div></div>");
    html += F("<div class='dim'>ARROWS / ENTER / ESC / BACKSPACE / TEXT</div><script>");
    html += F("const W=240,H=135,cv=document.getElementById('screen'),cx=cv.getContext('2d'),img=cx.createImageData(W,H),ping=document.getElementById('ping'),sdinfo=document.getElementById('sdinfo'),lastkey=document.getElementById('lastkey'),uptime=document.getElementById('uptime'),clients=document.getElementById('clients'),heap=document.getElementById('heap');let busy=false;cv.onclick=()=>cv.focus();function safeKey(k){return k.length===1?'TEXT':k.replace('Arrow','').toUpperCase();}async function status(){const t=performance.now();try{const r=await fetch('/mirror/status?x='+Date.now(),{cache:'no-store'});const s=await r.json();ping.textContent=Math.round(performance.now()-t)+' ms';sdinfo.textContent=s.sd||'--';lastkey.textContent=s.last||'NONE';uptime.textContent=(s.uptime||0)+' s';clients.textContent=s.clients;heap.textContent=s.heap;}catch(e){ping.textContent='ERR';}setTimeout(status,1500);}async function frame(){if(busy)return;busy=true;try{const r=await fetch('/mirror/frame?x='+Date.now(),{cache:'no-store'});const b=new Uint8Array(await r.arrayBuffer());if(b.length===W*H*2){let j=0;for(let i=0;i<b.length;i+=2){const p=b[i]|(b[i+1]<<8);img.data[j++]=((p>>11)&31)*255/31;img.data[j++]=((p>>5)&63)*255/63;img.data[j++]=(p&31)*255/31;img.data[j++]=255;}cx.putImageData(img,0,0);}}catch(e){}busy=false;setTimeout(frame,110);}status();frame();");
    html += F("function send(k){lastkey.textContent=safeKey(k);fetch('/mirror/key',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+encodeURIComponent(k)}).catch(()=>{});}const keys=new Set(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Enter','Backspace','Delete','Escape']);window.addEventListener('keydown',e=>{if(document.activeElement!==cv)return;if(keys.has(e.key)||e.key.length===1){e.preventDefault();send(e.key);}});cv.focus();</script></main></body></html>");
    apModeWebServer.send(200, "text/html", html);
}

static void apModeWebHandleMirrorFrame() {
    if (!apModeWebActive()) {
        apModeWebServer.send(503, "text/plain", "WEB UI OFFLINE");
        return;
    }
    apModeWebTouchClient();
    apModeWebServer.sendHeader("Cache-Control", "no-store");
    apModeWebServer.setContentLength(AP_MODE_MIRROR_FRAME_BYTES);
    apModeWebServer.send(200, "application/octet-stream", "");
    WiFiClient client = apModeWebServer.client();
    uint8_t row[AP_MODE_MIRROR_W * 2];
    for (int y = 0; y < AP_MODE_MIRROR_H && client.connected(); y++) {
        for (int x = 0; x < AP_MODE_MIRROR_W; x++) {
            uint16_t pixel = (uint16_t)canvas.readPixel(x, y);
            row[x * 2] = pixel & 0xFF;
            row[x * 2 + 1] = pixel >> 8;
        }
        client.write(row, sizeof(row));
    }
}

static void apModeWebHandleMirrorKey() {
    if (!apModeWebActive()) {
        apModeWebServer.send(503, "text/plain", "WEB UI OFFLINE");
        return;
    }
    apModeWebTouchClient();
    String key = apModeWebServer.hasArg("key") ? apModeWebServer.arg("key") : apModeWebServer.arg("plain");
    apModeQueueRemoteKey(key);
    apModeWebServer.send(200, "text/plain", "OK");
}

static void apModeWebHandleMirrorStatus() {
    if (!apModeWebActive()) {
        apModeWebServer.send(503, "application/json", "{\"error\":\"WEB UI OFFLINE\"}");
        return;
    }
    apModeWebTouchClient();
    apModeWebServer.sendHeader("Cache-Control", "no-store");
    String json;
    json.reserve(180);
    json += F("{\"sd\":\"");
    json += apModeWebJson(apModeMirrorSdInfo());
    json += F("\",\"last\":\"");
    json += apModeWebJson(apModeRemoteLastLabel);
    json += F("\",\"uptime\":");
    json += String(millis() / 1000UL);
    json += F(",\"clients\":");
    json += String(apModeWebRecentClientCount());
    json += F(",\"heap\":");
    json += String(ESP.getFreeHeap());
    json += F("}");
    apModeWebServer.send(200, "application/json", json);
}

static void apModeWebHandleNotFound() {
    apModeWebServer.send(404, "text/plain", "BREACH_OS AP WEB: NOT FOUND");
}

static void apModeStartWebServer() {
    if (!apModeWebRoutesConfigured) {
        apModeWebServer.on("/", HTTP_GET, apModeWebHandleRoot);
        apModeWebServer.on("/sd", HTTP_GET, apModeWebHandleSd);
        apModeWebServer.on("/mkdir", HTTP_POST, apModeWebHandleMkdir);
        apModeWebServer.on("/delete", HTTP_POST, apModeWebHandleDelete);
        apModeWebServer.on("/download", HTTP_GET, apModeWebHandleDownload);
        apModeWebServer.on("/mirror", HTTP_GET, apModeWebHandleMirrorPage);
        apModeWebServer.on("/mirror/frame", HTTP_GET, apModeWebHandleMirrorFrame);
        apModeWebServer.on("/mirror/key", HTTP_POST, apModeWebHandleMirrorKey);
        apModeWebServer.on("/mirror/status", HTTP_GET, apModeWebHandleMirrorStatus);
        apModeWebServer.on("/upload", HTTP_POST, apModeWebHandleUploadDone, apModeWebHandleUploadData);
        apModeWebServer.onNotFound(apModeWebHandleNotFound);
        apModeWebRoutesConfigured = true;
    }
    if (apModeWebServerStarted) apModeWebServer.stop();
    apModeWebServer.begin();
    apModeWebServerStarted = true;
    apModeWebLastStatus = "WEB UI READY";
}

static void apModeStopWebServer() {
    if (apModeWebUploadFile) apModeWebUploadFile.close();
    apModeClearRemoteInput();
    apModeWebClearClients();
    if (apModeWebServerStarted) apModeWebServer.stop();
    apModeWebServerStarted = false;
}

void serviceApModeWeb() {
    if (apModeWebActive() && apModeWebServerStarted) {
        apModeWebServer.handleClient();
    } else if (!apModeWebActive() && apModeWebServerStarted) {
        apModeStopWebServer();
    }
}

bool isApModeWebUiActive() {
    return apModeWebActive() && apModeWebServerStarted;
}

static bool apModeStartLanWeb() {
    if (WiFi.status() != WL_CONNECTED) {
        apModeStatus = "NO CONNECTED WIFI";
        return false;
    }

    if (apModeWebServerStarted) apModeStopWebServer();
    if (apModeRunning) {
        WiFi.softAPdisconnect(true);
        apModeRunning = false;
    }
    WiFi.mode(WIFI_STA);
    apModeLanWebActive = true;
    apModeUseLanSource = true;
    apModeIp = WiFi.localIP().toString();
    apModeStartWebServer();
    apModeStatus = apModeWebServerStarted ? String("NETWORK ") + apModeIp : String("WEB START FAIL");
    return apModeWebServerStarted;
}

static bool apModeStartAp() {
    apModeSsid.trim();
    apModePass.trim();
    if (apModeSsid == "") apModeSsid = "Breach_OS_AP";
    if (apModeSsid.length() > AP_MODE_MAX_SSID_LEN) apModeSsid = apModeSsid.substring(0, AP_MODE_MAX_SSID_LEN);
    apModeClampChannel();

    if (!apModeOpen && apModePass.length() < 8) {
        apModeStatus = "PASS MIN 8 CHARS";
        return false;
    }

    if (apModeWebServerStarted) apModeStopWebServer();
    apModeLanWebActive = false;
    apModeUseLanSource = false;
    apModeStaWasConnected = (WiFi.status() == WL_CONNECTED);
    WiFi.mode(apModeStaWasConnected ? WIFI_AP_STA : WIFI_AP);
    const char* pass = apModeOpen ? NULL : apModePass.c_str();
    bool ok = WiFi.softAP(apModeSsid.c_str(), pass, apModeChannel, 0, 4);
    if (!ok) {
        apModeRunning = false;
        apModeIp = "";
        apModeStatus = "AP START FAIL";
        return false;
    }

    apModeRunning = true;
    apModeIp = WiFi.softAPIP().toString();
    apModeStartWebServer();
    apModeStatus = apModeWebServerStarted ? String("WEB AP ") + apModeIp : String("AP ONLINE");
    apModeSavePrefs();
    return true;
}

static void apModeStopAp() {
    apModeStopWebServer();
    WiFi.softAPdisconnect(true);
    apModeRunning = false;
    apModeLanWebActive = false;
    apModeIp = "";
    apModeStatus = "AP STOPPED";
    if (apModeStaWasConnected || WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
    } else {
        WiFi.mode(WIFI_OFF);
    }
}

static void apModeStopLanWeb() {
    apModeStopWebServer();
    apModeLanWebActive = false;
    apModeIp = "";
    apModeStatus = "WEB UI STOPPED";
}

static void apModeStopWebUi() {
    if (apModeRunning) apModeStopAp();
    else if (apModeLanWebActive) apModeStopLanWeb();
    else apModeStatus = "WEB UI OFFLINE";
}

static bool apModeStartSelectedWeb() {
    if (apModeUseLanSource) {
        if (WiFi.status() == WL_CONNECTED) {
            return apModeStartLanWeb();
        }
        resumeOtaAfterWifi = false;
        resumeTextfilesAfterWifi = false;
        resumeApModeLanWebAfterWifi = true;
        startWifiScan();
        return false;
    }
    return apModeStartAp();
}

static void apModeReturnToNetworkNode() {
    appState = STATE_MAIN_MENU;
    mainMenuFocus = 4;
    currentMenuScroll = mainMenuFocus;
    targetMenuScroll = mainMenuFocus;
    showMenuDesc = false;
    descAnimWidth = 0.0;
    drawMainMenu();
}

static void apModeReturnToSourcePrompt() {
    apModePromptActive = true;
    apModeSourceFocus = apModeUseLanSource ? 1 : 0;
    currentApModeSourceScroll = (float)apModeSourceFocus;
    targetApModeSourceScroll = (float)apModeSourceFocus;
    apModeFocus = 0;
    apModeListScroll = 0;
    appState = STATE_AP_MODE;
    drawApModeScreen();
}

static void apModeEnsureVisible() {
    if (apModeFocus < 0) apModeFocus = 0;
    if (apModeFocus >= AP_MODE_FOCUS_COUNT) apModeFocus = AP_MODE_FOCUS_COUNT - 1;
    int listFocus = apModeFocus;
    if (listFocus >= AP_MODE_ROW_COUNT) listFocus = AP_MODE_ROW_COUNT - 1;
    if (listFocus < 0) listFocus = 0;
    if (listFocus < apModeListScroll) apModeListScroll = listFocus;
    if (listFocus >= apModeListScroll + AP_MODE_VISIBLE_ROWS) {
        apModeListScroll = listFocus - AP_MODE_VISIBLE_ROWS + 1;
    }
    int maxScroll = AP_MODE_ROW_COUNT - AP_MODE_VISIBLE_ROWS;
    if (maxScroll < 0) maxScroll = 0;
    if (apModeListScroll > maxScroll) apModeListScroll = maxScroll;
    if (apModeListScroll < 0) apModeListScroll = 0;
}

static void apModeDrawVerticalScrollIndicator(int scroll, int maxScroll, int x, int y, int h) {
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

static void apModeDrawSolidChippedButton(int x, int y, int w, int h, uint16_t fillColor, uint16_t outlineColor) {
    int chip = (h > 25) ? 8 : 5;
    if (w <= chip) return;
    for (int yy = 0; yy <= h; yy++) {
        int lineW = w + 1;
        if (yy > h - chip) {
            lineW = w - (yy - (h - chip)) + 1;
        }
        if (lineW > 0) canvas.drawLine(x, y + yy, x + lineW - 1, y + yy, fillColor);
    }
    drawChippedButton(x, y, w, h, outlineColor);
}

static void apModeRowText(int index, String &label, String &value) {
    switch (index) {
        case 0:
            label = apModeWebActive() ? "STOP" : "START";
            value = apModeUseLanSource ? "NETWORK" : "AP MODE";
            break;
        case 1:
            label = "SSID";
            value = apModeSsid;
            if (value == "") value = "<EMPTY>";
            break;
        case 2:
            label = "PASS";
            value = apModeMaskedPass();
            break;
        case 3:
            label = "CHANNEL";
            value = String(apModeChannel);
            break;
        case 4:
            label = "SECURITY";
            value = apModeOpen ? "OPEN" : "WPA2 PSK";
            break;
        default:
            label = "";
            value = "";
            break;
    }
}

static void apModeDrawRow(int index, int y) {
    bool selected = (apModeFocus == index);
    uint16_t color = selected ? CP_YELLOW : CP_DIM;
    String label;
    String value;
    apModeRowText(index, label, value);

    if (index == 0) {
        uint16_t actionColor = apModeWebActive() ? CP_RED : CP_GREEN;
        if (selected) {
            apModeDrawSolidChippedButton(6, y - 2, 216, 20, actionColor, actionColor);
        } else {
            drawChippedButton(6, y - 2, 216, 20, actionColor);
        }
        canvas.setTextSize(2);
        canvas.setTextColor(selected ? CP_BG : actionColor);
        canvas.drawCenterString(label, 114, y + 2);
        return;
    }

    if (selected && (index == 1 || index == 2) && blinkState) value += "_";
    if (selected && (index == 3 || index == 4)) value = "< " + value + " >";

    drawChippedButton(6, y - 2, 216, 20, color);
    canvas.setTextSize(1);

    String shownValue = value;
    const int valueRight = 214;
    const int minValueX = 78;
    while (shownValue.length() > 1 && canvas.textWidth(shownValue) > valueRight - minValueX) {
        shownValue = apModeShort(value, shownValue.length() - 1);
    }

    canvas.setTextColor(color);
    canvas.setCursor(13, y + 5);
    canvas.print(label);
    canvas.setTextColor(selected ? CP_CYAN : CP_DIM);
    int valueX = valueRight - canvas.textWidth(shownValue);
    if (valueX < minValueX) valueX = minValueX;
    canvas.setCursor(valueX, y + 5);
    canvas.print(shownValue);
}

static void drawApModeSourcePrompt() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawGlitchText("WEB UI", 72, 4, 1, CP_CYAN, true, true);
    drawTopStatusIcons(132, 1);

    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);

    std::vector<String> labels = {"AP MODE", "NETWORK", "BACK"};
    for (int i = 0; i < AP_MODE_SOURCE_COUNT; i++) {
        float rawOffset = i - currentApModeSourceScroll;
        float offset = fmod(rawOffset, (float)AP_MODE_SOURCE_COUNT);
        float halfItems = (float)AP_MODE_SOURCE_COUNT / 2.0;
        if (offset > halfItems) offset -= (float)AP_MODE_SOURCE_COUNT;
        if (offset < -halfItems) offset += (float)AP_MODE_SOURCE_COUNT;
        if (abs(offset) > 1.5) continue;

        float angle = offset * 0.391;
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        bool selected = (i == apModeSourceFocus);
        uint16_t tickColor = selected ? CP_CYAN : CP_DIM;
        float tickEndX = -80 + cos(angle) * (selected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (selected ? 117 : 115);
        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tickColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tickColor);
        if (selected) canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tickColor);

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

    drawWheelPositionIndicator(currentApModeSourceScroll, AP_MODE_SOURCE_COUNT);
    pushCanvas();
}

bool updateApModeSourcePromptAnimation() {
    if (!apModePromptActive) return false;
    if (abs(currentApModeSourceScroll - targetApModeSourceScroll) <= 0.01) return false;
    currentApModeSourceScroll += (targetApModeSourceScroll - currentApModeSourceScroll) * 0.3;
    if (abs(currentApModeSourceScroll - targetApModeSourceScroll) <= 0.01) {
        currentApModeSourceScroll = targetApModeSourceScroll;
    }
    return true;
}

void enterApMode() {
    if (apModeSsid == "") apModeSsid = "Breach_OS_AP";
    if (apModePass == "") apModePass = "breach123";
    apModeClampChannel();
    apModePromptActive = true;
    apModeSourceFocus = 0;
    currentApModeSourceScroll = 0.0;
    targetApModeSourceScroll = 0.0;
    apModeFocus = 0;
    apModeListScroll = 0;
    if (apModeRunning) {
        apModeIp = WiFi.softAPIP().toString();
        apModeStatus = "WEB AP " + apModeIp;
    } else if (apModeLanWebActive) {
        apModeIp = WiFi.localIP().toString();
        apModeStatus = "NETWORK " + apModeIp;
    } else {
        apModeStatus = "READY";
    }
    appState = STATE_AP_MODE;
    drawApModeScreen();
}

void enterApModeLanWeb() {
    if (apModeSsid == "") apModeSsid = "Breach_OS_AP";
    if (apModePass == "") apModePass = "breach123";
    apModeClampChannel();
    appState = STATE_AP_MODE;
    apModePromptActive = false;
    apModeUseLanSource = true;
    apModeFocus = 0;
    apModeListScroll = 0;

    if (WiFi.status() == WL_CONNECTED) {
        apModeIp = WiFi.localIP().toString();
        apModeStatus = apModeLanWebActive ? (String("NETWORK ") + apModeIp) : String("NETWORK READY");
        apModeEnsureVisible();
    } else {
        apModePromptActive = true;
        apModeSourceFocus = 1;
        currentApModeSourceScroll = 1.0;
        targetApModeSourceScroll = 1.0;
    }
    drawApModeScreen();
}

void drawApModeScreen() {
    if (apModePromptActive) {
        drawApModeSourcePrompt();
        return;
    }

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    String apModeHeader = apModeUseLanSource ? "NETWORK" : "AP MODE";
    drawGlitchText(apModeHeader, 72, 4, 1, apModeWebActive() ? CP_YELLOW : CP_CYAN, true, true);
    drawTopStatusIcons(132, 1);
    canvas.drawLine(5, 18, 235, 18, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(8, 22);
    canvas.print("TYPE EDIT  <> CH/SEC  R DEFAULT");

    canvas.setTextColor(apModeWebActive() ? CP_GREEN : CP_CYAN);
    canvas.setCursor(8, 32);
    String statusLine = apModeStatus;
    if (apModeRunning) {
        statusLine += " C" + String(WiFi.softAPgetStationNum());
    }
    canvas.print(apModeShort(statusLine, 31));

    apModeEnsureVisible();
    int y = 45;
    for (int row = 0; row < AP_MODE_VISIBLE_ROWS; row++) {
        int idx = apModeListScroll + row;
        if (idx >= AP_MODE_ROW_COUNT) break;
        apModeDrawRow(idx, y);
        y += 23;
    }
    int maxIndicator = AP_MODE_FOCUS_COUNT - 1;
    if (maxIndicator < 0) maxIndicator = 0;
    apModeDrawVerticalScrollIndicator(apModeFocus, maxIndicator, 229, 45, 78);
    pushCanvas();
}

void handleApModeInput(Keyboard_Class::KeysState status) {
    if (apModePromptActive) {
        bool hasUp = false, hasDown = false, hasBack = status.del;
        for (char c : status.word) {
            if (c == ';') hasUp = true;
            else if (c == '.') hasDown = true;
            else if (c == ',' || c == '`') hasBack = true;
        }

        if (hasBack) {
            playSound(sound_select, sound_select_size);
            apModeReturnToNetworkNode();
            return;
        }
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            apModeSourceFocus = (apModeSourceFocus - 1 + AP_MODE_SOURCE_COUNT) % AP_MODE_SOURCE_COUNT;
            targetApModeSourceScroll -= 1.0;
        } else if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            apModeSourceFocus = (apModeSourceFocus + 1) % AP_MODE_SOURCE_COUNT;
            targetApModeSourceScroll += 1.0;
        }

        if (!status.enter) return;
        playSound(sound_select, sound_select_size);

        if (apModeSourceFocus == 0) {
            apModePromptActive = false;
            apModeUseLanSource = false;
            apModeFocus = 0;
            apModeListScroll = 0;
            apModeEnsureVisible();
            apModeStatus = apModeRunning ? (String("WEB AP ") + WiFi.softAPIP().toString()) : String("AP CONFIG READY");
        } else if (apModeSourceFocus == 1) {
            if (WiFi.status() == WL_CONNECTED) {
                resumeApModeLanWebAfterWifi = false;
                apModePromptActive = false;
                apModeUseLanSource = true;
                apModeFocus = 0;
                apModeListScroll = 0;
                apModeIp = WiFi.localIP().toString();
                apModeStatus = apModeLanWebActive ? (String("NETWORK ") + apModeIp) : String("NETWORK READY");
                apModeEnsureVisible();
            } else {
                resumeOtaAfterWifi = false;
                resumeTextfilesAfterWifi = false;
                resumeApModeLanWebAfterWifi = true;
                startWifiScan();
            }
        } else {
            apModeReturnToNetworkNode();
        }
        return;
    }

    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasBack = status.del;
    bool resetDefaults = false;
    bool typed = false;

    for (char c : status.word) {
        if (c == ';') hasUp = true;
        else if (c == '.') hasDown = true;
        else if (c == ',') hasLeft = true;
        else if (c == '/') hasRight = true;
        else if (c == '`') hasBack = true;
        else if (c == 'r' || c == 'R') resetDefaults = true;
        else if (c >= 32 && c <= 126) {
            if (apModeFocus == 1 && apModeSsid.length() < AP_MODE_MAX_SSID_LEN) {
                apModeSsid += c;
                typed = true;
            } else if (apModeFocus == 2 && apModePass.length() < AP_MODE_MAX_PASS_LEN) {
                apModePass += c;
                typed = true;
            }
        }
    }

    if (resetDefaults) {
        playSound(sound_select, sound_select_size);
        apModeResetDefaults();
        return;
    }

    if (status.del) {
        if (apModeFocus == 1 && apModeSsid.length() > 0) {
            apModeSsid.remove(apModeSsid.length() - 1);
            typed = true;
        } else if (apModeFocus == 2 && apModePass.length() > 0) {
            apModePass.remove(apModePass.length() - 1);
            typed = true;
        } else {
            hasBack = true;
        }
    }

    if (typed) {
        apModeStatus = "EDITING";
        return;
    }

    if (hasBack || (hasLeft && apModeFocus != 3 && apModeFocus != 4)) {
        playSound(sound_select, sound_select_size);
        apModeSavePrefs();
        apModeReturnToSourcePrompt();
        return;
    }

    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        apModeFocus = (apModeFocus - 1 + AP_MODE_FOCUS_COUNT) % AP_MODE_FOCUS_COUNT;
    }
    if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        apModeFocus = (apModeFocus + 1) % AP_MODE_FOCUS_COUNT;
    }

    if (apModeFocus == 3 && (hasLeft || hasRight)) {
        playSound(sound_hover, sound_hover_size);
        apModeChannel += hasRight ? 1 : -1;
        apModeClampChannel();
        apModeStatus = "CHANNEL " + String(apModeChannel);
    } else if (apModeFocus == 4 && (hasLeft || hasRight)) {
        playSound(sound_hover, sound_hover_size);
        apModeOpen = !apModeOpen;
        apModeStatus = apModeOpen ? "OPEN SECURITY" : "WPA2 SECURITY";
    }

    if (!status.enter) return;
    playSound(sound_select, sound_select_size);

    if (apModeFocus == 0) {
        if (apModeWebActive()) apModeStopWebUi();
        else apModeStartSelectedWeb();
    } else if (apModeFocus == 3) {
        apModeChannel++;
        apModeClampChannel();
        apModeStatus = "CHANNEL " + String(apModeChannel);
    } else if (apModeFocus == 4) {
        apModeOpen = !apModeOpen;
        apModeStatus = apModeOpen ? "OPEN SECURITY" : "WPA2 SECURITY";
    } else {
        apModeFocus = (apModeFocus + 1) % AP_MODE_FOCUS_COUNT;
    }
}
