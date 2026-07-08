// HARDWARE NODE / PASSWORDS: encrypted SD password vault with explicit USB HID payload typing.

static constexpr const char* PM_OS_DIR = "/Breach_OS";
static constexpr const char* PM_VAULT_FILE = "/Breach_OS/passwords.vault";
static constexpr const char* PM_VAULT_TMP_FILE = "/Breach_OS/passwords.vault.tmp";
static constexpr const char* PM_VAULT_BAK_FILE = "/Breach_OS/passwords.vault.bak";
static constexpr const char* PM_VAULT_HEADER = "BOS_PASS_VAULT_V1";
static constexpr int PM_SD_CS = 12;
static constexpr uint32_t PM_SD_SPI_HZ = 20000000;
static constexpr int PM_MODE_UNLOCK = 0;
static constexpr int PM_MODE_CREATE = 1;
static constexpr int PM_MODE_CONFIRM_MASTER = 2;
static constexpr int PM_MODE_LIST = 3;
static constexpr int PM_MODE_DETAIL = 4;
static constexpr int PM_MODE_EDIT = 5;
static constexpr int PM_MODE_DELETE_CONFIRM = 6;
static constexpr int PM_MODE_HID_PICKER = 7;
static constexpr int PM_MODE_HID_CONFIRM = 8;
static constexpr int PM_MODE_HID_RUNNING = 9;
static constexpr int PM_MODE_ERROR = 10;
static constexpr int PM_HID_ACCOUNT = 0;
static constexpr int PM_HID_PASSWORD = 1;
static constexpr int PM_HID_LOGIN = 2;
static constexpr int PM_HID_LOGIN_ENTER = 3;
static constexpr int PM_MAX_ENTRIES = 50;
static constexpr int PM_MAX_SERVICE_LEN = 32;
static constexpr int PM_MAX_ACCOUNT_LEN = 64;
static constexpr int PM_MAX_PASSWORD_LEN = 96;
static constexpr int PM_MAX_NOTE_LEN = 96;
static constexpr int PM_MAX_JSON_BYTES = 12288;
static constexpr uint32_t PM_KDF_ITERS = 30000;
static constexpr unsigned long PM_UNLOCK_TIMEOUT_MS = 5UL * 60UL * 1000UL;
static bool passwordManagerShowPassword = false;

static String passwordManagerShort(String text, int maxLen) {
    if ((int)text.length() <= maxLen) return text;
    if (maxLen <= 3) return text.substring(0, maxLen);
    return text.substring(0, maxLen - 3) + "...";
}

static String passwordManagerFitText(String text, int maxWidth) {
    if (canvas.textWidth(text) <= maxWidth) return text;
    while (text.length() > 3 && canvas.textWidth(text.substring(0, text.length() - 3) + "...") > maxWidth) {
        text.remove(text.length() - 1);
    }
    if (text.length() <= 3) return passwordManagerShort(text, 3);
    return text.substring(0, text.length() - 3) + "...";
}

static String passwordManagerMasked(String text) {
    int count = text.length();
    if (count <= 0) return "<EMPTY>";
    if (count > 14) count = 14;
    String masked = "";
    for (int i = 0; i < count; i++) masked += "*";
    return masked;
}

static const char* passwordManagerFieldName(int field) {
    if (field == 0) return "SERVICE";
    if (field == 1) return "ACCOUNT";
    if (field == 2) return "PASSWORD";
    return "NOTE";
}

static int passwordManagerFieldLimit(int field) {
    if (field == 0) return PM_MAX_SERVICE_LEN;
    if (field == 1) return PM_MAX_ACCOUNT_LEN;
    if (field == 2) return PM_MAX_PASSWORD_LEN;
    return PM_MAX_NOTE_LEN;
}

static String passwordManagerEditFieldValue() {
    if (passwordManagerEditField == 0) return passwordManagerEditEntry.service;
    if (passwordManagerEditField == 1) return passwordManagerEditEntry.account;
    if (passwordManagerEditField == 2) return passwordManagerEditEntry.password;
    return passwordManagerEditEntry.note;
}

static void passwordManagerSetEditFieldValue(String value) {
    value.replace("\r", "");
    value.replace("\n", " ");
    int limit = passwordManagerFieldLimit(passwordManagerEditField);
    if ((int)value.length() > limit) value = value.substring(0, limit);
    if (passwordManagerEditField == 0) passwordManagerEditEntry.service = value;
    else if (passwordManagerEditField == 1) passwordManagerEditEntry.account = value;
    else if (passwordManagerEditField == 2) passwordManagerEditEntry.password = value;
    else passwordManagerEditEntry.note = value;
}

static void passwordManagerTouch() {
    passwordManagerLastActivityMs = millis();
}

static void passwordManagerClearEditEntry() {
    passwordManagerEditEntry.service = "";
    passwordManagerEditEntry.account = "";
    passwordManagerEditEntry.password = "";
    passwordManagerEditEntry.note = "";
}

static void passwordManagerClearStoredEntry(int index) {
    if (index < 0 || index >= (int)passwordEntries.size()) return;
    passwordEntries[index].service = "";
    passwordEntries[index].account = "";
    passwordEntries[index].password = "";
    passwordEntries[index].note = "";
}

static void passwordManagerClearKey() {
    memset(passwordManagerKey, 0, sizeof(passwordManagerKey));
    passwordManagerKeyReady = false;
}

static void passwordManagerClearVaultRam() {
    for (size_t i = 0; i < passwordEntries.size(); i++) {
        passwordManagerClearStoredEntry((int)i);
    }
    passwordEntries.clear();
    passwordManagerClearEditEntry();
    passwordManagerInput = "";
    passwordManagerInputConfirm = "";
    passwordManagerClearKey();
    passwordManagerUnlocked = false;
}

static void passwordManagerLock(String statusText) {
    passwordManagerClearVaultRam();
    passwordManagerMode = PM_MODE_UNLOCK;
    passwordManagerFocus = 0;
    passwordManagerScrollOffset = 0;
    passwordManagerActionFocus = 0;
    passwordManagerEditField = 0;
    passwordManagerSelectedIndex = 0;
    passwordManagerHidPayloadFocus = 0;
    passwordManagerPendingHidAction = 0;
    passwordManagerDeleteConfirm = 0;
    passwordManagerHidAbortFlag = false;
    passwordManagerShowPassword = false;
    passwordManagerStatus = statusText;
}

static bool passwordManagerMountSd() {
    SPI.begin(40, 39, 14, PM_SD_CS);
    if (!SD.begin(PM_SD_CS, SPI, PM_SD_SPI_HZ) || SD.cardType() == CARD_NONE) {
        passwordManagerStatus = "SD MOUNT FAIL";
        return false;
    }
    if (!SD.exists(PM_OS_DIR) && !SD.mkdir(PM_OS_DIR)) {
        passwordManagerStatus = "BREACH_OS DIR FAIL";
        return false;
    }
    return true;
}

static bool passwordManagerRandomBytes(uint8_t *out, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        uint32_t r = esp_random();
        for (int i = 0; i < 4 && pos < len; i++) {
            out[pos++] = (uint8_t)((r >> (8 * i)) & 0xFF);
        }
    }
    return true;
}

static bool passwordManagerBase64Encode(const uint8_t *data, size_t len, String &out) {
    out = "";
    size_t outLen = 0;
    int ret = mbedtls_base64_encode(NULL, 0, &outLen, data, len);
    if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return false;
    std::vector<uint8_t> buffer(outLen + 1);
    ret = mbedtls_base64_encode(buffer.data(), buffer.size(), &outLen, data, len);
    if (ret != 0) return false;
    buffer[outLen] = 0;
    out = String((const char*)buffer.data());
    return true;
}

static bool passwordManagerBase64Decode(String value, std::vector<uint8_t> &out) {
    value.trim();
    out.clear();
    if (value == "") return true;
    size_t outLen = 0;
    int ret = mbedtls_base64_decode(NULL, 0, &outLen, (const uint8_t*)value.c_str(), value.length());
    if (ret != 0 && ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return false;
    out.resize(outLen + 1);
    ret = mbedtls_base64_decode(out.data(), out.size(), &outLen, (const uint8_t*)value.c_str(), value.length());
    if (ret != 0) {
        out.clear();
        return false;
    }
    out.resize(outLen);
    return true;
}

static bool passwordManagerHmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t out[32]) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return false;
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    bool ok = false;
    if (mbedtls_md_setup(&ctx, info, 1) == 0 &&
        mbedtls_md_hmac_starts(&ctx, key, keyLen) == 0 &&
        mbedtls_md_hmac_update(&ctx, data, dataLen) == 0 &&
        mbedtls_md_hmac_finish(&ctx, out) == 0) {
        ok = true;
    }
    mbedtls_md_free(&ctx);
    return ok;
}

static bool passwordManagerDeriveKey(String master, const uint8_t *salt, uint8_t outKey[32]) {
    if (master.length() < 4) {
        passwordManagerStatus = "MASTER TOO SHORT";
        return false;
    }
    const uint8_t *pass = (const uint8_t*)master.c_str();
    size_t passLen = master.length();
    uint8_t u[32];
    uint8_t t[32];
    uint8_t saltBlock[20];
    memcpy(saltBlock, salt, 16);

    size_t generated = 0;
    uint32_t blockIndex = 1;
    while (generated < 32) {
        saltBlock[16] = (uint8_t)((blockIndex >> 24) & 0xFF);
        saltBlock[17] = (uint8_t)((blockIndex >> 16) & 0xFF);
        saltBlock[18] = (uint8_t)((blockIndex >> 8) & 0xFF);
        saltBlock[19] = (uint8_t)(blockIndex & 0xFF);
        if (!passwordManagerHmacSha256(pass, passLen, saltBlock, sizeof(saltBlock), u)) return false;
        memcpy(t, u, sizeof(t));
        for (uint32_t iter = 1; iter < PM_KDF_ITERS; iter++) {
            if (!passwordManagerHmacSha256(pass, passLen, u, sizeof(u), u)) return false;
            for (int i = 0; i < 32; i++) t[i] ^= u[i];
            if ((iter % 8000) == 0) delay(1);
        }
        size_t copyLen = 32 - generated;
        if (copyLen > 32) copyLen = 32;
        memcpy(outKey + generated, t, copyLen);
        generated += copyLen;
        blockIndex++;
    }
    memset(u, 0, sizeof(u));
    memset(t, 0, sizeof(t));
    return true;
}

static bool passwordManagerEncrypt(String plaintext, const uint8_t key[32], const uint8_t nonce[12], std::vector<uint8_t> &cipher, uint8_t tag[16]) {
    cipher.clear();
    cipher.resize(plaintext.length());
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret == 0) {
        ret = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plaintext.length(), nonce, 12,
                                        (const uint8_t*)PM_VAULT_HEADER, strlen(PM_VAULT_HEADER),
                                        (const uint8_t*)plaintext.c_str(), cipher.data(), 16, tag);
    }
    mbedtls_gcm_free(&ctx);
    if (ret != 0) cipher.clear();
    return ret == 0;
}

static bool passwordManagerDecrypt(const std::vector<uint8_t> &cipher, const uint8_t key[32], const uint8_t nonce[12], const uint8_t tag[16], String &plaintext) {
    plaintext = "";
    std::vector<uint8_t> plain(cipher.size() + 1);
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (ret == 0) {
        ret = mbedtls_gcm_auth_decrypt(&ctx, cipher.size(), nonce, 12,
                                       (const uint8_t*)PM_VAULT_HEADER, strlen(PM_VAULT_HEADER),
                                       tag, 16, cipher.data(), plain.data());
    }
    mbedtls_gcm_free(&ctx);
    if (ret != 0) return false;
    plain[cipher.size()] = 0;
    plaintext = String((const char*)plain.data());
    return true;
}

static bool passwordManagerEntriesToJson(String &json) {
    JsonDocument doc;
    doc["version"] = 1;
    JsonArray entries = doc["entries"].to<JsonArray>();
    for (size_t i = 0; i < passwordEntries.size() && i < PM_MAX_ENTRIES; i++) {
        JsonObject obj = entries.add<JsonObject>();
        obj["service"] = passwordEntries[i].service;
        obj["account"] = passwordEntries[i].account;
        obj["password"] = passwordEntries[i].password;
        obj["note"] = passwordEntries[i].note;
    }
    json = "";
    serializeJson(doc, json);
    if ((int)json.length() > PM_MAX_JSON_BYTES) {
        passwordManagerStatus = "VAULT TOO LARGE";
        return false;
    }
    return true;
}

static String passwordManagerLimitField(String value, int limit) {
    value.replace("\r", "");
    value.replace("\n", " ");
    if ((int)value.length() > limit) value = value.substring(0, limit);
    return value;
}

static bool passwordManagerEntriesFromJson(String json) {
    if ((int)json.length() > PM_MAX_JSON_BYTES) {
        passwordManagerStatus = "VAULT TOO LARGE";
        return false;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        passwordManagerStatus = "JSON FAIL";
        return false;
    }
    if (doc["version"].as<int>() != 1) {
        passwordManagerStatus = "VAULT VERSION FAIL";
        return false;
    }
    JsonArray entries = doc["entries"].as<JsonArray>();
    passwordEntries.clear();
    for (JsonVariant v : entries) {
        if ((int)passwordEntries.size() >= PM_MAX_ENTRIES) break;
        PasswordEntry entry;
        entry.service = passwordManagerLimitField(v["service"].as<String>(), PM_MAX_SERVICE_LEN);
        entry.account = passwordManagerLimitField(v["account"].as<String>(), PM_MAX_ACCOUNT_LEN);
        entry.password = passwordManagerLimitField(v["password"].as<String>(), PM_MAX_PASSWORD_LEN);
        entry.note = passwordManagerLimitField(v["note"].as<String>(), PM_MAX_NOTE_LEN);
        if (entry.service != "" && entry.password != "") passwordEntries.push_back(entry);
    }
    return true;
}

static bool passwordManagerCopyFile(const char* fromPath, const char* toPath) {
    File src = SD.open(fromPath, FILE_READ);
    if (!src) return false;
    if (SD.exists(toPath)) SD.remove(toPath);
    File dst = SD.open(toPath, FILE_WRITE);
    if (!dst) {
        src.close();
        return false;
    }
    uint8_t buffer[256];
    while (src.available()) {
        size_t got = src.read(buffer, sizeof(buffer));
        if (got == 0) break;
        if (dst.write(buffer, got) != got) {
            src.close();
            dst.close();
            return false;
        }
    }
    src.close();
    dst.close();
    return true;
}

static bool passwordManagerSaveVault() {
    if (!passwordManagerKeyReady) {
        passwordManagerStatus = "LOCKED";
        return false;
    }
    if (!passwordManagerMountSd()) return false;

    String json;
    if (!passwordManagerEntriesToJson(json)) return false;

    uint8_t nonce[12];
    uint8_t tag[16];
    std::vector<uint8_t> cipher;
    passwordManagerRandomBytes(nonce, sizeof(nonce));
    if (!passwordManagerEncrypt(json, passwordManagerKey, nonce, cipher, tag)) {
        passwordManagerStatus = "ENCRYPT FAIL";
        return false;
    }

    String saltB64, nonceB64, tagB64, dataB64;
    if (!passwordManagerBase64Encode(passwordManagerSalt, sizeof(passwordManagerSalt), saltB64) ||
        !passwordManagerBase64Encode(nonce, sizeof(nonce), nonceB64) ||
        !passwordManagerBase64Encode(tag, sizeof(tag), tagB64) ||
        !passwordManagerBase64Encode(cipher.data(), cipher.size(), dataB64)) {
        passwordManagerStatus = "B64 FAIL";
        return false;
    }

    if (SD.exists(PM_VAULT_TMP_FILE)) SD.remove(PM_VAULT_TMP_FILE);
    File file = SD.open(PM_VAULT_TMP_FILE, FILE_WRITE);
    if (!file) {
        passwordManagerStatus = "TMP OPEN FAIL";
        return false;
    }
    file.println(PM_VAULT_HEADER);
    file.println("kdf=pbkdf2-sha256");
    file.println("iters=" + String((uint32_t)PM_KDF_ITERS));
    file.println("salt=" + saltB64);
    file.println("nonce=" + nonceB64);
    file.println("tag=" + tagB64);
    file.println("data=" + dataB64);
    file.close();

    if (SD.exists(PM_VAULT_FILE)) {
        if (SD.exists(PM_VAULT_BAK_FILE)) SD.remove(PM_VAULT_BAK_FILE);
        passwordManagerCopyFile(PM_VAULT_FILE, PM_VAULT_BAK_FILE);
        SD.remove(PM_VAULT_FILE);
    }
    if (!SD.rename(PM_VAULT_TMP_FILE, PM_VAULT_FILE)) {
        if (!passwordManagerCopyFile(PM_VAULT_TMP_FILE, PM_VAULT_FILE)) {
            passwordManagerStatus = "SAVE FAIL";
            return false;
        }
        SD.remove(PM_VAULT_TMP_FILE);
    }
    passwordManagerStatus = "SAVED";
    return true;
}

static bool passwordManagerParseVaultFile(String &saltB64, String &nonceB64, String &tagB64, String &dataB64) {
    saltB64 = nonceB64 = tagB64 = dataB64 = "";
    File file = SD.open(PM_VAULT_FILE, FILE_READ);
    if (!file) {
        passwordManagerStatus = "VAULT OPEN FAIL";
        return false;
    }
    String header = file.readStringUntil('\n');
    header.replace("\r", "");
    header.trim();
    if (header != PM_VAULT_HEADER) {
        file.close();
        passwordManagerStatus = "VAULT HEADER FAIL";
        return false;
    }
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.replace("\r", "");
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        String value = line.substring(eq + 1);
        value.trim();
        if (key == "salt") saltB64 = value;
        else if (key == "nonce") nonceB64 = value;
        else if (key == "tag") tagB64 = value;
        else if (key == "data") dataB64 = value;
    }
    file.close();
    if (saltB64 == "" || nonceB64 == "" || tagB64 == "" || dataB64 == "") {
        passwordManagerStatus = "VAULT FIELD FAIL";
        return false;
    }
    return true;
}

static bool passwordManagerLoadVault(String master) {
    if (!passwordManagerMountSd()) return false;
    if (!SD.exists(PM_VAULT_FILE)) {
        passwordManagerStatus = "NO VAULT";
        return false;
    }

    String saltB64, nonceB64, tagB64, dataB64;
    if (!passwordManagerParseVaultFile(saltB64, nonceB64, tagB64, dataB64)) return false;

    std::vector<uint8_t> saltVec, nonceVec, tagVec, cipher;
    if (!passwordManagerBase64Decode(saltB64, saltVec) || saltVec.size() != 16 ||
        !passwordManagerBase64Decode(nonceB64, nonceVec) || nonceVec.size() != 12 ||
        !passwordManagerBase64Decode(tagB64, tagVec) || tagVec.size() != 16 ||
        !passwordManagerBase64Decode(dataB64, cipher)) {
        passwordManagerStatus = "VAULT B64 FAIL";
        return false;
    }

    uint8_t tempKey[32];
    memset(tempKey, 0, sizeof(tempKey));
    if (!passwordManagerDeriveKey(master, saltVec.data(), tempKey)) {
        memset(tempKey, 0, sizeof(tempKey));
        return false;
    }
    String json;
    if (!passwordManagerDecrypt(cipher, tempKey, nonceVec.data(), tagVec.data(), json)) {
        memset(tempKey, 0, sizeof(tempKey));
        passwordManagerStatus = "UNLOCK FAIL";
        return false;
    }
    if (!passwordManagerEntriesFromJson(json)) {
        memset(tempKey, 0, sizeof(tempKey));
        passwordEntries.clear();
        return false;
    }

    memcpy(passwordManagerKey, tempKey, sizeof(passwordManagerKey));
    memcpy(passwordManagerSalt, saltVec.data(), sizeof(passwordManagerSalt));
    memset(tempKey, 0, sizeof(tempKey));
    passwordManagerKeyReady = true;
    passwordManagerUnlocked = true;
    passwordManagerStatus = passwordEntries.empty() ? "VAULT EMPTY" : "UNLOCKED";
    passwordManagerMode = PM_MODE_LIST;
    passwordManagerFocus = 0;
    passwordManagerScrollOffset = 0;
    passwordManagerTouch();
    return true;
}

static bool passwordManagerCreateEmptyVault(String master) {
    if (!passwordManagerMountSd()) return false;
    passwordEntries.clear();
    passwordManagerRandomBytes(passwordManagerSalt, sizeof(passwordManagerSalt));
    if (!passwordManagerDeriveKey(master, passwordManagerSalt, passwordManagerKey)) {
        passwordManagerClearKey();
        return false;
    }
    passwordManagerKeyReady = true;
    passwordManagerUnlocked = true;
    bool ok = passwordManagerSaveVault();
    if (!ok) {
        passwordManagerClearVaultRam();
        return false;
    }
    passwordManagerStatus = "VAULT CREATED";
    passwordManagerMode = PM_MODE_LIST;
    passwordManagerTouch();
    return true;
}

static void passwordManagerEnsureVisible() {
    if (passwordManagerFocus < 0) passwordManagerFocus = 0;
    if (passwordManagerFocus >= (int)passwordEntries.size()) passwordManagerFocus = passwordEntries.empty() ? 0 : (int)passwordEntries.size() - 1;
    if (passwordManagerFocus < passwordManagerScrollOffset) passwordManagerScrollOffset = passwordManagerFocus;
    if (passwordManagerFocus >= passwordManagerScrollOffset + 4) passwordManagerScrollOffset = passwordManagerFocus - 3;
    if (passwordManagerScrollOffset < 0) passwordManagerScrollOffset = 0;
}

static bool passwordManagerBackPressed() {
    M5Cardputer.update();
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    if (status.del) return true;
    for (char c : status.word) {
        if (c == '`' || c == ',') return true;
    }
    return false;
}

static bool passwordManagerDelay(uint32_t ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        if (passwordManagerBackPressed()) {
            passwordManagerHidAbortFlag = true;
            return false;
        }
        delay(10);
    }
    return true;
}

static String passwordManagerHidPayloadName(int action) {
    if (action == PM_HID_ACCOUNT) return "TYPE ACCOUNT";
    if (action == PM_HID_PASSWORD) return "TYPE PASS";
    if (action == PM_HID_LOGIN) return "TYPE LOGIN";
    return "LOGIN+ENTER";
}

static void passwordManagerDrawRunStatus(String statusText) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_YELLOW);
    drawChippedButton(8, 7, 224, 120, CP_DIM);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("--- HID KEYBOARD ---", 120, 12);
    canvas.drawLine(14, 26, 226, 26, CP_YELLOW);
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString("AUTHORIZED HOSTS ONLY", 120, 38);
    canvas.setTextColor(CP_CYAN);
    if (passwordManagerSelectedIndex >= 0 && passwordManagerSelectedIndex < (int)passwordEntries.size()) {
        canvas.drawCenterString(passwordManagerShort(passwordEntries[passwordManagerSelectedIndex].service, 24), 120, 52);
    }
    canvas.setTextColor(WHITE);
    canvas.drawCenterString(statusText, 120, 68);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(passwordManagerHidPayloadName(passwordManagerPendingHidAction), 120, 84);
    pushCanvas();
}

static bool passwordManagerTypeSelected() {
#if BREACH_USB_HID_AVAILABLE
    if (passwordManagerSelectedIndex < 0 || passwordManagerSelectedIndex >= (int)passwordEntries.size()) {
        passwordManagerStatus = "NO ENTRY";
        return false;
    }
    if (!initBreachHidKeyboard()) {
        passwordManagerStatus = "HID START FAIL";
        return false;
    }
    PasswordEntry &entry = passwordEntries[passwordManagerSelectedIndex];
    badUsbKeyboard.releaseAll();
    if (passwordManagerPendingHidAction == PM_HID_ACCOUNT) {
        badUsbKeyboard.print(entry.account);
    } else if (passwordManagerPendingHidAction == PM_HID_PASSWORD) {
        badUsbKeyboard.print(entry.password);
    } else {
        badUsbKeyboard.print(entry.account);
        badUsbKeyboard.write(KEY_TAB);
        badUsbKeyboard.print(entry.password);
        if (passwordManagerPendingHidAction == PM_HID_LOGIN_ENTER) {
            badUsbKeyboard.write(KEY_RETURN);
        }
    }
    badUsbKeyboard.releaseAll();
    passwordManagerStatus = "TYPED";
    return true;
#else
    passwordManagerStatus = "BUILD NEEDS TINYUSB";
    return false;
#endif
}

static void passwordManagerRunHidPayload() {
    passwordManagerHidAbortFlag = false;
    passwordManagerMode = PM_MODE_HID_RUNNING;
    passwordManagerTypeSelected();
    passwordManagerMode = PM_MODE_DETAIL;
    passwordManagerTouch();
}

static void passwordManagerExitToHardware() {
    passwordManagerLock("LOCKED");
    appState = STATE_HARDWARE_MENU;
    hardwareMenuFocus = 10;
    currentHardwareScroll = 10;
    targetHardwareScroll = 10;
    drawHardwareMenu();
}

static void passwordManagerStartAdd() {
    passwordManagerClearEditEntry();
    passwordManagerEditingNew = true;
    passwordManagerEditField = 0;
    passwordManagerInput = "";
    passwordManagerStatus = "NEW ENTRY";
    passwordManagerMode = PM_MODE_EDIT;
}

static void passwordManagerStartEdit() {
    if (passwordManagerFocus < 0 || passwordManagerFocus >= (int)passwordEntries.size()) return;
    passwordManagerSelectedIndex = passwordManagerFocus;
    passwordManagerEditEntry = passwordEntries[passwordManagerSelectedIndex];
    passwordManagerEditingNew = false;
    passwordManagerEditField = 0;
    passwordManagerInput = passwordManagerEditEntry.service;
    passwordManagerStatus = "EDIT ENTRY";
    passwordManagerMode = PM_MODE_EDIT;
}

static bool passwordManagerCommitEdit() {
    passwordManagerEditEntry.service.trim();
    passwordManagerEditEntry.account.trim();
    passwordManagerEditEntry.password.trim();
    passwordManagerEditEntry.note.trim();
    if (passwordManagerEditEntry.service == "") {
        passwordManagerStatus = "SERVICE REQUIRED";
        passwordManagerEditField = 0;
        passwordManagerInput = passwordManagerEditEntry.service;
        return false;
    }
    if (passwordManagerEditEntry.password == "") {
        passwordManagerStatus = "PASS REQUIRED";
        passwordManagerEditField = 2;
        passwordManagerInput = passwordManagerEditEntry.password;
        return false;
    }
    if (passwordManagerEditingNew) {
        if ((int)passwordEntries.size() >= PM_MAX_ENTRIES) {
            passwordManagerStatus = "VAULT FULL";
            return false;
        }
        passwordEntries.push_back(passwordManagerEditEntry);
        passwordManagerFocus = passwordEntries.size() - 1;
    } else if (passwordManagerSelectedIndex >= 0 && passwordManagerSelectedIndex < (int)passwordEntries.size()) {
        passwordEntries[passwordManagerSelectedIndex] = passwordManagerEditEntry;
        passwordManagerFocus = passwordManagerSelectedIndex;
    }
    bool ok = passwordManagerSaveVault();
    passwordManagerClearEditEntry();
    passwordManagerMode = PM_MODE_LIST;
    passwordManagerEnsureVisible();
    passwordManagerTouch();
    return ok;
}

static void passwordManagerAdvanceEditField() {
    passwordManagerSetEditFieldValue(passwordManagerInput);
    if (passwordManagerEditField < 3) {
        passwordManagerEditField++;
        passwordManagerInput = passwordManagerEditFieldValue();
        return;
    }
    passwordManagerCommitEdit();
}

static void passwordManagerDrawPanelHeader(String title, uint16_t accent) {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, accent);
    drawChippedButton(8, 7, 224, 120, CP_DIM);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(title, 120, 12);
    canvas.drawLine(14, 26, 226, 26, accent);
}

static void passwordManagerDrawSecretInput(String title, String prompt, String value, String footer) {
    passwordManagerDrawPanelHeader(title, CP_CYAN);
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString(prompt, 120, 36);
    canvas.setTextColor(WHITE);
    drawChippedButton(22, 52, 196, 22, CP_YELLOW);
    canvas.setCursor(30, 59);
    canvas.print(passwordManagerShort(passwordManagerMasked(value), 26));
    canvas.setTextColor(passwordManagerStatus.indexOf("FAIL") >= 0 ? CP_RED : CP_DIM);
    canvas.drawCenterString(passwordManagerStatus, 120, 84);
    pushCanvas();
}

static void passwordManagerDrawList() {
    passwordManagerDrawPanelHeader("--- PASSWORD VAULT ---", CP_CYAN);
    canvas.setTextColor(passwordEntries.empty() ? CP_RED : CP_GREEN);
    canvas.drawCenterString(passwordManagerStatus, 120, 32);
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString("SD /Breach_OS/passwords.vault", 120, 44);

    if (passwordEntries.empty()) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("NO ENTRIES", 120, 66);
        canvas.drawCenterString("RIGHT ADD", 120, 82);
        pushCanvas();
        return;
    }

    passwordManagerEnsureVisible();
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(24, 53);
    canvas.print("SERVICE");
    String accountHeader = "ACCOUNT";
    canvas.setCursor(218 - canvas.textWidth(accountHeader), 53);
    canvas.print(accountHeader);

    int y = 64;
    for (int row = 0; row < 4; row++) {
        int idx = passwordManagerScrollOffset + row;
        if (idx >= (int)passwordEntries.size()) break;
        bool selected = (idx == passwordManagerFocus);
        uint16_t color = selected ? CP_YELLOW : CP_DIM;
        if (selected) drawChippedButton(18, y - 2, 204, 12, CP_YELLOW);
        String serviceText = passwordManagerFitText(passwordEntries[idx].service, 88);
        String accountText = passwordManagerFitText(passwordEntries[idx].account, 82);
        canvas.setTextColor(color);
        canvas.setCursor(24, y);
        canvas.print((selected ? "> " : "  ") + serviceText);
        int accountX = 218 - canvas.textWidth(accountText);
        if (accountX < 130) accountX = 130;
        canvas.setCursor(accountX, y);
        canvas.print(accountText);
        y += 12;
    }
    pushCanvas();
}

static void passwordManagerDrawDetail() {
    passwordManagerDrawPanelHeader("--- PASSWORD DETAIL ---", CP_YELLOW);
    if (passwordManagerSelectedIndex < 0 || passwordManagerSelectedIndex >= (int)passwordEntries.size()) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NO ENTRY", 120, 62);
        pushCanvas();
        return;
    }
    PasswordEntry &entry = passwordEntries[passwordManagerSelectedIndex];
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString(passwordManagerShort(entry.service, 28), 120, 32);
    canvas.setTextColor(WHITE);
    canvas.setCursor(16, 48);
    canvas.print("ACCT " + passwordManagerShort(entry.account, 25));
    canvas.setCursor(16, 60);
    String shownPassword = passwordManagerShowPassword ? passwordManagerShort(entry.password, 25) : passwordManagerMasked(entry.password);
    canvas.print("PASS " + shownPassword);
    canvas.setCursor(16, 72);
    canvas.print("NOTE " + passwordManagerShort(entry.note, 25));

    const char* actions[] = {"HID KEYBOARD", passwordManagerShowPassword ? "HIDE PASS" : "SHOW PASS", "EDIT", "DELETE", "BACK"};
    for (int i = 0; i < 5; i++) {
        int x = (i == 4) ? 72 : ((i % 2 == 0) ? 20 : 124);
        int y = (i == 4) ? 116 : ((i < 2) ? 86 : 101);
        bool selected = (i == passwordManagerActionFocus);
        if (selected) drawChippedButton(x - 4, y - 3, 96, 14, CP_YELLOW);
        canvas.setTextColor(selected ? CP_YELLOW : CP_DIM);
        canvas.setCursor(x, y);
        canvas.print(actions[i]);
    }
    pushCanvas();
}

static void passwordManagerDrawEdit() {
    passwordManagerDrawPanelHeader(passwordManagerEditingNew ? "--- NEW PASSWORD ---" : "--- EDIT PASSWORD ---", CP_CYAN);
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString(passwordManagerFieldName(passwordManagerEditField), 120, 34);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("FIELD " + String(passwordManagerEditField + 1) + "/4", 120, 46);
    drawChippedButton(12, 58, 216, 28, CP_YELLOW);
    canvas.setTextColor(WHITE);
    canvas.setCursor(20, 68);
    String shown = passwordManagerInput;
    if (passwordManagerEditField == 2) shown = passwordManagerMasked(shown);
    if (shown == "") shown = "_";
    canvas.print(passwordManagerShort(shown, 31));
    canvas.setTextColor(passwordManagerStatus.indexOf("REQUIRED") >= 0 ? CP_RED : CP_DIM);
    canvas.drawCenterString(passwordManagerStatus, 120, 96);
    pushCanvas();
}

static void passwordManagerDrawDeleteConfirm() {
    passwordManagerDrawPanelHeader("--- DELETE ENTRY ---", CP_RED);
    if (passwordManagerSelectedIndex >= 0 && passwordManagerSelectedIndex < (int)passwordEntries.size()) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString(passwordManagerShort(passwordEntries[passwordManagerSelectedIndex].service, 26), 120, 40);
    }
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString("DELETE THIS RECORD?", 120, 62);
    canvas.setTextColor(passwordManagerDeleteConfirm == 0 ? CP_YELLOW : CP_DIM);
    canvas.drawCenterString(passwordManagerDeleteConfirm == 0 ? "> NO" : "  NO", 80, 88);
    canvas.setTextColor(passwordManagerDeleteConfirm == 1 ? CP_YELLOW : CP_DIM);
    canvas.drawCenterString(passwordManagerDeleteConfirm == 1 ? "> YES" : "  YES", 160, 88);
    pushCanvas();
}

static void passwordManagerDrawHidPicker() {
    passwordManagerDrawPanelHeader("--- HID KEYBOARD ---", CP_YELLOW);
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString("AUTHORIZED HOSTS ONLY", 120, 34);
    if (passwordManagerSelectedIndex >= 0 && passwordManagerSelectedIndex < (int)passwordEntries.size()) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString(passwordManagerShort(passwordEntries[passwordManagerSelectedIndex].service, 26), 120, 47);
    }
    const char* payloads[] = {"TYPE ACCOUNT", "TYPE PASS", "TYPE LOGIN", "LOGIN+ENTER"};
    int y = 64;
    for (int i = 0; i < 4; i++) {
        bool selected = (i == passwordManagerHidPayloadFocus);
        if (selected) drawChippedButton(36, y - 2, 168, 13, CP_YELLOW);
        canvas.setTextColor(selected ? CP_YELLOW : CP_DIM);
        canvas.setCursor(46, y);
        canvas.print((selected ? "> " : "  ") + String(payloads[i]));
        y += 13;
    }
    pushCanvas();
}

static void passwordManagerDrawHidConfirm() {
    passwordManagerDrawPanelHeader("--- ARM HID PAYLOAD ---", CP_YELLOW);
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString("AUTHORIZED HOSTS ONLY", 120, 36);
    if (passwordManagerSelectedIndex >= 0 && passwordManagerSelectedIndex < (int)passwordEntries.size()) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString(passwordManagerShort(passwordEntries[passwordManagerSelectedIndex].service, 26), 120, 52);
    }
    canvas.setTextColor(WHITE);
    canvas.drawCenterString(passwordManagerHidPayloadName(passwordManagerPendingHidAction), 120, 70);
    pushCanvas();
}

void enterPasswordManager() {
    stopMp3Playback();
    passwordManagerClearVaultRam();
    passwordManagerMode = PM_MODE_UNLOCK;
    passwordManagerFocus = 0;
    passwordManagerScrollOffset = 0;
    passwordManagerActionFocus = 0;
    passwordManagerEditField = 0;
    passwordManagerSelectedIndex = 0;
    passwordManagerHidPayloadFocus = 0;
    passwordManagerPendingHidAction = 0;
    passwordManagerDeleteConfirm = 0;
    passwordManagerShowPassword = false;
    passwordManagerInput = "";
    passwordManagerInputConfirm = "";
    appState = STATE_PASSWORD_MANAGER;
    if (!passwordManagerMountSd()) {
        passwordManagerMode = PM_MODE_ERROR;
    } else if (SD.exists(PM_VAULT_FILE)) {
        passwordManagerStatus = "ENTER MASTER";
        passwordManagerMode = PM_MODE_UNLOCK;
    } else {
        passwordManagerStatus = "CREATE MASTER";
        passwordManagerMode = PM_MODE_CREATE;
    }
    drawPasswordManagerScreen();
}

void drawPasswordManagerScreen() {
    if (passwordManagerMode == PM_MODE_UNLOCK) {
        passwordManagerDrawSecretInput("--- PASSWORD VAULT ---", "MASTER PASSWORD", passwordManagerInput, "ENTER UNLOCK  ESC BACK");
        return;
    }
    if (passwordManagerMode == PM_MODE_CREATE) {
        passwordManagerDrawSecretInput("--- NEW VAULT ---", "NEW MASTER", passwordManagerInput, "ENTER NEXT  ESC BACK");
        return;
    }
    if (passwordManagerMode == PM_MODE_CONFIRM_MASTER) {
        passwordManagerDrawSecretInput("--- NEW VAULT ---", "CONFIRM MASTER", passwordManagerInputConfirm, "ENTER CREATE  ESC BACK");
        return;
    }
    if (passwordManagerMode == PM_MODE_LIST) {
        passwordManagerDrawList();
        return;
    }
    if (passwordManagerMode == PM_MODE_DETAIL) {
        passwordManagerDrawDetail();
        return;
    }
    if (passwordManagerMode == PM_MODE_EDIT) {
        passwordManagerDrawEdit();
        return;
    }
    if (passwordManagerMode == PM_MODE_DELETE_CONFIRM) {
        passwordManagerDrawDeleteConfirm();
        return;
    }
    if (passwordManagerMode == PM_MODE_HID_PICKER) {
        passwordManagerDrawHidPicker();
        return;
    }
    if (passwordManagerMode == PM_MODE_HID_CONFIRM) {
        passwordManagerDrawHidConfirm();
        return;
    }
    passwordManagerDrawPanelHeader("--- PASSWORD VAULT ---", CP_RED);
    canvas.setTextColor(CP_RED);
    canvas.drawCenterString(passwordManagerStatus, 120, 58);
    pushCanvas();
}

static void passwordManagerHandleTextInput(Keyboard_Class::KeysState status, String &target, int maxLen) {
    bool backspaceRequested = status.del;
    for (char c : status.word) {
        if (c == '\b' || c == 0x7f) backspaceRequested = true;
    }
    if (backspaceRequested && target.length() > 0) {
        target.remove(target.length() - 1);
        return;
    }
    for (char c : status.word) {
        if (c == '\b' || c == 0x7f) continue;
        if (c < 32 || c > 126) continue;
        if (c == ';' || c == '.' || c == ',' || c == '/' || c == '`') continue;
        if ((int)target.length() < maxLen) target += c;
    }
}

void handlePasswordManagerInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasBack = false;
    bool backspaceRequested = status.del;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') { hasLeft = true; hasBack = true; }
        if (c == '/') hasRight = true;
        if (c == '`') hasBack = true;
        if (c == '\b' || c == 0x7f) backspaceRequested = true;
    }

    if (passwordManagerMode == PM_MODE_ERROR) {
        if (hasBack || status.del) passwordManagerExitToHardware();
        return;
    }

    if (passwordManagerMode == PM_MODE_UNLOCK) {
        if (hasBack) {
            playSound(sound_select, sound_select_size);
            passwordManagerExitToHardware();
            return;
        }
        passwordManagerHandleTextInput(status, passwordManagerInput, 64);
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            String master = passwordManagerInput;
            passwordManagerInput = "";
            if (!passwordManagerLoadVault(master)) {
                passwordManagerInput = "";
            }
            master = "";
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_CREATE) {
        if (hasBack) {
            playSound(sound_select, sound_select_size);
            passwordManagerExitToHardware();
            return;
        }
        passwordManagerHandleTextInput(status, passwordManagerInput, 64);
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            if (passwordManagerInput.length() < 4) {
                passwordManagerStatus = "MASTER TOO SHORT";
            } else {
                passwordManagerInputConfirm = "";
                passwordManagerStatus = "CONFIRM MASTER";
                passwordManagerMode = PM_MODE_CONFIRM_MASTER;
            }
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_CONFIRM_MASTER) {
        if (hasBack) {
            playSound(sound_select, sound_select_size);
            passwordManagerInput = "";
            passwordManagerInputConfirm = "";
            passwordManagerStatus = "CREATE MASTER";
            passwordManagerMode = PM_MODE_CREATE;
            return;
        }
        passwordManagerHandleTextInput(status, passwordManagerInputConfirm, 64);
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            if (passwordManagerInput != passwordManagerInputConfirm) {
                passwordManagerInputConfirm = "";
                passwordManagerStatus = "MASTER MISMATCH";
            } else {
                String master = passwordManagerInput;
                passwordManagerInput = "";
                passwordManagerInputConfirm = "";
                passwordManagerCreateEmptyVault(master);
                master = "";
            }
        }
        return;
    }

    if (!passwordManagerUnlocked && passwordManagerMode != PM_MODE_CREATE && passwordManagerMode != PM_MODE_CONFIRM_MASTER) {
        passwordManagerMode = PM_MODE_UNLOCK;
        passwordManagerStatus = "LOCKED";
        return;
    }
    passwordManagerTouch();

    if (passwordManagerMode == PM_MODE_LIST) {
        if (hasBack || status.del) {
            playSound(sound_select, sound_select_size);
            passwordManagerExitToHardware();
            return;
        }
        if (hasUp && !passwordEntries.empty()) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerFocus--;
            if (passwordManagerFocus < 0) passwordManagerFocus = passwordEntries.size() - 1;
            passwordManagerEnsureVisible();
        }
        if (hasDown && !passwordEntries.empty()) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerFocus++;
            if (passwordManagerFocus >= (int)passwordEntries.size()) passwordManagerFocus = 0;
            passwordManagerEnsureVisible();
        }
        if (hasRight) {
            playSound(sound_select, sound_select_size);
            passwordManagerStartAdd();
            return;
        }
        if (status.enter && !passwordEntries.empty()) {
            playSound(sound_select, sound_select_size);
            passwordManagerSelectedIndex = passwordManagerFocus;
            passwordManagerActionFocus = 0;
            passwordManagerShowPassword = false;
            passwordManagerMode = PM_MODE_DETAIL;
        } else if (status.enter && passwordEntries.empty()) {
            playSound(sound_select, sound_select_size);
            passwordManagerStartAdd();
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_DETAIL) {
        if (hasBack || status.del) {
            playSound(sound_select, sound_select_size);
            passwordManagerShowPassword = false;
            passwordManagerMode = PM_MODE_LIST;
            return;
        }
        if (hasUp || hasLeft) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerActionFocus--;
            if (passwordManagerActionFocus < 0) passwordManagerActionFocus = 4;
        }
        if (hasDown || hasRight) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerActionFocus++;
            if (passwordManagerActionFocus > 4) passwordManagerActionFocus = 0;
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            if (passwordManagerActionFocus == 0) {
                passwordManagerShowPassword = false;
                passwordManagerHidPayloadFocus = 0;
                passwordManagerMode = PM_MODE_HID_PICKER;
            } else if (passwordManagerActionFocus == 1) {
                passwordManagerShowPassword = !passwordManagerShowPassword;
            } else if (passwordManagerActionFocus == 2) {
                passwordManagerShowPassword = false;
                passwordManagerStartEdit();
            } else if (passwordManagerActionFocus == 3) {
                passwordManagerShowPassword = false;
                passwordManagerDeleteConfirm = 0;
                passwordManagerMode = PM_MODE_DELETE_CONFIRM;
            } else {
                passwordManagerShowPassword = false;
                passwordManagerMode = PM_MODE_LIST;
            }
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_EDIT) {
        if (hasBack) {
            playSound(sound_select, sound_select_size);
            passwordManagerClearEditEntry();
            passwordManagerMode = passwordEntries.empty() ? PM_MODE_LIST : PM_MODE_DETAIL;
            return;
        }
        if (backspaceRequested) {
            if (passwordManagerInput.length() > 0) passwordManagerInput.remove(passwordManagerInput.length() - 1);
            return;
        }
        for (char c : status.word) {
            if (c == '\b' || c == 0x7f) continue;
            if (c < 32 || c > 126) continue;
            if (c == ';' || c == '.' || c == ',' || c == '/' || c == '`') continue;
            if ((int)passwordManagerInput.length() < passwordManagerFieldLimit(passwordManagerEditField)) passwordManagerInput += c;
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            passwordManagerAdvanceEditField();
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_DELETE_CONFIRM) {
        if (hasBack || status.del) {
            playSound(sound_select, sound_select_size);
            passwordManagerMode = PM_MODE_DETAIL;
            return;
        }
        if (hasLeft || hasRight || hasUp || hasDown) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerDeleteConfirm = 1 - passwordManagerDeleteConfirm;
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            if (passwordManagerDeleteConfirm == 1 && passwordManagerSelectedIndex >= 0 && passwordManagerSelectedIndex < (int)passwordEntries.size()) {
                passwordManagerClearStoredEntry(passwordManagerSelectedIndex);
                passwordEntries.erase(passwordEntries.begin() + passwordManagerSelectedIndex);
                if (passwordManagerFocus >= (int)passwordEntries.size()) passwordManagerFocus = passwordEntries.empty() ? 0 : passwordEntries.size() - 1;
                passwordManagerSaveVault();
                passwordManagerMode = PM_MODE_LIST;
            } else {
                passwordManagerMode = PM_MODE_DETAIL;
            }
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_HID_PICKER) {
        if (hasBack || status.del) {
            playSound(sound_select, sound_select_size);
            passwordManagerMode = PM_MODE_DETAIL;
            return;
        }
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerHidPayloadFocus--;
            if (passwordManagerHidPayloadFocus < 0) passwordManagerHidPayloadFocus = 3;
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            passwordManagerHidPayloadFocus++;
            if (passwordManagerHidPayloadFocus > 3) passwordManagerHidPayloadFocus = 0;
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            passwordManagerPendingHidAction = passwordManagerHidPayloadFocus;
            passwordManagerTypeSelected();
            passwordManagerMode = PM_MODE_DETAIL;
            passwordManagerTouch();
        }
        return;
    }

    if (passwordManagerMode == PM_MODE_HID_CONFIRM) {
        if (hasBack || status.del) {
            playSound(sound_select, sound_select_size);
            passwordManagerStatus = "ABORTED";
            passwordManagerMode = PM_MODE_DETAIL;
            return;
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            passwordManagerRunHidPayload();
        }
        return;
    }
}

bool updatePasswordManagerUi() {
    if (passwordManagerUnlocked && passwordManagerMode != PM_MODE_HID_RUNNING && millis() - passwordManagerLastActivityMs > PM_UNLOCK_TIMEOUT_MS) {
        passwordManagerLock("LOCKED TIMEOUT");
        return true;
    }
    return false;
}
