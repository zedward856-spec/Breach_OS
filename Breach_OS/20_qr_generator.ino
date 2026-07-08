// HARDWARE NODE / QR GENERATOR: type text and render a scannable QR code on the Cardputer display.

static constexpr uint8_t QR_GENERATOR_VERSION = 5;
static constexpr uint8_t QR_GENERATOR_ECC = ECC_LOW;
static constexpr size_t QR_GENERATOR_MAX_CHARS = 96;
static constexpr size_t QR_GENERATOR_BUFFER_BYTES = 512;

static String qrGeneratorText = "";
static String qrGeneratorStatus = "TYPE TEXT";

static String qrGeneratorPreviewLine(int lineIndex) {
    int start = lineIndex * 18;
    if (start >= (int)qrGeneratorText.length()) return "";
    int end = start + 18;
    if (end > (int)qrGeneratorText.length()) end = qrGeneratorText.length();
    return qrGeneratorText.substring(start, end);
}

void enterQrGenerator() {
    appState = STATE_QR_GENERATOR;
    if (qrGeneratorText.length() == 0) qrGeneratorStatus = "TYPE TEXT";
    drawQrGeneratorScreen();
}

void drawQrGeneratorScreen() {
    static uint8_t qrModules[QR_GENERATOR_BUFFER_BYTES];
    memset(qrModules, 0, QR_GENERATOR_BUFFER_BYTES);

    QRCode qrcode;
    bool qrReady = false;
    if (qrGeneratorText.length() == 0) {
        qrGeneratorStatus = "TYPE TEXT";
    } else {
        int8_t result = lgfx_qrcode_initText(&qrcode, qrModules, QR_GENERATOR_VERSION, QR_GENERATOR_ECC, qrGeneratorText.c_str());
        qrReady = (result == 0);
        qrGeneratorStatus = qrReady ? "QR READY" : "TOO LONG";
    }

    canvas.startWrite();
    canvas.fillScreen(CP_BG);

    drawGlitchText("QR GENERATOR", 72, 4, 1, CP_CYAN, true, true);
    drawTopStatusIcons(132, 1);
    canvas.drawLine(8, 20, 232, 20, CP_CYAN);

    constexpr int moduleSize = 2;
    constexpr int quietModules = 4;
    constexpr int qrMaxSize = QR_GENERATOR_VERSION * 4 + 17;
    constexpr int qrPixels = (qrMaxSize + quietModules * 2) * moduleSize;
    constexpr int qrX = 8;
    constexpr int qrY = 28;

    canvas.fillRect(qrX, qrY, qrPixels, qrPixels, WHITE);
    if (qrReady) {
        for (int y = 0; y < qrcode.size; y++) {
            for (int x = 0; x < qrcode.size; x++) {
                if (!lgfx_qrcode_getModule(&qrcode, x, y)) continue;
                canvas.fillRect(qrX + (x + quietModules) * moduleSize,
                                qrY + (y + quietModules) * moduleSize,
                                moduleSize, moduleSize, BLACK);
            }
        }
    } else {
        canvas.setTextColor(CP_DIM);
        canvas.setTextSize(1);
        canvas.drawCenterString("TYPE", qrX + qrPixels / 2, qrY + 33);
        canvas.drawCenterString("DATA", qrX + qrPixels / 2, qrY + 45);
    }
    canvas.drawRect(qrX - 1, qrY - 1, qrPixels + 2, qrPixels + 2, CP_CYAN);

    int panelX = 104;
    drawChippedButton(panelX, 28, 130, 89, CP_DIM);
    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(panelX + 8, 36);
    canvas.print("DATA:");

    canvas.setTextColor(qrReady ? WHITE : CP_DIM);
    for (int i = 0; i < 3; i++) {
        String line = qrGeneratorPreviewLine(i);
        canvas.setCursor(panelX + 8, 49 + i * 10);
        canvas.print(line.length() ? line : (i == 0 ? "<EMPTY>" : ""));
    }

    canvas.setTextColor(qrReady ? CP_CYAN : CP_YELLOW);
    canvas.setCursor(panelX + 8, 84);
    canvas.print(qrGeneratorStatus);
    canvas.setTextColor(CP_DIM);
    canvas.setCursor(panelX + 8, 96);
    canvas.print(String(qrGeneratorText.length()) + "/" + String(QR_GENERATOR_MAX_CHARS));

    canvas.setTextColor(CP_DIM);
    canvas.setCursor(8, 122);
    canvas.print("TYPE  DEL ERASE  ENTER QR  ESC BACK");

    pushCanvas();
}

void handleQrGeneratorInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    bool backspaceRequested = status.del;

    for (char c : status.word) {
        if (c == '\b' || c == 0x7f) backspaceRequested = true;
        if (!status.fn && (c == ',' || c == '`')) hasBack = true;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 6;
        currentHardwareScroll = 6;
        targetHardwareScroll = 6;
        drawHardwareMenu();
        return;
    }

    if (backspaceRequested) {
        if (qrGeneratorText.length() > 0) {
            qrGeneratorText.remove(qrGeneratorText.length() - 1);
            qrGeneratorStatus = qrGeneratorText.length() ? "QR READY" : "TYPE TEXT";
        }
        return;
    }

    for (char c : status.word) {
        if (status.fn && (c == ';' || c == '.' || c == ',' || c == '/')) continue;
        if (c == '\b' || c == 0x7f) continue;
        if (c < 32 || c > 126 || c == ',' || c == '`') continue;
        if (qrGeneratorText.length() < QR_GENERATOR_MAX_CHARS) {
            qrGeneratorText += c;
            qrGeneratorStatus = "QR READY";
        } else {
            qrGeneratorStatus = "MAX LENGTH";
        }
    }

    if (status.enter) {
        playSound(sound_select, sound_select_size);
        qrGeneratorStatus = qrGeneratorText.length() ? "QR READY" : "TYPE TEXT";
    }
}
