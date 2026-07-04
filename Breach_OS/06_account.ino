// Account profile screen and account update input handling.

void returnFromAccountMenu() {
    accountStatsFetched = false;
    if (accountReturnToBreach) {
        accountReturnToBreach = false;
        returnToBreachMode();
    } else {
        enterMainMenu();
    }
}

void drawAccountMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    canvas.setTextColor(CP_CYAN);
    canvas.setTextSize(2);
    canvas.drawCenterString("OPERATIVE PROFILE", 120, 10);
    
    if (!accountStatsFetched) {
        canvas.setTextSize(1);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("FETCHING DATA...", 120, 40);
        pushCanvas();
        if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
        HTTPClient http;
        String url = String(API_URL) + "/account";
        if (url.startsWith("https")) http.begin(secureClient, url);
        else http.begin(url);
        
        http.addHeader("Content-Type", "application/json");
        String payload = "{\"action\":\"get_stats\",\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\"}";
        int httpCode = http.POST(payload);
        if (httpCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getString());
            accountHighScore = doc["highScore"].as<int>();
            accountRank = doc["rank"].as<int>();
            accountHighGrid = doc["grid"].as<int>();
            accountHighPhase = doc["phase"].as<int>();
        }
        http.end();
        accountStatsFetched = true;
        newAccountName = authUser;
        newAccountPass = authPass;
        
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        canvas.setTextColor(CP_CYAN);
        canvas.setTextSize(2);
        canvas.drawCenterString("OPERATIVE PROFILE", 120, 10);
    }
    
    canvas.setTextSize(1);
    canvas.setTextColor(WHITE);
    canvas.setCursor(10, 35);
    canvas.print("NAME: ");
    canvas.setTextColor(CP_CYAN);
    canvas.print(authUser);
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(10, 50);
    canvas.print("HIGHSCORE: ");
    canvas.setTextColor(CP_YELLOW);
    if (accountRank > 0) {
        canvas.print(String(accountHighScore) + " (" + String(accountHighGrid) + "x" + String(accountHighGrid) + " P" + String(accountHighPhase) + ") R:#" + String(accountRank));
    } else {
        canvas.print("NO DATA (RANK: N/A)");
    }
    
    uint16_t c0 = (accountFocus == 0) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c0);
    drawGlitchText("> NAME:   " + newAccountName + (accountFocus == 0 && blinkState ? "_" : ""), 10, 60, 1, c0, false);
    
    uint16_t c1 = (accountFocus == 1) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c1);
    String stars = "";
    for (int i=0; i<newAccountPass.length(); i++) stars += "*";
    drawGlitchText("> PASS:   " + stars + (accountFocus == 1 && blinkState ? "_" : ""), 10, 75, 1, c1, false);
    
    uint16_t c2 = (accountFocus == 2) ? CP_YELLOW : WHITE;
    canvas.setTextColor(c2);
    String modeLabel = "";
    if (insaneMode == 0) modeLabel = "[OFF]";
    else if (insaneMode == 1) modeLabel = "[ON]";
    else modeLabel = "[INSANE]";
    drawGlitchText("> GLITCH TEXT: " + modeLabel, 10, 90, 1, c2, false);

    uint16_t c3 = (accountFocus == 3) ? CP_YELLOW : WHITE;
    drawChippedButton(10, 110, 100, 20, c3);
    canvas.setTextColor(c3);
    drawGlitchText("UPDATE", 60, 115, 1, c3);
    
    uint16_t c4 = (accountFocus == 4) ? CP_YELLOW : WHITE;
    drawChippedButton(130, 110, 100, 20, c4);
    canvas.setTextColor(c4);
    drawGlitchText("BACK", 180, 115, 1, c4);
    
    pushCanvas();
}

void handleAccountInput(Keyboard_Class::KeysState status) {
    if (status.del) {
        if (accountFocus == 0 && newAccountName.length() > 0) newAccountName.remove(newAccountName.length()-1);
        if (accountFocus == 1 && newAccountPass.length() > 0) newAccountPass.remove(newAccountPass.length()-1);
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (accountFocus == 2) {
            insaneMode = (insaneMode + 1) % 3;
            prefs.putInt("insane", insaneMode);
            drawAccountMenu();
            return;
        } else if (accountFocus == 3) {
            if (newAccountName == authUser && newAccountPass == authPass) {
                accountFocus = 4;
                return;
            }
            if (newAccountName == "") return;
            drawMessage("UPDATING...");
            if (!secureClientInit) { secureClient.setInsecure(); secureClientInit = true; }
            HTTPClient http;
            String url = String(API_URL) + "/account";
            if (url.startsWith("https")) http.begin(secureClient, url);
            else http.begin(url);
            
            http.addHeader("Content-Type", "application/json");
            String payload = "{\"action\":\"update_account\",\"username\":\"" + authUser + "\",\"password\":\"" + authPass + "\",\"newUsername\":\"" + newAccountName + "\",\"newPassword\":\"" + newAccountPass + "\"}";
            int httpCode = http.POST(payload);
            http.end();
            
            if (httpCode == 200) {
                authUser = newAccountName;
                authPass = newAccountPass;
                drawMessage("UPDATE SUCCESS!");
            } else {
                drawMessage("UPDATE FAILED!");
            }
            delay(1500);
            returnFromAccountMenu();
            return;
        } else if (accountFocus == 4) {
            returnFromAccountMenu();
            return;
        }
        accountFocus++;
        if (accountFocus > 4) accountFocus = 0;
        return;
    }
    
    bool hasUp = false, hasDown = false, hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c >= 32 && c <= 126 && c != ';' && c != '.' && c != ',' && c != '/') {
            if (accountFocus == 0 && newAccountName.length() < 16) newAccountName += c;
            if (accountFocus == 1 && newAccountPass.length() < 16) newAccountPass += c;
        }
    }
    
    if (hasUp) { 
        playSound(sound_hover, sound_hover_size); 
        if (accountFocus == 3 || accountFocus == 4) accountFocus = 2;
        else if (accountFocus == 2) accountFocus = 1;
        else if (accountFocus == 1) accountFocus = 0;
        else if (accountFocus == 0) accountFocus = 3; 
    }
    if (hasDown) { 
        playSound(sound_hover, sound_hover_size); 
        if (accountFocus == 0) accountFocus = 1;
        else if (accountFocus == 1) accountFocus = 2;
        else if (accountFocus == 2) accountFocus = 3;
        else if (accountFocus == 3 || accountFocus == 4) accountFocus = 0;
    }
    if (hasLeft || hasRight) {
        if (accountFocus == 3) { accountFocus = 4; playSound(sound_hover, sound_hover_size); }
        else if (accountFocus == 4) { accountFocus = 3; playSound(sound_hover, sound_hover_size); }
    }
}
