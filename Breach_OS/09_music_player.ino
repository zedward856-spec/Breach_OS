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
                if (name.endsWith(".mp3") || name.endsWith(".MP3") || name.endsWith(".wav") || name.endsWith(".WAV")) {
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

String musicTwoDigits(uint32_t value) {
    if (value < 10) return "0" + String(value);
    return String(value);
}

String formatMusicDuration(uint32_t ms) {
    uint32_t seconds = (ms + 500) / 1000;
    if (seconds < 60) return String(seconds) + "s";

    uint32_t minutes = seconds / 60;
    uint32_t secs = seconds % 60;
    if (minutes < 60) return String(minutes) + "m " + musicTwoDigits(secs) + "s";

    uint32_t hours = minutes / 60;
    minutes %= 60;
    return String(hours) + "h " + musicTwoDigits(minutes) + "m " + musicTwoDigits(secs) + "s";
}

uint32_t getMusicPlaybackElapsedMs() {
    if (!isMp3Playing) return 0;
    uint32_t elapsed = musicPlaybackElapsedMs;
    if (elapsed == 0 && mp3StartTime != 0) elapsed = millis() - mp3StartTime;
    if (musicPlaybackDurationMs > 0 && elapsed > musicPlaybackDurationMs) elapsed = musicPlaybackDurationMs;
    return elapsed;
}

void drawMusicPlaybackProgress() {
    if (!isMp3Playing) return;

    uint32_t elapsed = getMusicPlaybackElapsedMs();
    bool hasDuration = musicPlaybackDurationMs > 0;
    uint32_t filePos = 0;
    uint32_t fileSize = 0;
    if (!hasDuration && audioBuffer) {
        filePos = audioBuffer->getPos();
        fileSize = audioBuffer->getSize();
        if (filePos > fileSize) filePos = fileSize;
    }

    String timeText = formatMusicDuration(elapsed) + " / " + (hasDuration ? formatMusicDuration(musicPlaybackDurationMs) : String("?"));
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString(timeText, 120, 115);

    int barX = 18;
    int barY = 125;
    int barW = 204;
    int barH = 4;
    canvas.drawRect(barX, barY, barW, barH, CP_DIM);
    int fillW = 0;
    if (hasDuration) {
        fillW = (int)((uint64_t)(barW - 2) * elapsed / musicPlaybackDurationMs);
    } else if (fileSize > 0) {
        fillW = (int)((uint64_t)(barW - 2) * filePos / fileSize);
    }
    if (fillW > barW - 2) fillW = barW - 2;
    if (fillW > 0) canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, CP_GREEN);
    if (!hasDuration && fileSize == 0) {
        int scanW = 24;
        int scanRange = barW - 2 - scanW;
        int scanX = barX + 1 + (scanRange > 0 ? (elapsed / 80) % scanRange : 0);
        canvas.fillRect(scanX, barY + 1, scanW, barH - 2, CP_GREEN);
    }
}

static bool musicEnterReleased = false;
static unsigned long lastMusicEnterAcceptMs = 0;
static const unsigned long MUSIC_ENTER_DEBOUNCE_MS = 450;

void updateMusicInputGate(bool enterDown) {
    if (!enterDown) musicEnterReleased = true;
}

bool acceptMusicEnterPress(bool enterDown) {
    updateMusicInputGate(enterDown);
    if (!enterDown || !musicEnterReleased) return false;

    unsigned long now = millis();
    if (now - lastMusicEnterAcceptMs < MUSIC_ENTER_DEBOUNCE_MS) return false;

    musicEnterReleased = false;
    lastMusicEnterAcceptMs = now;
    return true;
}

int musicVisibleRows() {
    return isMp3Playing ? 4 : 5;
}

void syncMusicPlaylistScroll() {
    if (playlist.empty()) {
        playlistFocus = 0;
        playlistScrollOffset = 0;
        return;
    }

    int total = (int)playlist.size();
    int rows = musicVisibleRows();
    if (playlistFocus < 0) playlistFocus = 0;
    if (playlistFocus >= total) playlistFocus = total - 1;
    if (playlistScrollOffset < 0) playlistScrollOffset = 0;
    if (playlistFocus < playlistScrollOffset) {
        playlistScrollOffset = playlistFocus;
    } else if (playlistFocus >= playlistScrollOffset + rows) {
        playlistScrollOffset = playlistFocus - rows + 1;
    }
}

void moveMusicPlaylistFocus(int delta) {
    if (playlist.empty()) return;

    int total = (int)playlist.size();
    playlistFocus = (playlistFocus + delta + total) % total;
    syncMusicPlaylistScroll();
}

void drawMusicPlayer() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- NEON WAVE AUDIO PLAYER ---", 120, 12);
    canvas.drawLine(10, 22, 230, 22, CP_CYAN);
    
    if (playlist.empty()) {
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("NO AUDIO TRACKS FOUND", 120, 52);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("DIR: " + musicDir, 120, 72);
    } else {
        int startY = 26;
        int maxPlayDisplay = musicVisibleRows();
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
            
            if (isCurrent && mp3IsPaused) {
                canvas.setCursor(160, rowY + 2);
                canvas.setTextColor(CP_GREEN);
                canvas.print("[PAUSED]");
            }
        }
    }
    
    int dividerY = isMp3Playing ? 83 : 94;
    canvas.drawLine(10, dividerY, 230, dividerY, CP_CYAN);
    
    if (isMp3Playing) {
        drawAudioSpectrum(48, 106, 144, 8);
        drawMusicPlaybackProgress();
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
    
    bool canNavigate = !isMp3Playing || mp3IsPaused;
    if (canNavigate && (hasUp || hasLeft) && !playlist.empty()) {
        playSound(sound_hover, sound_hover_size);
        moveMusicPlaylistFocus(-1);
        drawMusicPlayer();
        return;
    }
    if (canNavigate && (hasDown || hasRight) && !playlist.empty()) {
        playSound(sound_hover, sound_hover_size);
        moveMusicPlaylistFocus(1);
        drawMusicPlayer();
        return;
    }
    
    if (acceptMusicEnterPress(status.enter) && !playlist.empty()) {
        playSound(sound_select, sound_select_size);
        if (isMp3Playing) {
            if (mp3IsPaused && playlist[playlistFocus] != mp3Filename) {
                String nextFile = playlist[playlistFocus];
                stopMp3Playback();
                startMp3InPlayer(nextFile);
            } else {
                toggleMusicPlaybackPause();
            }
        } else {
            startMp3InPlayer(playlist[playlistFocus]);
        }
    }
}

void toggleMusicPlaybackPause() {
    if (!isMp3Playing) return;

    if (mp3IsPaused) {
        mp3IsPaused = false;
        if (mp3PausedTime != 0 && musicPlaybackElapsedMs == 0) {
            mp3StartTime += millis() - mp3PausedTime;
        }
        mp3PausedTime = 0;
    } else {
        mp3IsPaused = true;
        mp3PausedTime = millis();
        M5Cardputer.Speaker.stop();
    }
    drawMusicPlayer();
}

void playNextTrack() {
    if (playlist.empty()) return;
    stopMp3Playback();
    
    if (mp3PlayLoopMode == "random") {
        playlistFocus = random(0, playlist.size());
    } else {
        playlistFocus = (playlistFocus + 1) % playlist.size();
    }
    
    syncMusicPlaylistScroll();
    
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
    
    syncMusicPlaylistScroll();
    
    startMp3InPlayer(playlist[playlistFocus]);
}

uint16_t readWavLe16(const uint8_t* data, int offset) {
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

uint32_t readWavLe32(const uint8_t* data, int offset) {
    return (uint32_t)data[offset] |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}

uint32_t calcPcmWavDurationMs(uint32_t dataBytes, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    if (dataBytes == 0 || sampleRate == 0 || channels == 0 || bitsPerSample == 0) return 0;
    uint32_t bytesPerSample = bitsPerSample / 8;
    if (bytesPerSample == 0) return 0;
    uint32_t bytesPerSecond = sampleRate * channels * bytesPerSample;
    if (bytesPerSecond == 0) return 0;
    uint64_t durationMs = ((uint64_t)dataBytes * 1000ULL + bytesPerSecond - 1) / bytesPerSecond;
    if (durationMs > 0xFFFFFFFFULL) return 0xFFFFFFFFUL;
    return (uint32_t)durationMs;
}

uint32_t probePcmWavDurationMs(String fullPath, bool useSD) {
    File wavFile;
    if (useSD) {
        wavFile = SD.open(fullPath.c_str());
    } else {
        wavFile = SPIFFS.open(fullPath.c_str());
    }
    if (!wavFile) return 0;

    uint8_t header[44];
    if (wavFile.read(header, sizeof(header)) != sizeof(header)) {
        wavFile.close();
        return 0;
    }
    wavFile.close();

    bool riff = header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F';
    bool waveFmt = header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E';
    bool dataTag = header[36] == 'd' && header[37] == 'a' && header[38] == 't' && header[39] == 'a';
    uint16_t audioFormat = readWavLe16(header, 20);
    uint16_t channels = readWavLe16(header, 22);
    uint32_t sampleRate = readWavLe32(header, 24);
    uint16_t bitsPerSample = readWavLe16(header, 34);
    uint32_t dataBytes = readWavLe32(header, 40);
    if (!riff || !waveFmt || !dataTag || audioFormat != 1 || channels == 0 || sampleRate == 0 || bitsPerSample == 0) return 0;

    return calcPcmWavDurationMs(dataBytes, sampleRate, channels, bitsPerSample);
}

bool playDirectPcmWavFromSD(String fullPath, String fileName) {
    File wavFile = SD.open(fullPath.c_str());
    if (!wavFile) return false;

    uint8_t header[44];
    if (wavFile.read(header, sizeof(header)) != sizeof(header)) {
        wavFile.close();
        return false;
    }

    bool riff = header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F';
    bool waveFmt = header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E';
    bool dataTag = header[36] == 'd' && header[37] == 'a' && header[38] == 't' && header[39] == 'a';
    uint16_t audioFormat = readWavLe16(header, 20);
    uint16_t channels = readWavLe16(header, 22);
    uint32_t sampleRate = readWavLe32(header, 24);
    uint16_t bitsPerSample = readWavLe16(header, 34);
    uint32_t dataRemaining = readWavLe32(header, 40);

    if (!riff || !waveFmt || !dataTag || audioFormat != 1 || bitsPerSample != 16 || channels < 1 || channels > 2 || sampleRate == 0) {
        wavFile.close();
        return false;
    }

    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    resetAudioSpectrum();

    isMp3Playing = true;
    mp3Filename = fileName;
    mp3IsPaused = false;
    mp3StartTime = millis();
    mp3PausedTime = 0;
    uint32_t wavDataBytes = dataRemaining;
    uint32_t wavBytesPerSecond = sampleRate * channels * sizeof(int16_t);
    musicPlaybackDurationMs = calcPcmWavDurationMs(wavDataBytes, sampleRate, channels, bitsPerSample);
    musicPlaybackElapsedMs = 0;
    mp3DurationSeconds = (musicPlaybackDurationMs + 999) / 1000;
    drawMusicPlayer();

    static int16_t wavPlayBuffer[1024];
    bool stopped = false;
    bool switchTrack = false;
    String nextFile = "";
    unsigned long lastDraw = 0;
    unsigned long lastNavMs = 0;
    while (dataRemaining > 0 && wavFile.available() && !stopped) {
        M5Cardputer.update();
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        bool hasPrev = false;
        bool hasNext = false;
        if (status.del) stopped = true;
        for (char c : status.word) {
            if (c == '`') stopped = true;
            if (c == ';' || c == ',') hasPrev = true;
            if (c == '.' || c == '/') hasNext = true;
        }
        if (stopped) break;

        unsigned long now = millis();
        if (mp3IsPaused && !playlist.empty() && (hasPrev || hasNext) && now - lastNavMs >= 180) {
            playSound(sound_hover, sound_hover_size);
            moveMusicPlaylistFocus(hasPrev ? -1 : 1);
            lastNavMs = now;
            drawMusicPlayer();
        }

        if (acceptMusicEnterPress(status.enter)) {
            if (mp3IsPaused && !playlist.empty() && playlist[playlistFocus] != mp3Filename) {
                nextFile = playlist[playlistFocus];
                switchTrack = true;
                stopped = true;
                break;
            }
            toggleMusicPlaybackPause();
        }

        if (mp3IsPaused) {
            delay(10);
            now = millis();
            if (now - lastDraw >= 150) {
                lastDraw = now;
                drawMusicPlayer();
            }
            continue;
        }

        size_t bytesToRead = dataRemaining > sizeof(wavPlayBuffer) ? sizeof(wavPlayBuffer) : (size_t)dataRemaining;
        bytesToRead &= ~1U;
        if (bytesToRead == 0) break;

        size_t bytesRead = wavFile.read((uint8_t*)wavPlayBuffer, bytesToRead);
        if (bytesRead == 0) break;
        bytesRead &= ~1U;
        size_t sampleCount = bytesRead / sizeof(int16_t);
        dataRemaining -= bytesRead;
        musicPlaybackElapsedMs = (uint32_t)(((uint64_t)(wavDataBytes - dataRemaining) * 1000ULL) / wavBytesPerSecond);

        feedAudioSpectrumBuffer(wavPlayBuffer, sampleCount);
        M5Cardputer.Speaker.playRaw(wavPlayBuffer, sampleCount, sampleRate, channels == 2, 1, 0);
        while (M5Cardputer.Speaker.isPlaying() && !stopped) {
            delay(1);
            M5Cardputer.update();
            Keyboard_Class::KeysState playStatus = M5Cardputer.Keyboard.keysState();
            if (playStatus.del) stopped = true;
            for (char c : playStatus.word) {
                if (c == '`') stopped = true;
            }
            if (acceptMusicEnterPress(playStatus.enter)) {
                toggleMusicPlaybackPause();
                break;
            }
        }

        now = millis();
        if (now - lastDraw >= 100) {
            lastDraw = now;
            drawMusicPlayer();
        }
    }

    M5Cardputer.Speaker.stop();
    wavFile.close();
    isMp3Playing = false;
    mp3IsPaused = false;
    mp3PausedTime = 0;
    musicPlaybackDurationMs = 0;
    musicPlaybackElapsedMs = 0;
    drawMusicPlayer();
    if (switchTrack && nextFile.length() > 0) {
        startMp3InPlayer(nextFile);
    }
    return true;
}

void startMp3InPlayer(String fileName) {
    String fullPath = musicDir + (musicDir == "/" ? "" : "/") + fileName;
    String lowerName = fileName;
    lowerName.toLowerCase();
    resetAudioSpectrum();
    bool useSD = isSDCardManager;
    if (useSD && lowerName.endsWith(".wav")) {
        SPI.begin(40, 39, 14, 12);
        if (SD.begin(12, SPI, 20000000) && playDirectPcmWavFromSD(fullPath, fileName)) {
            return;
        }
        resetAudioSpectrum();
    }

    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    audioOut = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);

    bool started = false;
    uint32_t probedDurationMs = 0;
    
    if (useSD) {
        SPI.begin(40, 39, 14, 12);
        SD.begin(12, SPI, 20000000);
        if (lowerName.endsWith(".wav")) probedDurationMs = probePcmWavDurationMs(fullPath, true);
        fileSD = new AudioFileSourceSD(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSD, 8192);
        if (lowerName.endsWith(".wav")) {
            wav = new AudioGeneratorWAV();
            started = wav->begin(audioBuffer, audioOut);
        } else {
            mp3 = new AudioGeneratorMP3();
            started = mp3->begin(audioBuffer, audioOut);
        }
    } else {
        if (lowerName.endsWith(".wav")) probedDurationMs = probePcmWavDurationMs(fullPath, false);
        fileSPIFFS = new AudioFileSourceSPIFFS(fullPath.c_str());
        audioBuffer = new AudioFileSourceBuffer(fileSPIFFS, 8192);
        if (lowerName.endsWith(".wav")) {
            wav = new AudioGeneratorWAV();
            started = wav->begin(audioBuffer, audioOut);
        } else {
            mp3 = new AudioGeneratorMP3();
            started = mp3->begin(audioBuffer, audioOut);
        }
    }
    
    if (started) {
        isMp3Playing = true;
        mp3Filename = fileName;
        mp3IsPaused = false;
        mp3StartTime = millis();
        mp3PausedTime = 0;
        mp3DurationSeconds = probedDurationMs > 0 ? (probedDurationMs + 999) / 1000 : 0;
        musicPlaybackDurationMs = probedDurationMs;
        musicPlaybackElapsedMs = 0;
    } else {
        if (mp3) { delete mp3; mp3 = nullptr; }
        if (wav) { delete wav; wav = nullptr; }
        if (audioBuffer) { delete audioBuffer; audioBuffer = nullptr; }
        if (fileSD) { delete fileSD; fileSD = nullptr; }
        if (fileSPIFFS) { delete fileSPIFFS; fileSPIFFS = nullptr; }
        if (audioOut) { delete audioOut; audioOut = nullptr; }
        isMp3Playing = false;
        musicPlaybackDurationMs = 0;
        musicPlaybackElapsedMs = 0;
    }
    drawMusicPlayer();
}






