// HARDWARE NODE / IR: M5 IR Unit on the Cardputer Grove port.
// Default Grove mapping for Cardputer is RX=G1 and TX=G2. The screen includes
// a pin-swap action for Grove modules/cables that expose the IR lines reversed.

static constexpr int IR_ACTION_COUNT = 4;
static constexpr int IR_CAPTURE_ACTION_COUNT = 2;
static constexpr uint8_t IR_INTERNAL_TX_PIN = 44;
static constexpr int IR_SD_CS = 12;
static constexpr uint32_t IR_SD_SPI_HZ = 20000000;
static constexpr const char* IR_OS_DIR = "/Breach_OS";
static constexpr const char* IR_FILE_DIR = "/Breach_OS/ir";
static const char* IR_ACTION_LABELS[IR_ACTION_COUNT] = {
    "NEW IR",
    "SEND IR",
    "SWAP G1/G2",
    "BACK"
};
static const char* IR_CAPTURE_LABELS[IR_CAPTURE_ACTION_COUNT] = {
    "SAVE",
    "DISCARD"
};

static bool irEnsureFileDir();

static String irShort(String value, int maxLen) {
    if ((int)value.length() <= maxLen) return value;
    if (maxLen <= 3) return value.substring(0, maxLen);
    return value.substring(0, maxLen - 3) + "...";
}

static String irHex64(unsigned long long value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "0x%llX", value);
    return String(buf);
}

static String irHex16(uint16_t value) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%04X", value);
    return String(buf);
}

static void irDrawListIndicator(float scroll, int total, int x, int y, int w) {
    const int railH = 6;
    const int railR = railH / 2;
    const uint16_t railColor = CP_CYAN;
    const uint16_t knobColor = CP_YELLOW;
    int leftX = x + railR;
    int rightX = x + w - railR;
    int midY = y + railR;

    canvas.drawLine(leftX, y, rightX, y, railColor);
    canvas.drawLine(leftX, y + railH, rightX, y + railH, railColor);
    canvas.drawLine(leftX - 1, y, leftX, y, railColor);
    canvas.drawLine(leftX - 2, y + 1, leftX - 2, y + 1, railColor);
    canvas.drawLine(leftX - railR, midY - 1, leftX - railR, midY + 1, railColor);
    canvas.drawLine(leftX - 2, y + railH - 1, leftX - 2, y + railH - 1, railColor);
    canvas.drawLine(leftX - 1, y + railH, leftX, y + railH, railColor);
    canvas.drawLine(rightX, y, rightX + 1, y, railColor);
    canvas.drawLine(rightX + 2, y + 1, rightX + 2, y + 1, railColor);
    canvas.drawLine(rightX + railR, midY - 1, rightX + railR, midY + 1, railColor);
    canvas.drawLine(rightX + 2, y + railH - 1, rightX + 2, y + railH - 1, railColor);
    canvas.drawLine(rightX, y + railH, rightX + 1, y + railH, railColor);

    if (total <= 1) return;

    float wrapped = fmod(scroll, (float)total);
    if (wrapped < 0.0) wrapped += (float)total;
    float lastIndex = (float)(total - 1);
    float pos = wrapped;
    if (wrapped > lastIndex) pos = lastIndex * ((float)total - wrapped);
    float ratio = pos / lastIndex;
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;

    int dotX = leftX + (int)((rightX - leftX) * ratio);
    canvas.fillCircle(dotX, midY, 5, knobColor);
}

static bool irLerpScroll(float &current, float target) {
    if (fabs(current - target) <= 0.01) {
        current = target;
        return false;
    }
    current += (target - current) * 0.35;
    if (fabs(current - target) <= 0.01) current = target;
    return true;
}

static int irButtonShift(float current, float target) {
    float delta = target - current;
    if (delta > 1.0) delta = 1.0;
    if (delta < -1.0) delta = -1.0;
    return (int)(delta * 12.0);
}

bool updateIrUiAnimation() {
    bool needsRedraw = false;
    needsRedraw |= irLerpScroll(currentIrActionScroll, targetIrActionScroll);
    needsRedraw |= irLerpScroll(currentIrFileScroll, targetIrFileScroll);
    needsRedraw |= irLerpScroll(currentIrConfirmScroll, targetIrConfirmScroll);
    return needsRedraw;
}

static String irBaseName(String path) {
    int slash = path.lastIndexOf('/');
    if (slash >= 0) return path.substring(slash + 1);
    return path;
}

static String irCleanName(String value, bool fallback = true) {
    value.trim();
    value.toLowerCase();
    String out = "";
    for (size_t i = 0; i < value.length() && out.length() < 24; i++) {
        char c = value[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (c == '_' || c == '-') out += c;
        else if (c >= 32 && c <= 126) out += '_';
    }
    while (out.indexOf("__") >= 0) out.replace("__", "_");
    while (out.endsWith("_")) out.remove(out.length() - 1);
    while (out.startsWith("_")) out.remove(0, 1);
    if (out == "" && fallback) out = "ir";
    return out;
}

static bool irParseDefaultStemNumber(String stem, uint32_t &number) {
    stem.trim();
    stem.toLowerCase();
    if (!stem.startsWith("ir_") || stem.length() <= 3) return false;

    uint32_t parsed = 0;
    for (int i = 3; i < (int)stem.length(); i++) {
        char c = stem.charAt(i);
        if (c < '0' || c > '9') return false;
        parsed = (parsed * 10) + (uint32_t)(c - '0');
    }
    if (parsed == 0) return false;
    number = parsed;
    return true;
}

static String irDefaultFileStem() {
    if (!irEnsureFileDir()) return "";

    uint32_t highestNumber = 0;
    File dir = SD.open(IR_FILE_DIR);
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = irBaseName(String(file.name()));
                name.toLowerCase();
                if (name.endsWith(".ir")) {
                    String stem = name.substring(0, name.length() - 3);
                    uint32_t number = 0;
                    if (irParseDefaultStemNumber(stem, number) && number > highestNumber) {
                        highestNumber = number;
                    }
                }
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    } else if (dir) {
        dir.close();
    }

    return "ir_" + String(highestNumber + 1);
}

static unsigned long irParseUnsigned(String value) {
    value.trim();
    return strtoul(value.c_str(), nullptr, 0);
}

static bool irIsReplayableProtocol(int protocol) {
    return protocol == NEC || protocol == NEC2 || protocol == ONKYO || protocol == APPLE ||
           protocol == SAMSUNG || protocol == SAMSUNGLG || protocol == SAMSUNG48 ||
           protocol == LG || protocol == SONY || protocol == RC5 || protocol == RC6;
}

static bool irHasDecodedTxFrame() {
    return irHasLastCode && irIsReplayableProtocol(irLastProtocol);
}

static bool irHasRawTxFrame() {
    return !(irLastFlags & IRDATA_FLAGS_WAS_OVERFLOW) && irLastRawMicros.size() >= 2;
}

static bool irHasTxFrame() {
    return irHasDecodedTxFrame() || irHasRawTxFrame();
}

static void irStoreRawMicros() {
    irLastRawMicros.clear();
    if (irLastFlags & IRDATA_FLAGS_WAS_OVERFLOW) return;

    uint_fast16_t rawLen = IrReceiver.decodedIRData.rawlen;
    for (uint_fast16_t i = 1; i < rawLen; i++) {
        uint32_t duration = (uint32_t)IrReceiver.irparams.rawbuf[i] * MICROS_PER_TICK;
        if (i & 1) {
            if (duration > MARK_EXCESS_MICROS) duration -= MARK_EXCESS_MICROS;
        } else {
            duration += MARK_EXCESS_MICROS;
        }
        if (duration < MICROS_PER_TICK) duration = MICROS_PER_TICK;
        if (duration > 65535) duration = 65535;
        irLastRawMicros.push_back((uint16_t)duration);
    }
}

static String irRawMicrosLine(const std::vector<uint16_t> &rawMicros) {
    String line = "";
    for (size_t i = 0; i < rawMicros.size(); i++) {
        if (i) line += ',';
        line += String((uint32_t)rawMicros[i]);
    }
    return line;
}

static void irParseRawMicros(String value, std::vector<uint16_t> &rawMicros) {
    rawMicros.clear();
    int start = 0;
    while (start < (int)value.length()) {
        int comma = value.indexOf(',', start);
        String part = comma >= 0 ? value.substring(start, comma) : value.substring(start);
        part.trim();
        if (part.length()) {
            unsigned long duration = strtoul(part.c_str(), nullptr, 0);
            if (duration > 65535) duration = 65535;
            if (duration > 0) rawMicros.push_back((uint16_t)duration);
        }
        if (comma < 0) break;
        start = comma + 1;
    }
}

static bool irMountSd() {
    SPI.begin(40, 39, 14, IR_SD_CS);
    if (!SD.begin(IR_SD_CS, SPI, IR_SD_SPI_HZ) || SD.cardType() == CARD_NONE) {
        irStatus = "SD MOUNT FAIL";
        return false;
    }
    return true;
}

static bool irEnsureDir(const char* path) {
    File dir = SD.open(path);
    if (dir) {
        bool ok = dir.isDirectory();
        dir.close();
        return ok;
    }
    return SD.mkdir(path);
}

static bool irEnsureFileDir() {
    if (!irMountSd()) return false;
    if (!irEnsureDir(IR_OS_DIR) || !irEnsureDir(IR_FILE_DIR)) {
        irStatus = "IR DIR FAIL";
        return false;
    }
    return true;
}

static String irNextFilePath(String requestedName = "") {
    String stem = irCleanName(requestedName.length() ? requestedName : irDefaultFileStem());
    if (!stem.length()) stem = irDefaultFileStem();
    if (!stem.length()) stem = "ir_1";

    char name[96];
    snprintf(name, sizeof(name), "%s/%s.ir", IR_FILE_DIR, stem.c_str());
    if (!SD.exists(name)) return String(name);

    uint32_t defaultNumber = 0;
    if (irParseDefaultStemNumber(stem, defaultNumber)) {
        for (uint32_t i = defaultNumber + 1; i < defaultNumber + 1000; i++) {
            String numberedStem = "ir_" + String(i);
            snprintf(name, sizeof(name), "%s/%s.ir", IR_FILE_DIR, numberedStem.c_str());
            if (!SD.exists(name)) return String(name);
        }
    }

    for (uint16_t i = 1; i < 1000; i++) {
        snprintf(name, sizeof(name), "%s/%s_%u.ir", IR_FILE_DIR, stem.c_str(), i);
        if (!SD.exists(name)) return String(name);
    }
    snprintf(name, sizeof(name), "%s/%s_%lu.ir", IR_FILE_DIR, stem.c_str(), (unsigned long)millis());
    return String(name);
}

static bool irSaveLastCode(String requestedName = "") {
    if (!irHasTxFrame()) {
        irStatus = "NO TXABLE IR";
        return false;
    }
    if (!irEnsureFileDir()) return false;

    String path = irNextFilePath(requestedName);
    SD.remove(path.c_str());
    File file = SD.open(path.c_str(), FILE_WRITE);
    if (!file) {
        irStatus = "IR SAVE FAIL";
        return false;
    }

    file.println("BREACH_IR_V1");
    file.println("name=" + irBaseName(path));
    file.println("protocol=" + String((int)irLastProtocol));
    file.println("protocolName=" + irLastProtocolName);
    file.println("address=" + String((uint32_t)irLastAddress));
    file.println("command=" + String((uint32_t)irLastCommand));
    file.println("bits=" + String((uint32_t)irLastBits));
    file.println("raw=" + irHex64((unsigned long long)irLastRaw));
    file.println("flags=" + String((uint32_t)irLastFlags));
    file.println("frequency=38");
    file.println("rawCount=" + String((uint32_t)irLastRawMicros.size()));
    if (!irLastRawMicros.empty()) file.println("rawMicros=" + irRawMicrosLine(irLastRawMicros));
    file.close();

    irFileSaveCount++;
    irLastActionMs = millis();
    irStatus = "SAVED " + irShort(irBaseName(path), 17);
    return true;
}

static bool irPopulateSavedFiles() {
    irSavedFiles.clear();
    if (!irEnsureFileDir()) return false;

    File dir = SD.open(IR_FILE_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        irStatus = "IR DIR FAIL";
        return false;
    }

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = irBaseName(String(file.name()));
            String lower = name;
            lower.toLowerCase();
            if (lower.endsWith(".ir")) irSavedFiles.push_back(name);
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();

    std::sort(irSavedFiles.begin(), irSavedFiles.end(), [](const String &a, const String &b) {
        return a < b;
    });
    if (irFileFocus >= (int)irSavedFiles.size()) irFileFocus = (int)irSavedFiles.size() - 1;
    if (irFileFocus < 0) irFileFocus = 0;
    irStatus = irSavedFiles.empty() ? String("NO IR FILES") : String("IR FILES ") + String((uint32_t)irSavedFiles.size());
    return true;
}

static bool irReadStoredCode(String path, int &protocol, uint16_t &address, uint16_t &command,
                             uint16_t &bits, uint8_t &flags, String &protocolName,
                             uint8_t &frequency, std::vector<uint16_t> &rawMicros) {
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) return false;

    bool valid = false;
    bool hasProtocol = false;
    bool hasAddress = false;
    bool hasCommand = false;
    protocol = UNKNOWN;
    address = 0;
    command = 0;
    bits = 0;
    flags = 0;
    frequency = 38;
    rawMicros.clear();
    protocolName = "UNKNOWN";

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line == "BREACH_IR_V1") {
            valid = true;
        } else if (line.startsWith("protocol=")) {
            protocol = (int)irParseUnsigned(line.substring(9));
            hasProtocol = true;
        } else if (line.startsWith("protocolName=")) {
            protocolName = line.substring(13);
            protocolName.trim();
        } else if (line.startsWith("address=")) {
            address = (uint16_t)irParseUnsigned(line.substring(8));
            hasAddress = true;
        } else if (line.startsWith("command=")) {
            command = (uint16_t)irParseUnsigned(line.substring(8));
            hasCommand = true;
        } else if (line.startsWith("bits=")) {
            bits = (uint16_t)irParseUnsigned(line.substring(5));
        } else if (line.startsWith("flags=")) {
            flags = (uint8_t)irParseUnsigned(line.substring(6));
        } else if (line.startsWith("frequency=")) {
            unsigned long parsed = irParseUnsigned(line.substring(10));
            if (parsed >= 30 && parsed <= 60) frequency = (uint8_t)parsed;
        } else if (line.startsWith("rawMicros=")) {
            irParseRawMicros(line.substring(10), rawMicros);
        }
    }
    file.close();
    return valid && hasProtocol && hasAddress && hasCommand;
}

static bool irSendDecodedOrRawOnPin(uint8_t sendPin, bool canSendDecoded, int protocol, uint16_t address,
                                    uint16_t command, uint8_t frequency, const std::vector<uint16_t> &rawMicros) {
    pinMode(sendPin, OUTPUT);
    digitalWrite(sendPin, LOW);
    IrSender.setSendPin(sendPin);
    delay(4);
    if (canSendDecoded) {
        return IrSender.write((decode_type_t)protocol, address, command, 0) > 0;
    }
    IrSender.sendRaw(rawMicros.data(), rawMicros.size(), frequency);
    return true;
}

static bool irSendBothOutputs(bool canSendDecoded, int protocol, uint16_t address, uint16_t command,
                              uint8_t frequency, const std::vector<uint16_t> &rawMicros) {
    bool sentGrove = irSendDecodedOrRawOnPin(irTxPin, canSendDecoded, protocol, address, command, frequency, rawMicros);
    delay(35);
    bool sentInternal = true;
    if (IR_INTERNAL_TX_PIN != irTxPin) {
        sentInternal = irSendDecodedOrRawOnPin(IR_INTERNAL_TX_PIN, canSendDecoded, protocol, address, command, frequency, rawMicros);
        delay(35);
    }
    IrSender.setSendPin(irTxPin);
    return sentGrove && sentInternal;
}

static bool irSendStoredFile(String name) {
    if (!irEnsureFileDir()) return false;

    int protocol = UNKNOWN;
    uint16_t address = 0;
    uint16_t command = 0;
    uint16_t bits = 0;
    uint8_t flags = 0;
    uint8_t frequency = 38;
    std::vector<uint16_t> rawMicros;
    String protocolName = "UNKNOWN";
    String path = String(IR_FILE_DIR) + "/" + name;

    if (!irReadStoredCode(path, protocol, address, command, bits, flags, protocolName, frequency, rawMicros)) {
        irStatus = "BAD IR FILE";
        return false;
    }

    bool canSendDecoded = irIsReplayableProtocol(protocol);
    bool canSendRaw = rawMicros.size() >= 2;
    if (!canSendDecoded && !canSendRaw) {
        irStatus = "FILE NOT TXABLE";
        return false;
    }

    IrReceiver.end();
    delay(8);
    bool sent = irSendBothOutputs(canSendDecoded, protocol, address, command, frequency, rawMicros);
    delay(20);
    irResumeReceiver();

    if (sent) {
        irSendCount++;
        irLastActionMs = millis();
        irStatus = "TX BOTH " + irShort(name, 15);
        return true;
    }

    irStatus = "TX FILE FAIL";
    return false;
}

static void irConfigurePins() {
    if (irReady) {
        IrReceiver.end();
        delay(5);
    }

    irRxPin = irPinsSwapped ? G2 : G1;
    irTxPin = irPinsSwapped ? G1 : G2;
    pinMode(irRxPin, INPUT_PULLUP);
    pinMode(irTxPin, OUTPUT);
    digitalWrite(irTxPin, LOW);
    pinMode(IR_INTERNAL_TX_PIN, OUTPUT);
    digitalWrite(IR_INTERNAL_TX_PIN, LOW);

    IrSender.begin(irTxPin);
    IrReceiver.begin(irRxPin, false);
    irReady = true;
    irStatus = "LISTENING";
}

static void irStoreDecodedResult() {
    irLastProtocol = IrReceiver.decodedIRData.protocol;
    irLastProtocolName = String(getProtocolString(irLastProtocol));
    irLastAddress = IrReceiver.decodedIRData.address;
    irLastCommand = IrReceiver.decodedIRData.command;
    irLastBits = IrReceiver.decodedIRData.numberOfBits;
    irLastRaw = IrReceiver.decodedIRData.decodedRawData;
    irLastFlags = IrReceiver.decodedIRData.flags;
    irHasLastCode = (irLastProtocol != UNKNOWN) && !(irLastFlags & IRDATA_FLAGS_WAS_OVERFLOW);
    irStoreRawMicros();

    if (irLastFlags & IRDATA_FLAGS_WAS_OVERFLOW) {
        irOverflowCount++;
        irLastOverflowMs = millis();
        irStatus = "RX OVERFLOW BUF 750";
    } else if (irLastProtocol == UNKNOWN) {
        irReceiveCount++;
        irLastReceiveMs = millis();
        irStatus = irHasRawTxFrame() ? "RAW LEARNED" : "UNKNOWN RAW SEEN";
    } else if (irLastFlags & IRDATA_FLAGS_IS_REPEAT) {
        irReceiveCount++;
        irLastReceiveMs = millis();
        irStatus = "REPEAT " + irLastProtocolName;
    } else {
        irReceiveCount++;
        irLastReceiveMs = millis();
        irStatus = "LEARNED " + irLastProtocolName;
    }
}

static void irResumeReceiver() {
    IrReceiver.begin(irRxPin, false);
    irReady = true;
}

bool pollIrReceiver() {
    if (appState != STATE_IR || irFileMode || irConfirmMode || irNameMode) return false;
    if (!irReady) irConfigurePins();

    if (IrReceiver.decode()) {
        irStoreDecodedResult();
        IrReceiver.resume();
        if (irAutoSaveNext) {
            if (irHasTxFrame()) {
                irAutoSaveNext = false;
                irConfirmMode = true;
                irConfirmFocus = 0;
                currentIrConfirmScroll = 0;
                targetIrConfirmScroll = 0;
                irStatus = "SAVE OR DISCARD";
            } else if (irLastFlags & IRDATA_FLAGS_WAS_OVERFLOW) {
                irStatus = "OVERFLOW TRY AGAIN";
            } else if (irLastProtocol == UNKNOWN) {
                irStatus = "NO RAW TRY AGAIN";
            } else {
                irStatus = "NO RAW TRY AGAIN";
            }
        }
        return true;
    }
    return false;
}

void enterIrMode() {
    stopMp3Playback();
    appState = STATE_IR;
    irFocus = 0;
    irFileFocus = 0;
    irConfirmFocus = 0;
    currentIrActionScroll = 0;
    targetIrActionScroll = 0;
    currentIrFileScroll = 0;
    targetIrFileScroll = 0;
    currentIrConfirmScroll = 0;
    targetIrConfirmScroll = 0;
    irFileMode = false;
    irAutoSaveNext = false;
    irConfirmMode = false;
    irNameMode = false;
    irPendingFileName = "";
    irNameCursor = 0;
    irStatus = "STARTING IR";
    irConfigurePins();
    drawIrScreen();
}

void drawIrScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_YELLOW);
    drawChippedButton(8, 7, 224, 120, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    String title = "--- GROVE IR TX/RX ---";
    if (irFileMode) title = "--- SEND IR ---";
    else if (irNameMode) title = "--- NAME IR ---";
    else if (irConfirmMode) title = "--- SAVE IR? ---";
    canvas.drawCenterString(title, 120, 12);
    canvas.drawLine(14, 26, 226, 26, CP_YELLOW);

    canvas.setTextColor(irStatus.startsWith("RX") || irStatus.endsWith("FAIL") || irStatus.endsWith("FAILED") ? CP_RED : CP_CYAN);
    canvas.drawCenterString(irShort(irStatus, 27), 120, 31);

    if (irFileMode) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("SD /Breach_OS/ir", 120, 42);
        canvas.drawCenterString("FILES " + String((uint32_t)irSavedFiles.size()), 120, 54);

        int buttonW = 195;
        int buttonX = (240 - buttonW) / 2;
        int buttonY = 72;
        int buttonH = 30;
        int buttonShift = irSavedFiles.empty() ? 0 : irButtonShift(currentIrFileScroll, targetIrFileScroll);
        if (irSavedFiles.empty()) {
            drawChippedButton(buttonX, buttonY, buttonW, buttonH, CP_RED);
            canvas.setTextColor(CP_RED);
            canvas.setTextSize(2);
            canvas.drawCenterString("NO IR FILES", 120, buttonY + 8);
            canvas.setTextSize(1);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString("USE NEW IR FIRST", 120, 107);
        } else {
            drawChippedButton(buttonX + buttonShift, buttonY, buttonW, buttonH, CP_YELLOW);
            canvas.setTextColor(CP_YELLOW);
            canvas.setTextSize(1);
            canvas.drawCenterString(irShort(irSavedFiles[irFileFocus], 30), 120 + buttonShift, buttonY + 11);
            canvas.setTextSize(1);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString(String(irFileFocus + 1) + "/" + String((uint32_t)irSavedFiles.size()), 120, 107);
        }

        irDrawListIndicator(irSavedFiles.empty() ? 0.0 : currentIrFileScroll, irSavedFiles.empty() ? 1 : (int)irSavedFiles.size(), 45, 114, 150);
        pushCanvas();
        return;
    }

    if (irNameMode) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("CAPTURED " + irShort(irLastProtocolName, 13), 120, 42);
        canvas.drawCenterString("NAME FILE THEN ENTER", 120, 54);

        int buttonW = 195;
        int buttonX = (240 - buttonW) / 2;
        int buttonY = 68;
        int buttonH = 35;
        drawChippedButton(buttonX, buttonY, buttonW, buttonH, CP_YELLOW);
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        String shownName = irPendingFileName;
        if (blinkState) {
            int cursor = irNameCursor;
            if (cursor < 0) cursor = 0;
            if (cursor > (int)shownName.length()) cursor = shownName.length();
            shownName = shownName.substring(0, cursor) + "_" + shownName.substring(cursor);
        }
        canvas.drawCenterString(irShort(shownName, 28), 120, buttonY + 13);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("SAVES TO /Breach_OS/ir", 120, 108);
        pushCanvas();
        return;
    }

    if (irConfirmMode) {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("CAPTURED " + irShort(irLastProtocolName, 13), 120, 42);
        canvas.drawCenterString("A " + irHex16(irLastAddress) + "  C " + irHex16(irLastCommand), 120, 54);
        canvas.drawCenterString("SAVE TO SD /Breach_OS/ir", 120, 66);

        int buttonW = 195;
        int buttonX = (240 - buttonW) / 2;
        int buttonY = 76;
        int buttonH = 30;
        int buttonShift = irButtonShift(currentIrConfirmScroll, targetIrConfirmScroll);
        uint16_t buttonColor = irConfirmFocus == 0 ? CP_GREEN : CP_RED;
        drawChippedButton(buttonX + buttonShift, buttonY, buttonW, buttonH, buttonColor);
        canvas.setTextColor(buttonColor);
        canvas.setTextSize(2);
        canvas.drawCenterString(IR_CAPTURE_LABELS[irConfirmFocus], 120 + buttonShift, buttonY + 8);

        irDrawListIndicator(currentIrConfirmScroll, IR_CAPTURE_ACTION_COUNT, 45, 114, 150);
        pushCanvas();
        return;
    }

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(18, 42);
    canvas.print("RX G" + String((int)irRxPin) + "  TX G" + String((int)irTxPin));
    canvas.setCursor(118, 42);
    canvas.print("IN " + String((uint32_t)irReceiveCount) + " OV " + String((uint32_t)irOverflowCount));

    canvas.setTextColor(irHasTxFrame() ? CP_GREEN : CP_DIM);
    canvas.setCursor(18, 54);
    canvas.print("LAST " + irShort(irLastProtocolName, 9));
    canvas.setCursor(118, 54);
    canvas.print("BITS " + String((uint32_t)irLastBits));
    canvas.setCursor(18, 66);
    canvas.print("A " + irHex16(irLastAddress));
    canvas.setCursor(118, 66);
    canvas.print("C " + irHex16(irLastCommand));

    int buttonW = 195;
    int buttonX = (240 - buttonW) / 2;
    int buttonY = 76;
    int buttonH = 30;
    int buttonShift = irButtonShift(currentIrActionScroll, targetIrActionScroll);
    drawChippedButton(buttonX + buttonShift, buttonY, buttonW, buttonH, CP_YELLOW);
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(2);
    canvas.drawCenterString(IR_ACTION_LABELS[irFocus], 120 + buttonShift, buttonY + 8);

    irDrawListIndicator(currentIrActionScroll, IR_ACTION_COUNT, 45, 114, 150);
    pushCanvas();
}

void handleIrInput(Keyboard_Class::KeysState status) {
    bool hasLeft = false, hasRight = false, hasBackKey = false, hasBack = status.del;
    for (char c : status.word) {
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c == '`') {
            hasBackKey = true;
            hasBack = true;
        }
    }

    if (irNameMode) {
        if (hasBackKey) {
            playSound(sound_select, sound_select_size);
            irNameMode = false;
            irConfirmMode = true;
            irStatus = "SAVE OR DISCARD";
            drawIrScreen();
            return;
        }
        if (status.enter) {
            playSound(sound_select, sound_select_size);
            irPendingFileName = irCleanName(irPendingFileName, false);
            if (!irPendingFileName.length()) {
                irStatus = "NAME REQUIRED";
                drawIrScreen();
                return;
            }
            irNameMode = false;
            irConfirmMode = false;
            irAutoSaveNext = false;
            irSaveLastCode(irPendingFileName);
            drawIrScreen();
            return;
        }

        bool changed = false;
        if (status.del && irPendingFileName.length() > 0) {
            irPendingFileName.remove(irPendingFileName.length() - 1);
            changed = true;
        }
        for (char c : status.word) {
            if (c == '`' || c == '\b' || c == 0x7f) continue;
            char out = 0;
            if (c >= 'A' && c <= 'Z') out = c + 32;
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out = c;
            else if (c == ' ' || c == '.') out = '_';
            if (out && irPendingFileName.length() < 24) {
                irPendingFileName += out;
                changed = true;
            }
        }
        if (changed) {
            irPendingFileName = irCleanName(irPendingFileName, false);
            irNameCursor = irPendingFileName.length();
            irStatus = irPendingFileName.length() ? "NAME THEN ENTER" : "NAME REQUIRED";
            drawIrScreen();
        }
        return;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (irFileMode) {
            irFileMode = false;
            irStatus = "LISTENING";
            drawIrScreen();
            return;
        }
        if (irConfirmMode) {
            irConfirmMode = false;
            irNameMode = false;
            irPendingFileName = "";
            irAutoSaveNext = true;
            irStatus = "DISCARDED LISTEN";
            if (!irReady) irConfigurePins();
            drawIrScreen();
            return;
        }
        irAutoSaveNext = false;
        if (irReady) {
            IrReceiver.end();
            irReady = false;
        }
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 4;
        currentHardwareScroll = 4;
        targetHardwareScroll = 4;
        drawHardwareMenu();
        return;
    }

    if ((hasLeft || hasRight) && !irFileMode && !irConfirmMode && irAutoSaveNext) {
        irAutoSaveNext = false;
        irStatus = "CAPTURE CANCELLED";
    }

    if (hasLeft) {
        playSound(sound_hover, sound_hover_size);
        if (irConfirmMode) {
            irConfirmFocus--;
            if (irConfirmFocus < 0) irConfirmFocus = IR_CAPTURE_ACTION_COUNT - 1;
            targetIrConfirmScroll -= 1.0;
        } else if (irFileMode) {
            if (!irSavedFiles.empty()) {
                irFileFocus--;
                if (irFileFocus < 0) irFileFocus = (int)irSavedFiles.size() - 1;
                targetIrFileScroll -= 1.0;
            }
        } else {
            irFocus--;
            if (irFocus < 0) irFocus = IR_ACTION_COUNT - 1;
            targetIrActionScroll -= 1.0;
        }
        drawIrScreen();
    }
    if (hasRight) {
        playSound(sound_hover, sound_hover_size);
        if (irConfirmMode) {
            irConfirmFocus++;
            if (irConfirmFocus >= IR_CAPTURE_ACTION_COUNT) irConfirmFocus = 0;
            targetIrConfirmScroll += 1.0;
        } else if (irFileMode) {
            if (!irSavedFiles.empty()) {
                irFileFocus++;
                if (irFileFocus >= (int)irSavedFiles.size()) irFileFocus = 0;
                targetIrFileScroll += 1.0;
            }
        } else {
            irFocus++;
            if (irFocus >= IR_ACTION_COUNT) irFocus = 0;
            targetIrActionScroll += 1.0;
        }
        drawIrScreen();
    }

    if (!status.enter) return;
    playSound(sound_select, sound_select_size);

    if (irConfirmMode) {
        if (irConfirmFocus == 0) {
            irConfirmMode = false;
            irNameMode = true;
            irAutoSaveNext = false;
            irPendingFileName = irDefaultFileStem();
            irNameCursor = irPendingFileName.length();
            irStatus = "NAME THEN ENTER";
        } else {
            irConfirmMode = false;
            irNameMode = false;
            irPendingFileName = "";
            irAutoSaveNext = true;
            irStatus = "DISCARDED LISTEN";
            if (!irReady) irConfigurePins();
        }
        drawIrScreen();
        return;
    }

    if (irFileMode) {
        if (irSavedFiles.empty()) {
            irPopulateSavedFiles();
        } else {
            irSendStoredFile(irSavedFiles[irFileFocus]);
        }
        drawIrScreen();
        return;
    }

    if (irFocus != 0) irAutoSaveNext = false;

    if (irFocus == 0) {
        irAutoSaveNext = true;
        irConfirmMode = false;
        irNameMode = false;
        irPendingFileName = "";
        irConfirmFocus = 0;
        if (!irReady) irConfigurePins();
        irStatus = "PRESS REMOTE";
    } else if (irFocus == 1) {
        irFileMode = true;
        irFileFocus = 0;
        irPopulateSavedFiles();
        currentIrFileScroll = irFileFocus;
        targetIrFileScroll = irFileFocus;
    } else if (irFocus == 2) {
        irPinsSwapped = !irPinsSwapped;
        irConfigurePins();
        irStatus = irPinsSwapped ? "PINS SWAPPED" : "PINS NORMAL";
    } else if (irFocus == 3) {
        if (irReady) {
            IrReceiver.end();
            irReady = false;
        }
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 4;
        currentHardwareScroll = 4;
        targetHardwareScroll = 4;
        drawHardwareMenu();
        return;
    }

    if (appState == STATE_IR) drawIrScreen();
}
