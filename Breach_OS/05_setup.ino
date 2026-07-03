// Device initialization.

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    initSPIFFS();
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(240, 135);
    
    prefs.begin("breach", false);
    highScore = prefs.getInt("highscore", 0);
    savedSSID = prefs.getString("wifi_ssid", "");
    savedWifiPass = prefs.getString("wifi_pass", "");
    mp3PlayLoopMode = prefs.getString("loopMode", "name");
    if (mp3PlayLoopMode != "name" && mp3PlayLoopMode != "random") {
        mp3PlayLoopMode = "name";
    }
    musicDir = prefs.getString("music_dir", "/");
    if (musicDir == "") {
        musicDir = "/";
    }
    if (prefs.isKey("insane")) {
        insaneMode = prefs.getInt("insane", -1);
        if (insaneMode == -1) {
            insaneMode = prefs.getBool("insane", false) ? 2 : 0;
            prefs.remove("insane");
            prefs.putInt("insane", insaneMode);
        }
    } else {
        insaneMode = 0;
    }
    authUser = prefs.getString("user", "");
    authPass = prefs.getString("pass", "");
    sshHost = prefs.getString("ssh_host", "");
    sshPort = prefs.getString("ssh_port", "22");
    if (sshPort == "") sshPort = "22";
    sshUser = prefs.getString("ssh_user", "");
    sshPass = prefs.getString("ssh_pass", "");
    sshTarget = prefs.getString("ssh_target", "");
    if (sshTarget == "") {
        sshTarget = (sshUser == "") ? sshHost : sshUser + "@" + sshHost;
    }
    globalVolume = prefs.getInt("volume", 80);
    if (globalVolume > 100) globalVolume = (globalVolume * 100) / 255;
    globalVolume = ((globalVolume + 2) / 5) * 5;
    if (globalVolume > 100) globalVolume = 100;
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    
    globalBrightness = prefs.getInt("brightness", 80);
    if (globalBrightness > 100) globalBrightness = (globalBrightness * 100) / 255;
    globalBrightness = ((globalBrightness + 2) / 5) * 5;
    if (globalBrightness > 100) globalBrightness = 100;
    if (globalBrightness < 5) globalBrightness = 5;
    M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
    
    if (authUser != "") rememberMe = true;
    
    if (savedSSID != "") {
        WiFi.begin(savedSSID.c_str(), savedWifiPass.c_str());
    }
    
    appState = STATE_SPLASH;
    drawSplash();
    playSound(intro_wav, intro_wav_len);
}
