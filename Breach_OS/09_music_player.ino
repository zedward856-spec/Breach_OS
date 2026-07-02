// SD-card playlist and music player UI.

void populatePlaylist() {
    playlist.clear();
    isSDCardManager = true;
    bool useSD = true;
    
    File dir;
    if (useSD) {
        SPI.begin(40, 39, 14, 12);
        SD.begin(12, SPI, 20000000);
        dir = SD.open(musicDir.c_str());
    } else {
        dir = SPIFFS.open(musicDir.c_str());
    }
    
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = String(file.name());
                int lastSlash = name.lastIndexOf('/');
                if (lastSlash >= 0) {
                    name = name.substring(lastSlash + 1);
                }
                if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
                    playlist.push_back(name);
                }
            }
            file = dir.openNextFile();
        }
        dir.close();
    }
    
    std::sort(playlist.begin(), playlist.end());
}

void drawDirConfirmPopup() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 15, 230, 95, CP_CYAN);
    canvas.drawRect(7, 17, 226, 91, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- SET MUSIC DIRECTORY ---", 120, 25);
    
    canvas.setTextColor(WHITE);
    String displayPath = pendingSelectedDir;
    if (displayPath.length() > 30) displayPath = "..." + displayPath.substring(displayPath.length() - 26);
    canvas.drawCenterString(displayPath, 120, 48);
    
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString("ENTER: YES (CONFIRM SETTING)", 120, 72);
    canvas.drawCenterString("ESC/DEL: NO (ENTER FOLDER)", 120, 88);
    
    pushCanvas();
}

void handleDirConfirmPopupInput(Keyboard_Class::KeysState status) {
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == '`') hasEsc = true;
    }
    
    if (hasEsc || status.del) {
        playSound(sound_select, sound_select_size);
        // NO -> enter the folder!
        fileManagerCurrentPath = pendingSelectedDir;
        fileManagerSelected = 0;
        fileManagerScrollOffset = 0;
        appState = STATE_FILE_MANAGER;
        populateFileList();
        drawFileManager();
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        // YES -> Confirm selection!
        musicDir = pendingSelectedDir;
        prefs.putString("music_dir", musicDir);
        isDirSelectionMode = false;
        appState = STATE_HARDWARE_SETTINGS;
        drawHardwareSettings();
    }
}

void drawMusicPlayer() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- NEON WAVE MP3 PLAYER ---", 120, 12);
    canvas.drawLine(10, 22, 230, 22, CP_CYAN);
    
    if (playlist.empty()) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NO MP3 TRACKS FOUND", 120, 52);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("DIR: " + musicDir, 120, 72);
    } else {
        int startY = 26;
        int maxPlayDisplay = 5;
        for (int i = 0; i < maxPlayDisplay; i++) {
            int idx = playlistScrollOffset + i;
            if (idx >= (int)playlist.size()) break;
            
            bool isFocus = (idx == playlistFocus);
            bool isCurrent = (isMp3Playing && playlist[idx] == mp3Filename);
            int rowY = startY + i * 13;
            
            if (isFocus) {
                canvas.fillRect(15, rowY, 210, 13, canvas.color565(30, 30, 30));
                canvas.drawRect(15, rowY, 210, 13, CP_YELLOW);
                canvas.setTextColor(CP_CYAN);
            } else {
                canvas.setTextColor(isCurrent ? CP_YELLOW : WHITE);
            }
            
            canvas.setCursor(20, rowY + 2);
            String nameToPrint = playlist[idx];
            if (nameToPrint.length() > 22) nameToPrint = nameToPrint.substring(0, 19) + "...";
            canvas.print(nameToPrint);
            
            if (isCurrent) {
                canvas.setCursor(160, rowY + 2);
                canvas.setTextColor(CP_GREEN);
                canvas.print("[PLAYING]");
            }
        }
    }
    
    canvas.drawLine(10, 94, 230, 94, CP_CYAN);
    
    if (isMp3Playing) {
        // Draw visualizer
        int startX = 60;
        for (int i = 0; i < 10; i++) {
            int h = random(2, 11);
            canvas.fillRect(startX + i * 11, 108 - h, 7, h, CP_CYAN);
            canvas.drawRect(startX + i * 11, 108 - h, 7, h, CP_YELLOW);
        }
        
        String playText = "PLAYING: " + mp3Filename;
        if (playText.length() > 36) playText = playText.substring(0, 33) + "...";
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString(playText, 120, 113);
    } else {
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("STOPPED | ESC: BACK TO HW NODE", 120, 113);
    }
    
    pushCanvas();
}

void handleMusicPlayerInput(Keyboard_Class::KeysState status) {
    bool hasEsc = false;
    for (char c : status.word) {
        if (c == '`') hasEsc = true;
    }
    
    if (hasEsc || status.del) {
        playSound(sound_select, sound_select_size);
        stopMp3Playback();
        appState = STATE_HARDWARE_MENU;
        drawHardwareMenu();
        return;
    }
    
    bool hasUp = false, hasDown = false;
    bool hasLeft = false, hasRight = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
    }
    
    if (hasUp && !playlist.empty()) {
        playSound(sound_hover, sound_hover_size);
        playlistFocus = (playlistFocus - 1 + playlist.size()) % playlist.size();
        if (playlistFocus < playlistScrollOffset) {
            playlistScrollOffset = playlistFocus;
        } else if (playlistFocus >= playlistScrollOffset + 5) {
            playlistScrollOffset = playlistFocus - 4;
        }
        drawMusicPlayer();
    } else if (hasDown && !playlist.empty()) {
        playSound(sound_select, sound_select_size);
        stopMp3Playback();
        playlistFocus = (playlistFocus + 1) % playlist.size();
        if (playlistFocus < playlistScrollOffset) {
            playlistScrollOffset = playlistFocus;
        } else if (playlistFocus >= playlistScrollOffset + 5) {
            playlistScrollOffset = playlistFocus - 4;
        }
        startMp3InPlayer(playlist[playlistFocus]);
        return;
    }
    
    if (hasLeft && !playlist.empty()) {
        playSound(sound_select, sound_select_size);
        playPrevTrack();
    } else if (hasRight && !playlist.empty()) {
        playSound(sound_select, sound_select_size);
        playNextTrack();
    }
    
    if (status.enter && !playlist.empty()) {
        playSound(sound_select, sound_select_size);
        if (isMp3Playing && playlist[playlistFocus] == mp3Filename) {
            stopMp3Playback();
            drawMusicPlayer();
        } else {
            startMp3InPlayer(playlist[playlistFocus]);
        }
    }
}

void playNextTrack() {
    if (playlist.empty()) return;
    stopMp3Playback();
    
    if (mp3PlayLoopMode == "random") {
        playlistFocus = random(0, playlist.size());
    } else {
        playlistFocus = (playlistFocus + 1) % playlist.size();
    }
    
    if (playlistFocus < playlistScrollOffset) {
        playlistScrollOffset = playlistFocus;
    } else if (playlistFocus >= playlistScrollOffset + 5) {
        playlistScrollOffset = playlistFocus - 4;
    }
    
    startMp3InPlayer(playlist[playlistFocus]);
}

void playPrevTrack() {
    if (playlist.empty()) return;
    stopMp3Playback();
    
    if (mp3PlayLoopMode == "random") {
        playlistFocus = random(0, playlist.size());
    } else {
        playlistFocus = (playlistFocus - 1 + playlist.size()) % playlist.size();
    }
    
    if (playlistFocus < playlistScrollOffset) {
        playlistScrollOffset = playlistFocus;
    } else if (playlistFocus >= playlistScrollOffset + 5) {
        playlistScrollOffset = playlistFocus - 4;
    }
    
    startMp3InPlayer(playlist[playlistFocus]);
}

void startMp3InPlayer(String fileName) {
    String fullPath = musicDir + (musicDir == "/" ? "" : "/") + fileName;
    audioOut = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);
    
    bool useSD = isSDCardManager;
    bool started = false;
    
    if (useSD) {
        SPI.begin(40, 39, 14, 12);
        SD.begin(12, SPI, 20000000);
        fileSD = new AudioFileSourceSD(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSD, 8192);
        mp3 = new AudioGeneratorMP3();
        started = mp3->begin(audioBuffer, audioOut);
    } else {
        fileSPIFFS = new AudioFileSourceSPIFFS(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSPIFFS, 8192);
        mp3 = new AudioGeneratorMP3();
        started = mp3->begin(audioBuffer, audioOut);
    }
    
    if (started) {
        isMp3Playing = true;
        mp3Filename = fileName;
        mp3IsPaused = false;
        mp3StartTime = millis();
        mp3PausedTime = 0;
        mp3DurationSeconds = 180;
    } else {
        if (mp3) { delete mp3; mp3 = nullptr; }
        if (audioBuffer) { delete audioBuffer; audioBuffer = nullptr; }
        if (fileSD) { delete fileSD; fileSD = nullptr; }
        if (fileSPIFFS) { delete fileSPIFFS; fileSPIFFS = nullptr; }
        if (audioOut) { delete audioOut; audioOut = nullptr; }
        isMp3Playing = false;
    }
    drawMusicPlayer();
}






