// SD-card playlist and music player UI.

static constexpr uint16_t MUSIC_MP3_BUFFER_BYTES = 4096;
static constexpr uint16_t MUSIC_WAV_BUFFER_BYTES = 8192;
static constexpr uint32_t MUSIC_SD_SPI_HZ = 25000000;
static const char* MUSIC_PLAYLIST_CACHE_HEADER = "BREACH_MUSIC_CACHE_V1";
static const char* MUSIC_PLAYLIST_CACHE_DIR = "/Breach_OS/cache";
static const char* MUSIC_PLAYLIST_CACHE_FILE = "/Breach_OS/cache/music_playlist.txt";
static const char* MUSIC_PLAYLIST_SPIFFS_CACHE_FILE = "/music_playlist_cache.txt";
static constexpr size_t MUSIC_MP3_DURATION_PROBE_BYTES = 512;
static bool musicSdReady = false;
static bool musicDurationProbePending = false;
static String musicDurationProbePath = "";
static bool musicDurationProbeUseSD = true;
static uint32_t musicDurationProbeAudioOffset = 0;

bool ensureMusicSdReady() {
    if (musicSdReady && SD.cardType() != CARD_NONE) return true;
    SPI.begin(40, 39, 14, 12);
    musicSdReady = SD.begin(12, SPI, MUSIC_SD_SPI_HZ) && SD.cardType() != CARD_NONE;
    return musicSdReady;
}

bool isMusicPlaylistFileName(String name) {
    name.toLowerCase();
    return name.endsWith(".mp3") || name.endsWith(".wav");
}

uint32_t readMusicMp3Synchsafe(const uint8_t* data) {
    return ((uint32_t)(data[0] & 0x7F) << 21) |
           ((uint32_t)(data[1] & 0x7F) << 14) |
           ((uint32_t)(data[2] & 0x7F) << 7) |
           (uint32_t)(data[3] & 0x7F);
}

uint32_t getMusicMp3AudioStartOffset(String fullPath, bool useSD) {
    File mp3File = useSD ? SD.open(fullPath.c_str(), FILE_READ) : SPIFFS.open(fullPath.c_str(), FILE_READ);
    if (!mp3File) return 0;

    uint32_t fileSize = mp3File.size();
    uint8_t header[10];
    size_t bytesRead = mp3File.read(header, sizeof(header));
    mp3File.close();
    if (bytesRead != sizeof(header)) return 0;
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') return 0;

    uint32_t audioOffset = 10 + readMusicMp3Synchsafe(header + 6);
    if (header[5] & 0x10) audioOffset += 10;
    return audioOffset < fileSize ? audioOffset : 0;
}

uint32_t readMusicMp3Be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

bool musicMp3HeaderValid(uint32_t header) {
    int version = (header >> 19) & 0x03;
    int layer = (header >> 17) & 0x03;
    int bitrate = (header >> 12) & 0x0F;
    int sampleRate = (header >> 10) & 0x03;
    return (header & 0xFFE00000UL) == 0xFFE00000UL && version != 1 && layer != 0 && bitrate != 0 && bitrate != 15 && sampleRate != 3;
}

uint16_t musicMp3HeaderBitrateKbps(uint32_t header) {
    static const uint16_t mpeg1[3][16] = {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}
    };
    static const uint16_t mpeg2[3][16] = {
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}
    };
    int version = (header >> 19) & 0x03;
    int layer = (header >> 17) & 0x03;
    int index = (header >> 12) & 0x0F;
    int row = 3 - layer;
    if (row < 0 || row > 2) return 0;
    return version == 3 ? mpeg1[row][index] : mpeg2[row][index];
}

uint32_t musicMp3HeaderSampleRateHz(uint32_t header) {
    static const uint32_t rates[3] = {44100, 48000, 32000};
    int version = (header >> 19) & 0x03;
    int index = (header >> 10) & 0x03;
    if (index >= 3 || version == 1) return 0;
    uint32_t rate = rates[index];
    if (version == 2) rate /= 2;
    if (version == 0) rate /= 4;
    return rate;
}

uint16_t musicMp3HeaderSamplesPerFrame(uint32_t header) {
    int version = (header >> 19) & 0x03;
    int layer = (header >> 17) & 0x03;
    if (layer == 3) return 384;
    if (layer == 1 && version != 3) return 576;
    return 1152;
}

uint32_t probeMusicMp3DurationMs(String fullPath, bool useSD, uint32_t audioOffset) {
    File mp3File = useSD ? SD.open(fullPath.c_str(), FILE_READ) : SPIFFS.open(fullPath.c_str(), FILE_READ);
    if (!mp3File) return 0;

    uint32_t fileSize = mp3File.size();
    if (audioOffset >= fileSize) audioOffset = 0;
    if (!mp3File.seek(audioOffset)) {
        mp3File.close();
        return 0;
    }

    uint8_t probe[MUSIC_MP3_DURATION_PROBE_BYTES];
    size_t probeLen = mp3File.read(probe, sizeof(probe));
    int frameIndex = -1;
    uint32_t header = 0;
    for (size_t i = 0; i < probeLen; i++) {
        header = (header << 8) | probe[i];
        if (i >= 3 && musicMp3HeaderValid(header)) {
            frameIndex = (int)i - 3;
            break;
        }
    }
    if (frameIndex < 0) {
        mp3File.close();
        return 0;
    }

    uint16_t bitrateKbps = musicMp3HeaderBitrateKbps(header);
    uint32_t sampleRate = musicMp3HeaderSampleRateHz(header);
    uint16_t samplesPerFrame = musicMp3HeaderSamplesPerFrame(header);
    if (bitrateKbps == 0 || sampleRate == 0 || samplesPerFrame == 0) {
        mp3File.close();
        return 0;
    }

    for (size_t i = frameIndex + 4; i + 12 <= probeLen; i++) {
        bool xing = probe[i] == 'X' && probe[i + 1] == 'i' && probe[i + 2] == 'n' && probe[i + 3] == 'g';
        bool info = probe[i] == 'I' && probe[i + 1] == 'n' && probe[i + 2] == 'f' && probe[i + 3] == 'o';
        if (xing || info) {
            uint32_t flags = readMusicMp3Be32(probe + i + 4);
            if (flags & 0x01) {
                uint32_t frames = readMusicMp3Be32(probe + i + 8);
                mp3File.close();
                if (frames > 0) {
                    return (uint32_t)(((uint64_t)frames * samplesPerFrame * 1000ULL + sampleRate - 1) / sampleRate);
                }
                return 0;
            }
            break;
        }
    }

    uint32_t frameOffset = audioOffset + frameIndex;
    uint32_t audioBytes = fileSize > frameOffset ? fileSize - frameOffset : 0;
    if (audioBytes > 128 && fileSize > frameOffset + 128) {
        uint8_t tail[3];
        mp3File.seek(fileSize - 128);
        if (mp3File.read(tail, sizeof(tail)) == sizeof(tail) && tail[0] == 'T' && tail[1] == 'A' && tail[2] == 'G') {
            audioBytes -= 128;
        }
    }
    mp3File.close();
    if (audioBytes == 0) return 0;
    return (uint32_t)(((uint64_t)audioBytes * 8ULL + bitrateKbps - 1) / bitrateKbps);
}

void clearMusicDurationProbe() {
    musicDurationProbePending = false;
    musicDurationProbePath = "";
    musicDurationProbeAudioOffset = 0;
}

void prepareMusicDurationProbe(String fullPath, bool useSD, bool isMp3, uint32_t audioOffset) {
    clearMusicDurationProbe();
    if (!isMp3) return;
    musicDurationProbePending = true;
    musicDurationProbePath = fullPath;
    musicDurationProbeUseSD = useSD;
    musicDurationProbeAudioOffset = audioOffset;
}

void updateMusicDurationProbe() {
    if (!musicDurationProbePending || !isMp3Playing || mp3StartTime == 0) return;
    musicDurationProbePending = false;
    uint32_t durationMs = probeMusicMp3DurationMs(musicDurationProbePath, musicDurationProbeUseSD, musicDurationProbeAudioOffset);
    clearMusicDurationProbe();
    if (durationMs > 0) {
        musicPlaybackDurationMs = durationMs;
        mp3DurationSeconds = (durationMs + 999) / 1000;
    }
}

void markMusicPlaybackStartedIfNeeded() {
    if (!isMp3Playing || mp3StartTime != 0) return;
    if (audioOut && audioOut->hasOutputStarted()) {
        mp3StartTime = millis();
        musicPlaybackElapsedMs = 0;
        updateMusicDurationProbe();
    }
}

bool pumpMusicPlayback(bool startupBurst) {
    if (!isMp3Playing || mp3IsPaused) return true;
    if (!mp3 && !wav) return false;

    unsigned long startedAt = millis();
    uint8_t maxLoops = startupBurst ? 40 : 4;
    uint16_t maxMs = startupBurst ? 80 : 5;
    bool stillPlaying = true;
    for (uint8_t i = 0; i < maxLoops && millis() - startedAt < maxMs; i++) {
        stillPlaying = mp3 ? mp3->loop() : wav->loop();
        markMusicPlaybackStartedIfNeeded();
        updateMusicDurationProbe();
        if (!stillPlaying || (startupBurst && mp3StartTime != 0)) break;
        delay(0);
    }
    return stillPlaying;
}

String musicPlaylistBaseName(String name) {
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) name = name.substring(lastSlash + 1);
    return name;
}

void clampMusicPlaylistFocus() {
    if (playlist.empty()) {
        playlistFocus = 0;
        playlistScrollOffset = 0;
        return;
    }

    int total = (int)playlist.size();
    if (playlistFocus < 0) playlistFocus = 0;
    if (playlistFocus >= total) playlistFocus = total - 1;
    if (playlistScrollOffset < 0) playlistScrollOffset = 0;
    int rows = isMp3Playing ? 4 : 5;
    if (playlistFocus < playlistScrollOffset) {
        playlistScrollOffset = playlistFocus;
    } else if (playlistFocus >= playlistScrollOffset + rows) {
        playlistScrollOffset = playlistFocus - rows + 1;
    }
}

bool loadMusicPlaylistCache(bool useSD) {
    String cachePath = useSD ? String(MUSIC_PLAYLIST_CACHE_FILE) : String(MUSIC_PLAYLIST_SPIFFS_CACHE_FILE);
    File cacheFile = useSD ? SD.open(cachePath.c_str(), FILE_READ) : SPIFFS.open(cachePath.c_str(), FILE_READ);
    if (!cacheFile) return false;

    auto readCacheLine = [&cacheFile]() -> String {
        String line = "";
        while (cacheFile.available()) {
            int value = cacheFile.read();
            if (value < 0) break;
            char c = (char)value;
            if (c == '\n') break;
            if (c != '\r') line += c;
        }
        line.trim();
        return line;
    };

    String header = readCacheLine();
    String cachedDir = readCacheLine();
    if (header != MUSIC_PLAYLIST_CACHE_HEADER || cachedDir != musicDir) {
        cacheFile.close();
        return false;
    }

    playlist.clear();
    while (cacheFile.available()) {
        String name = readCacheLine();
        if (name.length() > 0 && isMusicPlaylistFileName(name)) {
            playlist.push_back(name);
        }
    }
    cacheFile.close();
    clampMusicPlaylistFocus();
    return true;
}

bool saveMusicPlaylistCache(bool useSD) {
    String cachePath = useSD ? String(MUSIC_PLAYLIST_CACHE_FILE) : String(MUSIC_PLAYLIST_SPIFFS_CACHE_FILE);
    if (useSD) {
        if (!SD.exists("/Breach_OS")) SD.mkdir("/Breach_OS");
        if (!SD.exists(MUSIC_PLAYLIST_CACHE_DIR)) SD.mkdir(MUSIC_PLAYLIST_CACHE_DIR);
        if (SD.exists(cachePath.c_str())) SD.remove(cachePath.c_str());
    } else {
        if (SPIFFS.exists(cachePath.c_str())) SPIFFS.remove(cachePath.c_str());
    }

    File cacheFile = useSD ? SD.open(cachePath.c_str(), FILE_WRITE) : SPIFFS.open(cachePath.c_str(), FILE_WRITE);
    if (!cacheFile) return false;

    cacheFile.println(MUSIC_PLAYLIST_CACHE_HEADER);
    cacheFile.println(musicDir);
    for (const String& name : playlist) {
        cacheFile.println(name);
    }
    cacheFile.close();
    return true;
}

void scanMusicPlaylistDirectory(bool useSD) {
    playlist.clear();
    File dir = useSD ? SD.open(musicDir.c_str(), FILE_READ) : SPIFFS.open(musicDir.c_str(), FILE_READ);
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = musicPlaylistBaseName(String(file.name()));
                if (isMusicPlaylistFileName(name)) {
                    playlist.push_back(name);
                }
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }

    std::sort(playlist.begin(), playlist.end());
    clampMusicPlaylistFocus();
}

void populatePlaylistFromStorage(bool forceRescan) {
    playlist.clear();
    isSDCardManager = true;
    bool useSD = true;

    if (useSD) {
        if (!ensureMusicSdReady()) {
            clampMusicPlaylistFocus();
            return;
        }
    } else if (!SPIFFS.begin(true)) {
        clampMusicPlaylistFocus();
        return;
    }

    if (!forceRescan && loadMusicPlaylistCache(useSD)) {
        return;
    }

    scanMusicPlaylistDirectory(useSD);
    saveMusicPlaylistCache(useSD);
}

void populatePlaylist() {
    populatePlaylistFromStorage(false);
}

void rescanMusicPlaylist() {
    populatePlaylistFromStorage(true);
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

String formatMusicClock(uint32_t ms) {
    uint32_t seconds = (ms + 500) / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t secs = seconds % 60;
    return musicTwoDigits(minutes) + ":" + musicTwoDigits(secs);
}

uint32_t getMusicPlaybackElapsedMs() {
    if (!isMp3Playing) return 0;
    uint32_t elapsed = musicPlaybackElapsedMs;
    if (elapsed == 0 && mp3StartTime != 0) {
        unsigned long clockNow = (mp3IsPaused && mp3PausedTime != 0) ? mp3PausedTime : millis();
        elapsed = clockNow - mp3StartTime;
    }
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

    String timeText = formatMusicClock(elapsed) + "/" + (hasDuration ? formatMusicClock(musicPlaybackDurationMs) : String("00:00"));
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString(timeText, 120, 116);

    int barX = 13;
    int barY = 108;
    int barW = 214;
    int barH = 4;
    uint16_t darkYellow = canvas.color565(58, 45, 0);
    canvas.drawRect(barX, barY, barW, barH, CP_YELLOW);
    canvas.fillRect(barX + 1, barY + 1, barW - 2, barH - 2, darkYellow);
    int fillW = 0;
    if (hasDuration) {
        fillW = (int)((uint64_t)(barW - 2) * elapsed / musicPlaybackDurationMs);
    } else if (fileSize > 0) {
        fillW = (int)((uint64_t)(barW - 2) * filePos / fileSize);
    }
    if (fillW > barW - 2) fillW = barW - 2;
    if (fillW > 0) canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, CP_YELLOW);
    if (!hasDuration && fileSize == 0) {
        int scanW = 24;
        int scanRange = barW - 2 - scanW;
        int scanX = barX + 1 + (scanRange > 0 ? (elapsed / 80) % scanRange : 0);
        canvas.fillRect(scanX, barY + 1, scanW, barH - 2, CP_YELLOW);
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
    
    int dividerY = isMp3Playing ? 78 : 94;
    canvas.drawLine(10, dividerY, 230, dividerY, CP_CYAN);
    
    if (isMp3Playing) {
        drawAudioSpectrum(13, 106, 214, 52);
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
    bool hasRescan = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
        if (c == '/') hasRight = true;
        if (c == 'r' || c == 'R') hasRescan = true;
    }

    if (hasRescan && !isMp3Playing) {
        playSound(sound_select, sound_select_size);
        rescanMusicPlaylist();
        drawMusicPlayer();
        return;
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
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    resetAudioSpectrum();

    isMp3Playing = true;
    mp3Filename = fileName;
    mp3IsPaused = false;
    mp3StartTime = 0;
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
        if (mp3StartTime == 0) mp3StartTime = millis();
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
    bool isWav = lowerName.endsWith(".wav");
    bool isMp3 = lowerName.endsWith(".mp3");
    resetAudioSpectrum();
    bool useSD = isSDCardManager;
    if (useSD && isWav) {
        if (ensureMusicSdReady() && playDirectPcmWavFromSD(fullPath, fileName)) {
            return;
        }
        resetAudioSpectrum();
    }

    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    audioOut = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);

    bool started = false;
    uint32_t probedDurationMs = 0; // MP3 stays unknown until the post-start probe finishes.
    uint32_t audioStartOffset = 0;
    
    if (useSD) {
        if (ensureMusicSdReady()) {
            audioStartOffset = isMp3 ? getMusicMp3AudioStartOffset(fullPath, true) : 0;
            if (isWav) probedDurationMs = probePcmWavDurationMs(fullPath, true);
            fileSD = new AudioFileSourceSD(fullPath.c_str());
            if (isMp3 && audioStartOffset > 0) fileSD->seek(audioStartOffset, SEEK_SET);
            audioBuffer = new AudioFileSourceBuffer(fileSD, isMp3 ? MUSIC_MP3_BUFFER_BYTES : MUSIC_WAV_BUFFER_BYTES);
            if (isWav) {
                wav = new AudioGeneratorWAV();
                started = wav->begin(audioBuffer, audioOut);
            } else {
                mp3 = new AudioGeneratorMP3();
                started = mp3->begin(audioBuffer, audioOut);
            }
        }
    } else {
        audioStartOffset = isMp3 ? getMusicMp3AudioStartOffset(fullPath, false) : 0;
        if (isWav) probedDurationMs = probePcmWavDurationMs(fullPath, false);
        fileSPIFFS = new AudioFileSourceSPIFFS(fullPath.c_str());
        if (isMp3 && audioStartOffset > 0) fileSPIFFS->seek(audioStartOffset, SEEK_SET);
        audioBuffer = new AudioFileSourceBuffer(fileSPIFFS, isMp3 ? MUSIC_MP3_BUFFER_BYTES : MUSIC_WAV_BUFFER_BYTES);
        if (isWav) {
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
        mp3StartTime = 0;
        mp3PausedTime = 0;
        mp3DurationSeconds = probedDurationMs > 0 ? (probedDurationMs + 999) / 1000 : 0;
        musicPlaybackDurationMs = probedDurationMs;
        musicPlaybackElapsedMs = 0;
        prepareMusicDurationProbe(fullPath, useSD, isMp3, audioStartOffset);
        pumpMusicPlayback(true);
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
        clearMusicDurationProbe();
    }
    drawMusicPlayer();
}






