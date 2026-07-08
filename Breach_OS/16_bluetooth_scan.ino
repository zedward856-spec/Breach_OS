// NETWORK NODE / BLUETOOTH: scan BLE advertisements and show every field the ESP32-S3 can see.

static constexpr int BLUETOOTH_SCAN_SECONDS = 5;
static constexpr int BLUETOOTH_MAX_DEVICES = 24;
static constexpr int BLUETOOTH_LIST_VISIBLE_ROWS = 4;
static constexpr int BLUETOOTH_DETAIL_VISIBLE_ROWS = 7;
static constexpr int BLUETOOTH_TEXT_COLS = 35;

static void bluetoothParseAdPayload(const BluetoothScanDeviceInfo &info, std::vector<String> &lines);
static int bluetoothFrameTypeFromPayload(const std::vector<uint8_t> &payload);
static void bluetoothDrawDetailLine(const String &line, int x, int y, int maxChars);
static void bluetoothDrawVerticalScrollIndicator(int scroll, int maxScroll, int x, int y, int h);
#if BREACH_BLE_SCAN_AVAILABLE
static BluetoothScanDeviceInfo bluetoothInfoFromDevice(BLEAdvertisedDevice device);
static int bluetoothFindDeviceIndex(const String &address);
static bool bluetoothInfoLooksRicher(const BluetoothScanDeviceInfo &candidate, const BluetoothScanDeviceInfo &current);
static void bluetoothRememberDevice(BLEAdvertisedDevice device);
static void bluetoothAnimateScanProgress(int fromProgress, int toProgress, const String &statusText, BLEScan* scan);
#endif

static std::vector<BluetoothScanDeviceInfo> bluetoothDevices;
static int bluetoothFocus = 0;
static int bluetoothListScroll = 0;
static bool bluetoothDetailMode = false;
static int bluetoothDetailScroll = 0;
static String bluetoothStatus = "READY";
static unsigned long bluetoothLastScanMs = 0;
static bool bluetoothBleStarted = false;
static int bluetoothObservedAdvertisements = 0;

static String bluetoothShort(String s, int maxLen) {
    if ((int)s.length() <= maxLen) return s;
    if (maxLen <= 1) return s.substring(0, maxLen);
    return s.substring(0, maxLen - 1) + "~";
}

static String bluetoothHexByte(uint8_t value) {
    const char* hex = "0123456789ABCDEF";
    String out;
    out += hex[(value >> 4) & 0x0F];
    out += hex[value & 0x0F];
    return out;
}

static String bluetoothHex16(uint16_t value) {
    return "0x" + bluetoothHexByte((value >> 8) & 0xFF) + bluetoothHexByte(value & 0xFF);
}

static String bluetoothBytesToHex(const uint8_t* data, size_t len) {
    String out;
    for (size_t i = 0; i < len; i++) {
        if (i > 0) out += " ";
        out += bluetoothHexByte(data[i]);
    }
    return out;
}

static String bluetoothStringToHex(const String &data) {
    String out;
    for (size_t i = 0; i < data.length(); i++) {
        if (i > 0) out += " ";
        out += bluetoothHexByte((uint8_t)data[i]);
    }
    return out;
}

static String bluetoothBytesToAscii(const uint8_t* data, size_t len) {
    String out;
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c >= 32 && c <= 126) out += c;
        else out += '.';
    }
    return out;
}

static String bluetoothStringToAscii(const String &data) {
    String out;
    for (size_t i = 0; i < data.length(); i++) {
        char c = data[i];
        if (c >= 32 && c <= 126) out += c;
        else out += '.';
    }
    return out;
}

static String bluetoothAddressTypeName(uint8_t type) {
    switch (type) {
        case 0: return "PUBLIC";
        case 1: return "RANDOM";
        case 2: return "RPA/PUBLIC_ID";
        case 3: return "RPA/RANDOM_ID";
        default: return "TYPE " + String(type);
    }
}

static String bluetoothFrameTypeName(int type) {
    switch (type) {
        case 1: return "EDDYSTONE UUID";
        case 2: return "EDDYSTONE URL";
        case 3: return "EDDYSTONE TLM";
        default: return "UNKNOWN/GENERIC";
    }
}

static int bluetoothFrameTypeFromPayload(const std::vector<uint8_t> &payload) {
    size_t i = 0;
    while (i < payload.size()) {
        uint8_t len = payload[i];
        if (len == 0) {
            i++;
            continue;
        }
        if (i + len >= payload.size()) break;

        uint8_t type = payload[i + 1];
        const uint8_t* data = &payload[i + 2];
        size_t dataLen = len - 1;
        if (type == 0x16 && dataLen >= 3 && data[0] == 0xAA && data[1] == 0xFE) {
            if (data[2] == 0x00) return 1;
            if (data[2] == 0x10) return 2;
            if (data[2] == 0x20) return 3;
        }
        i += len + 1;
    }
    return 0;
}

static String bluetoothAdTypeName(uint8_t type) {
    switch (type) {
        case 0x01: return "FLAGS";
        case 0x02: return "16 UUID PART";
        case 0x03: return "16 UUID FULL";
        case 0x04: return "32 UUID PART";
        case 0x05: return "32 UUID FULL";
        case 0x06: return "128 UUID PART";
        case 0x07: return "128 UUID FULL";
        case 0x08: return "SHORT NAME";
        case 0x09: return "COMPLETE NAME";
        case 0x0A: return "TX POWER";
        case 0x0D: return "CLASS DEV";
        case 0x0E: return "PAIR HASH";
        case 0x0F: return "PAIR RAND";
        case 0x10: return "DEVICE ID";
        case 0x12: return "CONN INTERVAL";
        case 0x14: return "16 SOLICIT";
        case 0x15: return "128 SOLICIT";
        case 0x16: return "SERVICE DATA16";
        case 0x17: return "PUBLIC TARGET";
        case 0x18: return "RANDOM TARGET";
        case 0x19: return "APPEARANCE";
        case 0x1A: return "ADV INTERVAL";
        case 0x1B: return "LE ADDRESS";
        case 0x1C: return "LE ROLE";
        case 0x1D: return "PAIR HASH256";
        case 0x1E: return "PAIR RAND256";
        case 0x20: return "SERVICE DATA32";
        case 0x21: return "SERVICE DATA128";
        case 0x24: return "URI";
        case 0x25: return "INDOOR POS";
        case 0x26: return "TRANS DISC";
        case 0x27: return "LE FEATURES";
        case 0x28: return "CHAN MAP";
        case 0x29: return "PB ADV";
        case 0x2A: return "MESH MSG";
        case 0x2B: return "MESH BEACON";
        case 0x2C: return "BIG INFO";
        case 0x2D: return "BROADCAST CODE";
        case 0x3D: return "3D INFO";
        case 0xFF: return "MFG SPECIFIC";
        default: return "AD 0x" + bluetoothHexByte(type);
    }
}

static String bluetoothFlagsText(uint8_t flags) {
    String out = "0x" + bluetoothHexByte(flags) + " ";
    if (flags & 0x01) out += "LIMITED ";
    if (flags & 0x02) out += "GENERAL ";
    if (flags & 0x04) out += "NO_BREDR ";
    if (flags & 0x08) out += "LE+BR_CTRL ";
    if (flags & 0x10) out += "LE+BR_HOST ";
    if (out.endsWith(" ")) out.remove(out.length() - 1);
    return out;
}

static int bluetoothSignalBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -80) return 2;
    if (rssi >= -90) return 1;
    return 0;
}

static void bluetoothEnsureVisible() {
    if (bluetoothFocus < 0) bluetoothFocus = 0;
    if (bluetoothFocus >= (int)bluetoothDevices.size()) bluetoothFocus = bluetoothDevices.empty() ? 0 : (int)bluetoothDevices.size() - 1;
    if (bluetoothFocus < bluetoothListScroll) bluetoothListScroll = bluetoothFocus;
    if (bluetoothFocus >= bluetoothListScroll + BLUETOOTH_LIST_VISIBLE_ROWS) {
        bluetoothListScroll = bluetoothFocus - BLUETOOTH_LIST_VISIBLE_ROWS + 1;
    }
    if (bluetoothListScroll < 0) bluetoothListScroll = 0;
}

static void bluetoothDrawVerticalScrollIndicator(int scroll, int maxScroll, int x, int y, int h) {
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

static void bluetoothDrawDetailLine(const String &line, int x, int y, int maxChars) {
    String text = bluetoothShort(line, maxChars);
    int colon = text.indexOf(':');
    if (colon > 0) {
        String label = text.substring(0, colon + 1);
        String value = text.substring(colon + 1);
        canvas.setTextColor(CP_CYAN);
        canvas.setCursor(x, y);
        canvas.print(label);
        canvas.setTextColor(WHITE);
        canvas.setCursor(x + canvas.textWidth(label), y);
        canvas.print(value);
        return;
    }

    uint16_t color = (text.startsWith("#") || text.startsWith("AD STRUCTURES")) ? CP_CYAN : WHITE;
    canvas.setTextColor(color);
    canvas.setCursor(x, y);
    canvas.print(text);
}

static void bluetoothAddWrappedLine(std::vector<String> &lines, String line) {
    if (line.length() == 0) {
        lines.push_back("");
        return;
    }
    while ((int)line.length() > BLUETOOTH_TEXT_COLS) {
        int cut = BLUETOOTH_TEXT_COLS;
        for (int i = BLUETOOTH_TEXT_COLS; i > 14; i--) {
            if (line[i] == ' ') {
                cut = i;
                break;
            }
        }
        lines.push_back(line.substring(0, cut));
        line = "  " + line.substring(cut);
        line.trim();
        line = "  " + line;
    }
    lines.push_back(line);
}

static void bluetoothAddHexLines(std::vector<String> &lines, const String &label, const String &hex) {
    if (hex == "") {
        lines.push_back(label + ": none");
        return;
    }
    int pos = 0;
    int chunk = 30;
    bool first = true;
    while (pos < (int)hex.length()) {
        int end = pos + chunk;
        if (end < (int)hex.length()) {
            int back = hex.lastIndexOf(' ', end);
            if (back > pos + 8) end = back;
        } else {
            end = hex.length();
        }
        String prefix = first ? (label + ": ") : "  ";
        lines.push_back(prefix + hex.substring(pos, end));
        pos = end;
        while (pos < (int)hex.length() && hex[pos] == ' ') pos++;
        first = false;
    }
}

static void bluetoothParseAdPayload(const BluetoothScanDeviceInfo &info, std::vector<String> &lines) {
    if (info.payload.empty()) {
        lines.push_back("AD STRUCTURES: none captured");
        return;
    }

    lines.push_back("AD STRUCTURES:");
    size_t i = 0;
    int item = 1;
    while (i < info.payload.size()) {
        uint8_t len = info.payload[i];
        if (len == 0) {
            i++;
            continue;
        }
        if (i + len >= info.payload.size()) {
            lines.push_back("#" + String(item) + " TRUNCATED LEN " + String(len));
            break;
        }
        uint8_t type = info.payload[i + 1];
        const uint8_t* data = &info.payload[i + 2];
        size_t dataLen = len - 1;
        String header = "#" + String(item) + " " + bluetoothAdTypeName(type) + " len " + String((int)dataLen);
        if (type == 0x01 && dataLen >= 1) {
            header += " " + bluetoothFlagsText(data[0]);
            lines.push_back(header);
        } else if ((type == 0x08 || type == 0x09) && dataLen > 0) {
            bluetoothAddWrappedLine(lines, header + " \"" + bluetoothBytesToAscii(data, dataLen) + "\"");
        } else if (type == 0x0A && dataLen >= 1) {
            header += " " + String((int8_t)data[0]) + " dBm";
            lines.push_back(header);
        } else if (type == 0x19 && dataLen >= 2) {
            uint16_t appearance = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            header += " " + bluetoothHex16(appearance);
            lines.push_back(header);
        } else if (type == 0xFF && dataLen >= 2) {
            uint16_t company = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
            header += " company " + bluetoothHex16(company);
            lines.push_back(header);
            bluetoothAddHexLines(lines, "  data", bluetoothBytesToHex(data, dataLen));
        } else {
            lines.push_back(header);
            if (dataLen > 0) bluetoothAddHexLines(lines, "  data", bluetoothBytesToHex(data, dataLen));
        }
        i += len + 1;
        item++;
    }
}

#if BREACH_BLE_SCAN_AVAILABLE
static BluetoothScanDeviceInfo bluetoothInfoFromDevice(BLEAdvertisedDevice device) {
    BluetoothScanDeviceInfo info;
    info.address = device.getAddress().toString();
    info.addressType = device.getAddressType();
    info.name = device.haveName() ? device.getName() : String("(unnamed)");
    info.haveRssi = device.haveRSSI();
    info.rssi = device.haveRSSI() ? device.getRSSI() : -999;
    info.haveTxPower = device.haveTXPower();
    info.txPower = device.haveTXPower() ? device.getTXPower() : 0;
    info.haveAppearance = device.haveAppearance();
    info.appearance = device.haveAppearance() ? device.getAppearance() : 0;
    info.advType = device.getAdvType();
    info.legacy = device.isLegacyAdvertisement();
    info.scannable = device.isScannable();
    info.connectable = device.isConnectable();

    if (device.haveManufacturerData()) {
        String mfg = device.getManufacturerData();
        info.manufacturerHex = bluetoothStringToHex(mfg);
    }

    for (int i = 0; i < device.getServiceUUIDCount(); i++) {
        info.serviceUuids.push_back(device.getServiceUUID(i).toString());
    }
    for (int i = 0; i < device.getServiceDataUUIDCount(); i++) {
        info.serviceDataUuids.push_back(device.getServiceDataUUID(i).toString());
    }
    for (int i = 0; i < device.getServiceDataCount(); i++) {
        String data = device.getServiceData(i);
        info.serviceDataHex.push_back(bluetoothStringToHex(data));
        info.serviceDataAscii.push_back(bluetoothStringToAscii(data));
    }

    uint8_t* payload = device.getPayload();
    size_t payloadLength = device.getPayloadLength();
    if (payload != nullptr && payloadLength > 0) {
        info.payload.reserve(payloadLength);
        for (size_t i = 0; i < payloadLength; i++) {
            info.payload.push_back(payload[i]);
        }
    }
    info.frameType = bluetoothFrameTypeFromPayload(info.payload);
    return info;
}

static int bluetoothFindDeviceIndex(const String &address) {
    for (int i = 0; i < (int)bluetoothDevices.size(); i++) {
        if (bluetoothDevices[i].address == address) return i;
    }
    return -1;
}

static bool bluetoothInfoLooksRicher(const BluetoothScanDeviceInfo &candidate, const BluetoothScanDeviceInfo &current) {
    int candidateScore = (int)candidate.payload.size() + (candidate.name != "(unnamed)" ? 8 : 0) +
                         ((int)candidate.serviceUuids.size() * 4) + ((int)candidate.serviceDataHex.size() * 4) +
                         (candidate.manufacturerHex.length() > 0 ? 4 : 0);
    int currentScore = (int)current.payload.size() + (current.name != "(unnamed)" ? 8 : 0) +
                       ((int)current.serviceUuids.size() * 4) + ((int)current.serviceDataHex.size() * 4) +
                       (current.manufacturerHex.length() > 0 ? 4 : 0);
    if (candidateScore != currentScore) return candidateScore > currentScore;
    return candidate.haveRssi && (!current.haveRssi || candidate.rssi > current.rssi);
}

static void bluetoothRememberDevice(BLEAdvertisedDevice device) {
    bluetoothObservedAdvertisements++;
    BluetoothScanDeviceInfo info = bluetoothInfoFromDevice(device);
    int existing = bluetoothFindDeviceIndex(info.address);
    if (existing >= 0) {
        if (bluetoothInfoLooksRicher(info, bluetoothDevices[existing])) bluetoothDevices[existing] = info;
        return;
    }
    if ((int)bluetoothDevices.size() < BLUETOOTH_MAX_DEVICES) bluetoothDevices.push_back(info);
}

class BluetoothScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        bluetoothRememberDevice(advertisedDevice);
    }
};

static BluetoothScanCallbacks bluetoothScanCallbacks;
#endif

static void bluetoothBuildDetailLines(int index, std::vector<String> &lines) {
    lines.clear();
    if (index < 0 || index >= (int)bluetoothDevices.size()) {
        lines.push_back("NO DEVICE SELECTED");
        return;
    }

    const BluetoothScanDeviceInfo &d = bluetoothDevices[index];
    lines.push_back("NAME: " + d.name);
    lines.push_back("ADDR: " + d.address);
    lines.push_back("ADDR TYPE: " + bluetoothAddressTypeName(d.addressType));
    lines.push_back(String("RSSI: ") + (d.haveRssi ? String(d.rssi) + " dBm" : "not advertised"));
    lines.push_back(String("SIGNAL: ") + String(bluetoothSignalBars(d.rssi)) + "/4 bars");
    lines.push_back(String("TX POWER: ") + (d.haveTxPower ? String(d.txPower) + " dBm" : "not advertised"));
    lines.push_back(String("APPEARANCE: ") + (d.haveAppearance ? bluetoothHex16(d.appearance) + " / " + String(d.appearance) : "not advertised"));
    lines.push_back("ADV TYPE: 0x" + bluetoothHexByte(d.advType));
    lines.push_back(String("CONNECTABLE: ") + (d.connectable ? "yes" : "no"));
    lines.push_back(String("SCANNABLE: ") + (d.scannable ? "yes" : "no"));
    lines.push_back(String("LEGACY ADV: ") + (d.legacy ? "yes" : "no"));
    lines.push_back("FRAME: " + bluetoothFrameTypeName(d.frameType));

    if (!d.serviceUuids.empty()) {
        for (int i = 0; i < (int)d.serviceUuids.size(); i++) {
            bluetoothAddWrappedLine(lines, "SERVICE UUID[" + String(i) + "]: " + d.serviceUuids[i]);
        }
    } else {
        lines.push_back("SERVICE UUIDS: none advertised");
    }

    if (!d.serviceDataUuids.empty() || !d.serviceDataHex.empty()) {
        int count = max((int)d.serviceDataUuids.size(), (int)d.serviceDataHex.size());
        for (int i = 0; i < count; i++) {
            if (i < (int)d.serviceDataUuids.size()) {
                bluetoothAddWrappedLine(lines, "SVC DATA UUID[" + String(i) + "]: " + d.serviceDataUuids[i]);
            }
            if (i < (int)d.serviceDataHex.size()) {
                bluetoothAddHexLines(lines, "SVC DATA HEX[" + String(i) + "]", d.serviceDataHex[i]);
                bluetoothAddWrappedLine(lines, "SVC DATA ASCII[" + String(i) + "]: " + d.serviceDataAscii[i]);
            }
        }
    } else {
        lines.push_back("SERVICE DATA: none advertised");
    }

    if (d.manufacturerHex != "") {
        if (d.payload.size() > 0) {
            // Manufacturer company id is also decoded again inside AD STRUCTURES from raw payload.
        }
        bluetoothAddHexLines(lines, "MFG HEX", d.manufacturerHex);
    } else {
        lines.push_back("MFG DATA: none advertised");
    }

    lines.push_back("RAW PAYLOAD LEN: " + String((int)d.payload.size()) + " bytes");
    if (!d.payload.empty()) {
        bluetoothAddHexLines(lines, "RAW HEX", bluetoothBytesToHex(d.payload.data(), d.payload.size()));
    }
    bluetoothParseAdPayload(d, lines);
}

static void bluetoothScanProgress(int progress, String statusText) {
    drawProgressBar(progress, statusText, CP_CYAN);
}

#if BREACH_BLE_SCAN_AVAILABLE
static void bluetoothAnimateScanProgress(int fromProgress, int toProgress, const String &statusText, BLEScan* scan) {
    if (fromProgress < 0) fromProgress = 0;
    if (toProgress > 100) toProgress = 100;
    if (toProgress < fromProgress) toProgress = fromProgress;

    const unsigned long sliceMs = 1000;
    unsigned long startMs = millis();
    int lastProgress = fromProgress - 1;
    while (scan != nullptr && scan->isScanning()) {
        unsigned long elapsed = millis() - startMs;
        if (elapsed > sliceMs) elapsed = sliceMs;
        int progress = fromProgress + ((toProgress - fromProgress) * (int)elapsed) / (int)sliceMs;
        if (progress != lastProgress) {
            bluetoothScanProgress(progress, statusText);
            lastProgress = progress;
        }
        delay(35);
    }
    bluetoothScanProgress(toProgress, statusText);
}
#endif

static void bluetoothPerformScan() {
    bluetoothDevices.clear();
    bluetoothDevices.reserve(BLUETOOTH_MAX_DEVICES);
    bluetoothObservedAdvertisements = 0;
    bluetoothFocus = 0;
    bluetoothListScroll = 0;
    bluetoothDetailMode = false;
    bluetoothDetailScroll = 0;
    bluetoothStatus = "SCANNING BLE ADV";

#if BREACH_BLE_SCAN_AVAILABLE
    if (!bluetoothBleStarted) {
        BLEDevice::init("Breach_OS");
        bluetoothBleStarted = true;
    }

    BLEScan* scan = BLEDevice::getScan();
    if (scan == nullptr) {
        bluetoothStatus = "BLE SCAN UNAVAILABLE";
        return;
    }
    scan->setActiveScan(true);   // Request scan-response data too, when the advertiser allows it.
    scan->setInterval(80);
    scan->setWindow(60);
    scan->setAdvertisedDeviceCallbacks(&bluetoothScanCallbacks, true, true); // true = no BLEScanResults heap map; we dedupe/cap locally.
    scan->clearResults();

    for (int second = 0; second < BLUETOOTH_SCAN_SECONDS; second++) {
        int fromProgress = (second * 100) / BLUETOOTH_SCAN_SECONDS;
        int toProgress = ((second + 1) * 100) / BLUETOOTH_SCAN_SECONDS;
        String scanText = "BLE ADV SCAN " + String(second + 1) + "/" + String(BLUETOOTH_SCAN_SECONDS);
        bluetoothScanProgress(fromProgress, scanText);
        if (scan->start(1, nullptr, false)) {
            bluetoothAnimateScanProgress(fromProgress, toProgress, scanText, scan);
        } else {
            bluetoothScanProgress(toProgress, "BLE SCAN RETRY");
            delay(120);
        }
    }
    bluetoothScanProgress(100, "BLE ADV PARSE");
    scan->setAdvertisedDeviceCallbacks(nullptr, false, true);
    scan->clearResults();
    int rawCount = bluetoothObservedAdvertisements;

    std::sort(bluetoothDevices.begin(), bluetoothDevices.end(), [](const BluetoothScanDeviceInfo &a, const BluetoothScanDeviceInfo &b) {
        return a.rssi > b.rssi;
    });

    bluetoothLastScanMs = millis();
    bluetoothStatus = String((int)bluetoothDevices.size()) + " BLE DEV";
    if (rawCount > (int)bluetoothDevices.size()) {
        bluetoothStatus += " / " + String(rawCount) + " ADV";
    }
#else
    bluetoothStatus = "BLE API NOT BUILT";
#endif
}

void enterBluetoothScan() {
    appState = STATE_BLUETOOTH_SCAN;
    bluetoothPerformScan();
    drawBluetoothScanScreen();
}

void drawBluetoothScanScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawGlitchText("BLUETOOTH", 72, 4, 1, bluetoothDetailMode ? CP_YELLOW : CP_CYAN, true, true);
    drawTopStatusIcons(132, 1);
    canvas.drawLine(5, 18, 235, 18, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    String mode = bluetoothDetailMode ? "DEVICE INTEL" : "BLE ADVERTISEMENT SCAN";
    canvas.setCursor(7, 22);
    canvas.print(mode);

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(7, 32);
    canvas.print(bluetoothShort(bluetoothStatus, 30));

    if (bluetoothDetailMode) {
        std::vector<String> lines;
        bluetoothBuildDetailLines(bluetoothFocus, lines);
        int maxScroll = (int)lines.size() - BLUETOOTH_DETAIL_VISIBLE_ROWS;
        if (maxScroll < 0) maxScroll = 0;
        if (bluetoothDetailScroll > maxScroll) bluetoothDetailScroll = maxScroll;
        if (bluetoothDetailScroll < 0) bluetoothDetailScroll = 0;

        int y = 44;
        for (int row = 0; row < BLUETOOTH_DETAIL_VISIBLE_ROWS; row++) {
            int idx = bluetoothDetailScroll + row;
            if (idx >= (int)lines.size()) break;
            bluetoothDrawDetailLine(lines[idx], 7, y, 35);
            y += 11;
        }
        bluetoothDrawVerticalScrollIndicator(bluetoothDetailScroll, maxScroll, 229, 44, 78);

        canvas.setTextColor(CP_DIM);
        canvas.setCursor(7, 126);
        canvas.print("UP/DN SCROLL  < BACK  R RESCAN");
    } else {
        if (bluetoothDevices.empty()) {
            canvas.setTextColor(CP_RED);
            canvas.drawCenterString("NO BLE ADVERTISERS FOUND", 120, 61);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString("R RESCAN  ESC BACK", 120, 88);
        } else {
            bluetoothEnsureVisible();
            int y = 45;
            int listMaxScroll = (int)bluetoothDevices.size() - BLUETOOTH_LIST_VISIBLE_ROWS;
            if (listMaxScroll < 0) listMaxScroll = 0;
            for (int row = 0; row < BLUETOOTH_LIST_VISIBLE_ROWS; row++) {
                int idx = bluetoothListScroll + row;
                if (idx >= (int)bluetoothDevices.size()) break;
                const BluetoothScanDeviceInfo &d = bluetoothDevices[idx];
                bool selected = idx == bluetoothFocus;
                uint16_t color = selected ? CP_YELLOW : CP_DIM;
                drawChippedButton(6, y - 2, 216, 20, color);
                canvas.setTextColor(color);
                canvas.setTextSize(1);
                canvas.setCursor(13, y);
                String name = bluetoothShort(d.name, 16);
                canvas.print(String(idx + 1) + " " + name);
                canvas.setCursor(150, y);
                canvas.print(d.haveRssi ? String(d.rssi) + "dBm" : "RSSI?");
                canvas.setTextColor(selected ? CP_CYAN : CP_DIM);
                canvas.setCursor(13, y + 10);
                canvas.print(bluetoothShort(d.address + " " + bluetoothAddressTypeName(d.addressType), 34));
                y += 23;
            }
            bluetoothDrawVerticalScrollIndicator(bluetoothListScroll, listMaxScroll, 229, 45, 78);
        }
    }

    pushCanvas();
}

void handleBluetoothScanInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasBack = false, rescan = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c == '`') hasBack = true;
        if (c == 'r' || c == 'R') rescan = true;
    }

    if (rescan || (status.enter && bluetoothDevices.empty())) {
        playSound(sound_select, sound_select_size);
        bluetoothPerformScan();
        return;
    }

    if (hasBack || (!bluetoothDetailMode && hasLeft)) {
        playSound(sound_select, sound_select_size);
        appState = STATE_MAIN_MENU;
        mainMenuFocus = 7;
        currentMenuScroll = mainMenuFocus;
        targetMenuScroll = mainMenuFocus;
        showMenuDesc = false;
        descAnimWidth = 0.0;
        drawMainMenu();
        return;
    }

    if (bluetoothDetailMode) {
        std::vector<String> lines;
        bluetoothBuildDetailLines(bluetoothFocus, lines);
        int maxScroll = (int)lines.size() - BLUETOOTH_DETAIL_VISIBLE_ROWS;
        if (maxScroll < 0) maxScroll = 0;
        if (hasLeft || status.enter) {
            playSound(sound_select, sound_select_size);
            bluetoothDetailMode = false;
            bluetoothDetailScroll = 0;
            return;
        }
        if (hasUp && bluetoothDetailScroll > 0) {
            playSound(sound_hover, sound_hover_size);
            bluetoothDetailScroll--;
        }
        if (hasDown && bluetoothDetailScroll < maxScroll) {
            playSound(sound_hover, sound_hover_size);
            bluetoothDetailScroll++;
        }
        return;
    }

    if (status.enter || hasRight) {
        if (!bluetoothDevices.empty()) {
            playSound(sound_select, sound_select_size);
            bluetoothDetailMode = true;
            bluetoothDetailScroll = 0;
        }
        return;
    }

    if (!bluetoothDevices.empty()) {
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            bluetoothFocus--;
            if (bluetoothFocus < 0) bluetoothFocus = bluetoothDevices.size() - 1;
            bluetoothEnsureVisible();
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            bluetoothFocus++;
            if (bluetoothFocus >= (int)bluetoothDevices.size()) bluetoothFocus = 0;
            bluetoothEnsureVisible();
        }
    }
}
