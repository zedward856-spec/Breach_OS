// NETWORK NODE / WIFI GRAPH: passive Wi-Fi packet traffic graph with channel switching.

static constexpr int WIFI_GRAPH_MIN_CHANNEL = 1;
static constexpr int WIFI_GRAPH_MAX_CHANNEL = 13;
static constexpr int WIFI_GRAPH_HISTORY_BARS = 56;
static constexpr unsigned long WIFI_GRAPH_SAMPLE_MS = 300;

static volatile int wifiGraphChannel = 6;
static volatile uint32_t wifiGraphPacketCount = 0;
static volatile uint32_t wifiGraphByteCount = 0;
static volatile uint32_t wifiGraphMgmtCount = 0;
static volatile uint32_t wifiGraphCtrlCount = 0;
static volatile uint32_t wifiGraphDataCount = 0;
static volatile int wifiGraphLastRssi = -127;

static uint16_t wifiGraphHistory[WIFI_GRAPH_HISTORY_BARS] = {0};
static int wifiGraphHistoryCursor = 0;
static int wifiGraphHistoryCount = 0;
static bool wifiGraphActive = false;
static String wifiGraphStatus = "READY";
static unsigned long wifiGraphLastSampleMs = 0;
static uint32_t wifiGraphLastPacketTotal = 0;
static uint32_t wifiGraphLastByteTotal = 0;
static uint32_t wifiGraphLastMgmtTotal = 0;
static uint32_t wifiGraphLastCtrlTotal = 0;
static uint32_t wifiGraphLastDataTotal = 0;
static uint32_t wifiGraphSamplePps = 0;
static uint32_t wifiGraphSampleBytesPerSec = 0;
static uint32_t wifiGraphSampleMgmt = 0;
static uint32_t wifiGraphSampleCtrl = 0;
static uint32_t wifiGraphSampleData = 0;
static uint32_t wifiGraphPeakPps = 0;

static void wifiGraphResetHistory() {
    for (int i = 0; i < WIFI_GRAPH_HISTORY_BARS; i++) wifiGraphHistory[i] = 0;
    wifiGraphHistoryCursor = 0;
    wifiGraphHistoryCount = 0;
    wifiGraphSamplePps = 0;
    wifiGraphSampleBytesPerSec = 0;
    wifiGraphSampleMgmt = 0;
    wifiGraphSampleCtrl = 0;
    wifiGraphSampleData = 0;
    wifiGraphPeakPps = 0;
}

static void wifiGraphSyncTotals() {
    wifiGraphLastPacketTotal = wifiGraphPacketCount;
    wifiGraphLastByteTotal = wifiGraphByteCount;
    wifiGraphLastMgmtTotal = wifiGraphMgmtCount;
    wifiGraphLastCtrlTotal = wifiGraphCtrlCount;
    wifiGraphLastDataTotal = wifiGraphDataCount;
    wifiGraphLastSampleMs = millis();
}

static void wifiGraphPromiscuousPacket(void *buf, int type) {
    if (buf == nullptr) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    int rxChannel = pkt->rx_ctrl.channel;
    if (rxChannel != (int)wifiGraphChannel) return;

    wifiGraphPacketCount++;
    wifiGraphByteCount += pkt->rx_ctrl.sig_len;
    wifiGraphLastRssi = pkt->rx_ctrl.rssi;

    if (type == WIFI_PKT_MGMT) {
        wifiGraphMgmtCount++;
    } else if (type == WIFI_PKT_CTRL) {
        wifiGraphCtrlCount++;
    } else if (type == WIFI_PKT_DATA) {
        wifiGraphDataCount++;
    }
}

static String wifiGraphErrText(const char *action, esp_err_t err) {
    if (err == ESP_OK) return String(action) + " OK";
    return String(action) + " ERR " + String((int)err);
}

static void wifiGraphSetChannel(int channel) {
    if (channel < WIFI_GRAPH_MIN_CHANNEL) channel = WIFI_GRAPH_MAX_CHANNEL;
    if (channel > WIFI_GRAPH_MAX_CHANNEL) channel = WIFI_GRAPH_MIN_CHANNEL;

    wifiGraphChannel = channel;
    wifiGraphResetHistory();

    bool wasActive = wifiGraphActive;
    if (wasActive) esp_wifi_set_promiscuous(false);
    esp_err_t chErr = esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    if (wasActive) {
        esp_err_t promiscErr = esp_wifi_set_promiscuous(true);
        wifiGraphActive = (chErr == ESP_OK && promiscErr == ESP_OK);
        wifiGraphStatus = wifiGraphActive ? ("MONITORING CH " + String(channel)) : wifiGraphErrText("PROMISC", promiscErr);
    } else {
        wifiGraphStatus = (chErr == ESP_OK) ? ("CH " + String(channel) + " READY") : wifiGraphErrText("CHANNEL", chErr);
    }
    wifiGraphSyncTotals();
}

void enterWifiGraph() {
    appState = STATE_WIFI_GRAPH;
    wifiGraphActive = false;
    wifiGraphStatus = "STARTING RF MONITOR";
    wifiGraphResetHistory();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(80);

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb([](void *buf, wifi_promiscuous_pkt_type_t type) {
        wifiGraphPromiscuousPacket(buf, (int)type);
    });

    esp_err_t chErr = esp_wifi_set_channel((uint8_t)wifiGraphChannel, WIFI_SECOND_CHAN_NONE);
    esp_err_t promiscErr = esp_wifi_set_promiscuous(true);
    wifiGraphActive = (chErr == ESP_OK && promiscErr == ESP_OK);
    if (chErr != ESP_OK) {
        wifiGraphStatus = wifiGraphErrText("CHANNEL", chErr);
    } else if (promiscErr != ESP_OK) {
        wifiGraphStatus = wifiGraphErrText("PROMISC", promiscErr);
    } else {
        wifiGraphStatus = "MONITORING CH " + String((int)wifiGraphChannel);
    }
    wifiGraphSyncTotals();
    drawWifiGraphScreen();
}

static void wifiGraphExitToNetworkNode() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    wifiGraphActive = false;
    wifiGraphStatus = "STOPPED";

    appState = STATE_MAIN_MENU;
    mainMenuFocus = 6;
    currentMenuScroll = mainMenuFocus;
    targetMenuScroll = mainMenuFocus;
    showMenuDesc = false;
    descAnimWidth = 0.0;
    drawMainMenu();
}

bool updateWifiGraph() {
    if (appState != STATE_WIFI_GRAPH || !wifiGraphActive) return false;

    unsigned long now = millis();
    unsigned long elapsed = now - wifiGraphLastSampleMs;
    if (elapsed < WIFI_GRAPH_SAMPLE_MS) return false;
    if (elapsed == 0) elapsed = 1;

    uint32_t packetTotal = wifiGraphPacketCount;
    uint32_t byteTotal = wifiGraphByteCount;
    uint32_t mgmtTotal = wifiGraphMgmtCount;
    uint32_t ctrlTotal = wifiGraphCtrlCount;
    uint32_t dataTotal = wifiGraphDataCount;

    uint32_t deltaPackets = packetTotal - wifiGraphLastPacketTotal;
    uint32_t deltaBytes = byteTotal - wifiGraphLastByteTotal;
    wifiGraphSampleMgmt = mgmtTotal - wifiGraphLastMgmtTotal;
    wifiGraphSampleCtrl = ctrlTotal - wifiGraphLastCtrlTotal;
    wifiGraphSampleData = dataTotal - wifiGraphLastDataTotal;
    wifiGraphSamplePps = (deltaPackets * 1000UL) / elapsed;
    wifiGraphSampleBytesPerSec = (deltaBytes * 1000UL) / elapsed;

    uint32_t stored = wifiGraphSamplePps;
    if (stored > 999) stored = 999;
    wifiGraphHistory[wifiGraphHistoryCursor] = (uint16_t)stored;
    wifiGraphHistoryCursor = (wifiGraphHistoryCursor + 1) % WIFI_GRAPH_HISTORY_BARS;
    if (wifiGraphHistoryCount < WIFI_GRAPH_HISTORY_BARS) wifiGraphHistoryCount++;

    wifiGraphLastPacketTotal = packetTotal;
    wifiGraphLastByteTotal = byteTotal;
    wifiGraphLastMgmtTotal = mgmtTotal;
    wifiGraphLastCtrlTotal = ctrlTotal;
    wifiGraphLastDataTotal = dataTotal;
    wifiGraphLastSampleMs = now;

    wifiGraphPeakPps = 0;
    for (int i = 0; i < WIFI_GRAPH_HISTORY_BARS; i++) {
        if (wifiGraphHistory[i] > wifiGraphPeakPps) wifiGraphPeakPps = wifiGraphHistory[i];
    }
    return true;
}

static int wifiGraphHistoryIndexForColumn(int column) {
    if (wifiGraphHistoryCount < WIFI_GRAPH_HISTORY_BARS) {
        int firstColumn = WIFI_GRAPH_HISTORY_BARS - wifiGraphHistoryCount;
        if (column < firstColumn) return -1;
        return column - firstColumn;
    }
    return (wifiGraphHistoryCursor + column) % WIFI_GRAPH_HISTORY_BARS;
}

static void wifiGraphDrawChannelStrip() {
    int y = 122;
    for (int ch = WIFI_GRAPH_MIN_CHANNEL; ch <= WIFI_GRAPH_MAX_CHANNEL; ch++) {
        int x = 5 + (ch - WIFI_GRAPH_MIN_CHANNEL) * 18;
        bool selected = (ch == (int)wifiGraphChannel);
        uint16_t color = selected ? CP_YELLOW : CP_DIM;
        if (selected) canvas.fillRect(x, y, 16, 10, color);
        else canvas.drawRect(x, y, 16, 10, color);
        canvas.setTextSize(1);
        canvas.setTextColor(selected ? BLACK : color);
        canvas.setCursor(x + (ch < 10 ? 5 : 2), y + 1);
        canvas.print(ch);
    }
}

void drawWifiGraphScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);

    drawGlitchText("WIFI GRAPH", 72, 4, 1, CP_CYAN, true, true);
    drawTopStatusIcons(132, 1);
    canvas.drawLine(5, 18, 235, 18, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.setCursor(7, 22);
    canvas.print("CH ");
    if ((int)wifiGraphChannel < 10) canvas.print("0");
    canvas.print((int)wifiGraphChannel);
    canvas.setTextColor(wifiGraphActive ? CP_CYAN : CP_RED);
    canvas.setCursor(47, 22);
    canvas.print(wifiGraphStatus.substring(0, 25));

    canvas.setTextColor(WHITE);
    canvas.setCursor(7, 32);
    canvas.print("PKT/s ");
    canvas.print((int)wifiGraphSamplePps);
    canvas.setCursor(79, 32);
    canvas.print("B/s ");
    canvas.print((int)wifiGraphSampleBytesPerSec);
    canvas.setCursor(155, 32);
    canvas.print("RSSI ");
    canvas.print((int)wifiGraphLastRssi);

    const int graphX = 8;
    const int graphY = 44;
    const int graphW = 224;
    const int graphH = 59;
    const int baseY = graphY + graphH;
    drawChippedButton(graphX - 3, graphY - 4, graphW + 6, graphH + 9, CP_DIM);

    uint32_t maxVal = wifiGraphPeakPps;
    if (maxVal < 10) maxVal = 10;
    int step = graphW / WIFI_GRAPH_HISTORY_BARS;
    if (step < 1) step = 1;
    int barW = step - 1;
    if (barW < 1) barW = 1;
    bool historyFull = wifiGraphHistoryCount >= WIFI_GRAPH_HISTORY_BARS;

    uint16_t gridColor = canvas.color565(24, 115, 125);
    for (int i = 1; i <= 3; i++) {
        int gy = graphY + (graphH * i) / 4;
        canvas.drawFastHLine(graphX, gy, graphW, gridColor);
    }
    int gridPhase = historyFull ? wifiGraphHistoryCursor : wifiGraphHistoryCount;
    int gridOffset = (7 - (gridPhase % 7)) % 7;
    for (int col = gridOffset; col < WIFI_GRAPH_HISTORY_BARS; col += 7) {
        int gx = graphX + col * step - 1;
        if (gx >= graphX && gx < graphX + graphW) canvas.drawFastVLine(gx, graphY, graphH, gridColor);
    }

    for (int col = 0; col < WIFI_GRAPH_HISTORY_BARS; col++) {
        int idx = wifiGraphHistoryIndexForColumn(col);
        uint32_t value = (idx >= 0) ? wifiGraphHistory[idx] : 0;
        int barH = (int)((value * (uint32_t)(graphH - 2)) / maxVal);
        if (barH < 1 && value > 0) barH = 1;
        if (barH > graphH - 2) barH = graphH - 2;
        int x = graphX + col * step;
        uint8_t level = (uint8_t)((barH * 255) / (graphH - 2));
        uint8_t red = 34 - (level * 28) / 255;
        uint8_t green = 210 - (level * 170) / 255;
        uint8_t blue = 255 - (level * 95) / 255;
        uint16_t color = canvas.color565(red, green, blue);
        if (barH > 0) canvas.fillRect(x, baseY - barH, barW, barH, color);
        else canvas.drawPixel(x, baseY - 1, CP_DIM);
    }

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(7, 114);
    canvas.print("ARROWS SWITCH CHANNEL   ESC/DEL BACK");
    wifiGraphDrawChannelStrip();

    pushCanvas();
}

void handleWifiGraphInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false, hasBack = status.del;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c == '`') hasBack = true;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        wifiGraphExitToNetworkNode();
        return;
    }

    int nextChannel = (int)wifiGraphChannel;
    if (hasRight || hasUp) nextChannel++;
    if (hasLeft || hasDown) nextChannel--;
    if (nextChannel != (int)wifiGraphChannel) {
        playSound(sound_hover, sound_hover_size);
        wifiGraphSetChannel(nextChannel);
    }
}
