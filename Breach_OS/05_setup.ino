// Device initialization.

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    prefs.begin("breach", false);
    disableSplash = prefs.getBool("disable_splash", false);
    disableBootSound = prefs.getBool(PREF_BOOT_SOUND_OFF, false);

    auto cfg = M5.config();
    if (disableBootSound) {
        cfg.internal_spk = false;
        speakerInitSkippedForBootSound = true;
    }
    M5Cardputer.begin(cfg);
    initSPIFFS();
    M5Cardputer.Display.setRotation(1);
    canvas.createSprite(240, 135);
    resetUsbDriveStateForBoot();
    
    highScore = prefs.getInt("highscore", 0);
    savedSSID = prefs.getString("wifi_ssid", "");
    savedWifiPass = prefs.getString("wifi_pass", "");
    apModeSsid = prefs.getString("ap_ssid", "Breach_OS_AP");
    if (apModeSsid == "") apModeSsid = "Breach_OS_AP";
    if (apModeSsid.length() > 32) apModeSsid = apModeSsid.substring(0, 32);
    apModePass = prefs.getString("ap_pass", "breach123");
    if (apModePass == "") apModePass = "breach123";
    if (apModePass.length() > 63) apModePass = apModePass.substring(0, 63);
    apModeChannel = prefs.getInt("ap_ch", 6);
    if (apModeChannel < 1 || apModeChannel > 13) apModeChannel = 6;
    apModeOpen = prefs.getBool("ap_open", false);
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
    sshRememberMe = prefs.getBool("ssh_remember", false);
    sshHost = sshRememberMe ? prefs.getString("ssh_host", "") : String("");
    sshPort = sshRememberMe ? prefs.getString("ssh_port", "22") : String("22");
    if (sshPort == "") sshPort = "22";
    sshUser = sshRememberMe ? prefs.getString("ssh_user", "") : String("");
    prefs.remove("ssh_pass");
    sshPass = "";
    sshTarget = sshRememberMe ? prefs.getString("ssh_target", "") : String("");
    if (sshTarget == "") {
        sshTarget = (sshUser == "") ? sshHost : sshUser + "@" + sshHost;
    }
    if (sshTarget == "") sshTarget = BREACH_DEFAULT_SSH_TARGET;
    telnetRememberMe = prefs.getBool("telnet_remember", false);
    telnetHost = telnetRememberMe ? prefs.getString("telnet_host", "") : String("");
    telnetPort = telnetRememberMe ? prefs.getString("telnet_port", "23") : String("23");
    if (telnetPort == "") telnetPort = "23";
    telnetTarget = telnetRememberMe ? prefs.getString("telnet_target", "") : String("");
    if (telnetTarget == "") telnetTarget = telnetHost;
    globalVolume = prefs.getInt("volume", 80);
    if (globalVolume > 100) globalVolume = (globalVolume * 100) / 255;
    globalVolume = ((globalVolume + 2) / 5) * 5;
    if (globalVolume > 100) globalVolume = 100;
    if (!speakerInitSkippedForBootSound) {
        M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    }
    
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
    showSplashBootMenu = disableSplash;
    splashBootFocus = 0;
    resetSplashBootScroll();
    drawSplash();
    if (!disableBootSound) {
        playSound(intro_wav, intro_wav_len);
    }
}
