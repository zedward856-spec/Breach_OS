// Hardware menu, settings, file manager, file actions, and direct file playback.

static constexpr int FILE_NAME_MODE_RENAME = 0;
static constexpr int FILE_NAME_MODE_NEW_FOLDER = 1;
static constexpr int FILE_NAME_MODE_NEW_TXT = 2;
static constexpr int TEXT_EDITOR_MAX_BYTES = 4096;

bool isSystemFile(String name) {
    String lower = name;
    lower.toLowerCase();
    if (lower == "system volume information") return true;
    if (lower.startsWith(".")) return true;
    return false;
}

bool deleteRecursive(String path) {
    if (isSDCardManager) {
        if (isSDFallback) return true;
        File dir = SD.open(path);
        if (!dir) return false;
        
        if (!dir.isDirectory()) {
            dir.close();
            return SD.remove(path);
        }
        
        std::vector<String> childPaths;
        std::vector<bool> childIsDir;
        
        File file = dir.openNextFile();
        while (file) {
            String childName = String(file.name());
            int lastSlash = childName.lastIndexOf('/');
            if (lastSlash >= 0) childName = childName.substring(lastSlash + 1);
            
            childPaths.push_back(path + (path.endsWith("/") ? "" : "/") + childName);
            childIsDir.push_back(file.isDirectory());
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
        
        for (size_t i = 0; i < childPaths.size(); i++) {
            if (childIsDir[i]) {
                deleteRecursive(childPaths[i]);
            } else {
                SD.remove(childPaths[i]);
            }
        }
        return SD.rmdir(path);
    } else {
        if (isFlashFallback) return true;
        File root = SPIFFS.open("/");
        if (!root) return false;
        
        std::vector<String> filesToDelete;
        File file = root.openNextFile();
        while (file) {
            String name = String(file.name());
            String matchPrefix = path;
            if (!matchPrefix.endsWith("/")) matchPrefix += "/";
            
            if (name.startsWith(matchPrefix) || name == path) {
                filesToDelete.push_back(name);
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        
        for (const String& f : filesToDelete) {
            SPIFFS.remove(f);
        }
        return true;
    }
}

bool compareFiles(const RealFile& a, const RealFile& b) {
    String aName = a.name;
    String bName = b.name;
    aName.toLowerCase();
    bName.toLowerCase();
    
    if (currentSortField == SORT_FIELD_NAME) {
        if (currentSortOrder == SORT_ORDER_ASC) {
            return aName < bName;
        } else {
            return aName > bName;
        }
    } else { // SORT_FIELD_TYPE
        if (a.isDir != b.isDir) {
            if (currentSortOrder == SORT_ORDER_ASC) {
                return a.isDir > b.isDir; // Directory first
            } else {
                return a.isDir < b.isDir; // Files first
            }
        }
        return aName < bName;
    }
}

String cleanNewFileManagerName(String name, bool txtFile) {
    name.trim();
    name.replace("/", "");
    name.replace("\\", "");
    while (name.startsWith(".")) name.remove(0, 1);
    if (txtFile && name.length() > 0) {
        String lower = name;
        lower.toLowerCase();
        if (!lower.endsWith(".txt")) name += ".txt";
    }
    return name;
}

void selectFileManagerEntry(String name) {
    int foundIdx = -1;
    for (size_t i = 0; i < loadedFiles.size(); i++) {
        if (loadedFiles[i].name == name) {
            foundIdx = i;
            break;
        }
    }
    if (foundIdx >= 0) {
        fileManagerSelected = foundIdx;
        if ((int)loadedFiles.size() > 5) {
            fileManagerScrollOffset = fileManagerSelected > 4 ? fileManagerSelected - 4 : 0;
        } else {
            fileManagerScrollOffset = 0;
        }
    }
}

bool createFileManagerEntry(String name, bool folder) {
    String newPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + name;
    if (isSDCardManager) {
        if (isSDFallback) {
            RealFile nf = {name, folder ? "DIR" : "0.0 KB", folder};
            loadedFiles.push_back(nf);
            return true;
        }
        if (SD.exists(newPath)) return false;
        if (folder) return SD.mkdir(newPath);
        File f = SD.open(newPath, FILE_WRITE);
        if (!f) return false;
        f.close();
        return true;
    }
    if (isFlashFallback || folder || SPIFFS.exists(newPath)) return false;
    File f = SPIFFS.open(newPath, FILE_WRITE);
    if (!f) return false;
    f.close();
    return true;
}

String buildFileManagerPath(String name) {
    return fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + name;
}

bool isEditableTextFileName(String name, bool isDir) {
    if (name == ".." || isDir) return false;
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".txt");
}

void setTextEditorStatus(String status, uint16_t ms) {
    textEditorStatus = status;
    textEditorStatusUntil = millis() + ms;
}

void insertTextEditorChar(char c) {
    if ((int)textEditorBuffer.length() >= TEXT_EDITOR_MAX_BYTES) {
        setTextEditorStatus("MAX 4KB", 1100);
        return;
    }
    if (textEditorCursor < 0) textEditorCursor = 0;
    if (textEditorCursor > (int)textEditorBuffer.length()) textEditorCursor = textEditorBuffer.length();
    String insert = "";
    insert += c;
    textEditorBuffer = textEditorBuffer.substring(0, textEditorCursor) + insert + textEditorBuffer.substring(textEditorCursor);
    textEditorCursor++;
    textEditorDirty = true;
}

bool saveTextEditorFile() {
    if (!textEditorDirty) return true;
    File file = isSDCardManager ? SD.open(textEditorPath, FILE_WRITE) : SPIFFS.open(textEditorPath, FILE_WRITE);
    if (!file) {
        setTextEditorStatus("SAVE FAIL", 1600);
        return false;
    }
    size_t written = file.write((const uint8_t*)textEditorBuffer.c_str(), textEditorBuffer.length());
    file.close();
    if (written != textEditorBuffer.length()) {
        setTextEditorStatus("WRITE FAIL", 1600);
        return false;
    }
    textEditorDirty = false;
    setTextEditorStatus("SAVED", 700);
    return true;
}

void openTextEditor(String fileName) {
    textEditorFileName = fileName;
    textEditorPath = buildFileManagerPath(fileName);
    textEditorBuffer = "";
    textEditorBuffer.reserve(TEXT_EDITOR_MAX_BYTES + 8);
    textEditorCursor = 0;
    textEditorDirty = false;
    textEditorStatus = "";
    textEditorStatusUntil = 0;

    if (isSDCardManager) {
        SPI.begin(40, 39, 14, 12);
        SD.begin(12, SPI, 20000000);
    }

    File file = isSDCardManager ? SD.open(textEditorPath, FILE_READ) : SPIFFS.open(textEditorPath, FILE_READ);
    if (!file) {
        drawMessage("OPEN FAILED");
        delay(900);
        appState = STATE_FILE_ACTIONS_MENU;
        drawFileActionsMenu();
        return;
    }
    if (file.size() > TEXT_EDITOR_MAX_BYTES) {
        file.close();
        drawMessage("TXT TOO LARGE", "MAX 4KB");
        delay(1100);
        appState = STATE_FILE_ACTIONS_MENU;
        drawFileActionsMenu();
        return;
    }

    while (file.available() && textEditorBuffer.length() < TEXT_EDITOR_MAX_BYTES) {
        int value = file.read();
        if (value < 0) break;
        char c = (char)value;
        if (c != '\r') textEditorBuffer += c;
    }
    file.close();
    textEditorCursor = textEditorBuffer.length();
    appState = STATE_FILE_TEXT_EDITOR;
    blinkState = true;
    drawFileTextEditor();
}

void drawFileTextEditor() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(4, 4, 232, 124, CP_CYAN);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- TXT EDITOR ---", 120, 10);

    String name = textEditorFileName;
    if (name.length() > 26) name = name.substring(0, 23) + "...";
    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(12, 24);
    canvas.print(name);

    canvas.setTextColor(textEditorDirty ? CP_YELLOW : CP_DIM);
    canvas.setCursor(170, 24);
    canvas.print(String(textEditorBuffer.length()) + "/" + String(TEXT_EDITOR_MAX_BYTES));
    if (textEditorDirty) canvas.print("*");

    canvas.drawRect(10, 36, 220, 70, CP_DIM);
    String view = textEditorBuffer;
    int cursor = textEditorCursor;
    if (cursor < 0) cursor = 0;
    if (cursor > (int)view.length()) cursor = view.length();
    if (blinkState) view = view.substring(0, cursor) + "_" + view.substring(cursor);

    std::vector<String> lines;
    String line = "";
    for (size_t i = 0; i < view.length(); i++) {
        char c = view[i];
        if (c == '\n') {
            lines.push_back(line);
            line = "";
        } else {
            line += c;
        }
    }
    lines.push_back(line);

    int visible = 7;
    int start = (int)lines.size() > visible ? (int)lines.size() - visible : 0;
    canvas.setTextColor(WHITE);
    for (int i = 0; i < visible && start + i < (int)lines.size(); i++) {
        String row = lines[start + i];
        if (row.length() > 35) row = row.substring(row.length() - 35);
        canvas.setCursor(14, 40 + i * 9);
        canvas.print(row);
    }

    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("TYPE | ENTER NL | DEL BKSP", 120, 110);
    String footer = millis() < textEditorStatusUntil ? textEditorStatus : "ESC SAVE/BACK";
    canvas.drawCenterString(footer, 120, 120);
    pushCanvas();
}

void handleFileTextEditorInput(Keyboard_Class::KeysState status) {
    bool hasExit = false;
    for (char c : status.word) {
        if (c == '`') hasExit = true;
    }
    if (hasExit) {
        playSound(sound_select, sound_select_size);
        if (!saveTextEditorFile()) {
            drawFileTextEditor();
            return;
        }
        populateFileList();
        selectFileManagerEntry(textEditorFileName);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }

    bool changed = false;
    if (status.del && textEditorCursor > 0 && textEditorBuffer.length() > 0) {
        textEditorBuffer.remove(textEditorCursor - 1, 1);
        textEditorCursor--;
        textEditorDirty = true;
        changed = true;
    }
    if (status.enter) {
        insertTextEditorChar('\n');
        changed = true;
    }
    for (char c : status.word) {
        if (c == '`') continue;
        if (c >= 32 && c <= 126) {
            insertTextEditorChar(c);
            changed = true;
        }
    }
    if (changed) drawFileTextEditor();
}

void populateFileList() {
    loadedFiles.clear();
    fsStatusMessage = "";
    isSDFallback = false;
    isFlashFallback = false;
    
    // Insert parent directory ".." if we are in a subdirectory
    if (fileManagerCurrentPath != "/") {
        RealFile parentDir = {"..", "DIR", true};
        loadedFiles.push_back(parentDir);
    }
    
    if (isSDCardManager) {
        SPI.begin(40, 39, 14, 12);
        bool mountSuccess = SD.begin(12, SPI, 20000000);
        File root;
        if (mountSuccess) {
            root = SD.open(fileManagerCurrentPath);
        }
        
        if (!mountSuccess || !root || !root.isDirectory()) {
            isSDFallback = true;
            fsStatusMessage = "SD MOUNT FAIL";
            if (root) root.close();
        } else {
            File file = root.openNextFile();
            while (file && loadedFiles.size() < 100) {
                RealFile rf;
                rf.name = String(file.name());
                int lastSlashIdx = rf.name.lastIndexOf('/');
                if (lastSlashIdx >= 0) {
                    rf.name = rf.name.substring(lastSlashIdx + 1);
                }
                
                if (!showSystemFiles && isSystemFile(rf.name)) {
                    file = root.openNextFile();
                    continue;
                }
                
                rf.isDir = file.isDirectory();
                if (rf.isDir) {
                    rf.sizeStr = "DIR";
                } else {
                    rf.sizeStr = String(file.size() / 1024.0, 1) + " KB";
                }
                loadedFiles.push_back(rf);
                file = root.openNextFile();
            }
            root.close();
            // If the folder is empty (excluding ".." if present)
            int minSize = (fileManagerCurrentPath != "/") ? 1 : 0;
            if ((int)loadedFiles.size() <= minSize) {
                fsStatusMessage = "folder empty press esc";
            }
        }
    } else {
        bool mountSuccess = SPIFFS.begin(true);
        File root;
        if (mountSuccess) {
            root = SPIFFS.open("/");
        }
        
        if (!mountSuccess || !root) {
            isFlashFallback = true;
            fsStatusMessage = "FLASH MOUNT FAIL";
            if (root) root.close();
        } else {
            File file = root.openNextFile();
            while (file && loadedFiles.size() < 100) {
                RealFile rf;
                rf.name = String(file.name());
                if (rf.name.startsWith("/")) rf.name.remove(0, 1);
                
                if (!showSystemFiles && isSystemFile(rf.name)) {
                    file = root.openNextFile();
                    continue;
                }
                
                rf.isDir = file.isDirectory();
                if (rf.isDir) {
                    rf.sizeStr = "DIR";
                } else {
                    rf.sizeStr = String(file.size() / 1024.0, 1) + " KB";
                }
                loadedFiles.push_back(rf);
                file = root.openNextFile();
            }
            root.close();
            int minSize = (fileManagerCurrentPath != "/") ? 1 : 0;
            if ((int)loadedFiles.size() <= minSize) {
                fsStatusMessage = "folder empty press esc";
            }
        }
    }
    
    // Sort files based on settings (keeping ".." at the top if present)
    if (!loadedFiles.empty()) {
        int sortStart = (fileManagerCurrentPath != "/") ? 1 : 0;
        if ((int)loadedFiles.size() > sortStart) {
            std::sort(loadedFiles.begin() + sortStart, loadedFiles.end(), compareFiles);
        }
    }
    
    lastFileSelectionTime = millis();
    marqueeScrollOffset = 0;
}

void readSelectedFileContent(String fileName) {
    openedFileContent.clear();
    openedFileName = fileName;
    String path = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + fileName;
    
    if (isSDCardManager) {
        if (isSDFallback) {
            openedFileContent.push_back("SD storage unavailable.");
            return;
        }
        
        SPI.begin(40, 39, 14, 12);
        if (!SD.begin(12, SPI, 20000000)) {
            openedFileContent.push_back("SD Card Mount Error");
            return;
        }
        File f = SD.open(path);
        if (!f) {
            openedFileContent.push_back("Could not open file.");
            return;
        }
        if (f.isDirectory()) {
            openedFileContent.push_back("Cannot read directory.");
            f.close();
            return;
        }
        
        int lineCount = 0;
        while (f.available() && lineCount < 4) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            if (line.length() > 25) line = line.substring(0, 25) + "...";
            openedFileContent.push_back(line);
            lineCount++;
        }
        f.close();
    } else {
        if (isFlashFallback) {
            openedFileContent.push_back("Flash storage unavailable.");
            return;
        }
        
        if (!SPIFFS.begin(true)) {
            openedFileContent.push_back("Flash Mount Error");
            return;
        }
        File f = SPIFFS.open(path);
        if (!f) {
            openedFileContent.push_back("Could not open file.");
            return;
        }
        if (f.isDirectory()) {
            openedFileContent.push_back("Cannot read directory.");
            f.close();
            return;
        }
        
        int lineCount = 0;
        while (f.available() && lineCount < 4) {
            String line = f.readStringUntil('\n');
            line.replace("\r", "");
            if (line.length() > 25) line = line.substring(0, 25) + "...";
            openedFileContent.push_back(line);
            lineCount++;
        }
        f.close();
    }
}

static constexpr uint32_t SOUND_REC_SAMPLE_RATE = 16000;
static constexpr uint32_t SOUND_REC_BYTES_PER_SAMPLE = sizeof(int16_t);
static constexpr size_t SOUND_REC_RECORD_LENGTH = 240;
static constexpr uint32_t SOUND_REC_TARGET_SECONDS = 30;
static constexpr size_t SOUND_REC_RECORD_NUMBER = (SOUND_REC_SAMPLE_RATE * SOUND_REC_TARGET_SECONDS) / SOUND_REC_RECORD_LENGTH;
static constexpr size_t SOUND_REC_RECORD_SIZE = SOUND_REC_RECORD_NUMBER * SOUND_REC_RECORD_LENGTH;
static constexpr size_t SOUND_REC_STREAM_RECORD_LENGTH = 1024;
static constexpr size_t SOUND_REC_STREAM_RECORD_NUMBER = (SOUND_REC_SAMPLE_RATE * SOUND_REC_TARGET_SECONDS + SOUND_REC_STREAM_RECORD_LENGTH - 1) / SOUND_REC_STREAM_RECORD_LENGTH;
static constexpr size_t SOUND_REC_STREAM_SLOTS = 4;
static constexpr size_t SOUND_REC_VIZ_MAX_SAMPLES = SOUND_REC_STREAM_RECORD_LENGTH;

static constexpr uint32_t SOUND_REC_DRAW_INTERVAL_MS = 80;
static constexpr uint64_t SOUND_REC_WAV_HEADER_BYTES = 44ULL;
static constexpr const char* SOUND_REC_OS_DIR = "/Breach_OS";
static constexpr const char* SOUND_REC_DIR = "/Breach_OS/recordings";
static uint32_t soundRecFileCounter = 0;
uint16_t soundRecLivePeak = 0;
uint16_t soundRecMaxPeak = 0;

void resetSoundRecMicPins() {
    gpio_reset_pin(GPIO_NUM_43); // Cardputer PDM mic clock
    gpio_reset_pin(GPIO_NUM_46); // Cardputer PDM mic data
}

void soundRecPutText(uint8_t* header, int offset, const char* text) {
    for (int i = 0; i < 4; i++) header[offset + i] = (uint8_t)text[i];
}

void soundRecPutU16(uint8_t* header, int offset, uint16_t value) {
    header[offset] = (uint8_t)(value & 0xFF);
    header[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
}

void soundRecPutU32(uint8_t* header, int offset, uint32_t value) {
    header[offset] = (uint8_t)(value & 0xFF);
    header[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    header[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    header[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

void buildSoundRecWavHeader(uint8_t* header, uint32_t dataBytes) {
    const uint16_t bitsPerSample = 16;
    const uint16_t channels = 1;
    const uint32_t byteRate = SOUND_REC_SAMPLE_RATE * channels * bitsPerSample / 8;
    const uint16_t blockAlign = channels * bitsPerSample / 8;

    soundRecPutText(header, 0, "RIFF");
    soundRecPutU32(header, 4, 36 + dataBytes);
    soundRecPutText(header, 8, "WAVE");
    soundRecPutText(header, 12, "fmt ");
    soundRecPutU32(header, 16, 16);
    soundRecPutU16(header, 20, 1);
    soundRecPutU16(header, 22, channels);
    soundRecPutU32(header, 24, SOUND_REC_SAMPLE_RATE);
    soundRecPutU32(header, 28, byteRate);
    soundRecPutU16(header, 32, blockAlign);
    soundRecPutU16(header, 34, bitsPerSample);
    soundRecPutText(header, 36, "data");
    soundRecPutU32(header, 40, dataBytes);
}

bool ensureSoundRecDir(const char* path) {
    File dir = SD.open(path);
    if (dir) {
        bool ok = dir.isDirectory();
        dir.close();
        return ok;
    }
    return SD.mkdir(path);
}

bool ensureSoundRecRecordingsDir() {
    return ensureSoundRecDir(SOUND_REC_OS_DIR) && ensureSoundRecDir(SOUND_REC_DIR);
}

String makeSoundRecPath() {
    char filename[64];
    for (uint16_t tries = 0; tries < 1000; tries++) {
        snprintf(filename, sizeof(filename), "%s/recorded%lu.wav", SOUND_REC_DIR, (unsigned long)soundRecFileCounter++);
        if (!SD.exists(filename)) return String(filename);
    }
    snprintf(filename, sizeof(filename), "%s/recorded%lu.wav", SOUND_REC_DIR, (unsigned long)soundRecFileCounter++);
    return String(filename);
}

String soundRecTwoDigits(uint32_t value) {
    if (value < 10) return "0" + String(value);
    return String(value);
}

String formatSoundRecTime(uint64_t samples) {
    uint32_t seconds = (uint32_t)(samples / SOUND_REC_SAMPLE_RATE);
    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    if (hours > 0) {
        return String(hours) + ":" + soundRecTwoDigits(minutes) + ":" + soundRecTwoDigits(secs);
    }
    return String(minutes) + ":" + soundRecTwoDigits(secs);
}

String formatSoundRecSize(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        uint32_t whole = (uint32_t)(bytes / (1024ULL * 1024ULL * 1024ULL));
        uint32_t frac = (uint32_t)((bytes % (1024ULL * 1024ULL * 1024ULL)) * 10ULL / (1024ULL * 1024ULL * 1024ULL));
        return String(whole) + "." + String(frac) + "G";
    }
    if (bytes >= 1024ULL * 1024ULL) return String((uint32_t)(bytes / (1024ULL * 1024ULL))) + "M";
    if (bytes >= 1024ULL) return String((uint32_t)(bytes / 1024ULL)) + "K";
    return String((uint32_t)bytes) + "B";
}

uint16_t getSoundRecPeak(const int16_t* samples, size_t sampleCount) {
    uint16_t peak = 0;
    if (!samples) return peak;
    for (size_t i = 0; i < sampleCount; i++) {
        int32_t v = samples[i];
        if (v < 0) v = -v;
        if (v > 32767) v = 32767;
        if ((uint16_t)v > peak) peak = (uint16_t)v;
    }
    return peak;
}

int16_t* allocSoundRecBufferFromChoices(const size_t* chunkChoices, size_t choiceCount, size_t& recordChunks) {
    for (size_t i = 0; i < choiceCount; i++) {
        size_t chunks = chunkChoices[i];
        size_t bytes = chunks * SOUND_REC_RECORD_LENGTH * SOUND_REC_BYTES_PER_SAMPLE;
        int16_t* data = (int16_t*)malloc(bytes);
        if (data) {
            memset(data, 0, bytes);
            recordChunks = chunks;
            return data;
        }
    }
    recordChunks = 0;
    return nullptr;
}

int16_t* allocSoundRecBuffer(size_t& recordChunks) {
    static const size_t chunkChoices[] = {SOUND_REC_RECORD_NUMBER, 512, 384, 256, 192, 128, 96, 64};
    return allocSoundRecBufferFromChoices(chunkChoices, sizeof(chunkChoices) / sizeof(chunkChoices[0]), recordChunks);
}

int16_t* allocSoundRecFallbackBuffer(size_t& recordChunks) {
    static const size_t chunkChoices[] = {512, 384, 256, 192, 128, 96, 64};
    return allocSoundRecBufferFromChoices(chunkChoices, sizeof(chunkChoices) / sizeof(chunkChoices[0]), recordChunks);
}

void configureSoundRecMic() {
    auto cfg = M5Cardputer.Mic.config();
    cfg.pin_data_in = GPIO_NUM_46;
    cfg.pin_ws = GPIO_NUM_43;
    cfg.pin_bck = I2S_PIN_NO_CHANGE;
    cfg.pin_mck = I2S_PIN_NO_CHANGE;
    cfg.i2s_port = I2S_NUM_0;
    cfg.sample_rate = SOUND_REC_SAMPLE_RATE;
    cfg.input_channel = m5::input_channel_t::input_only_right;
    cfg.stereo = false;
    cfg.noise_filter_level = 0;
    cfg.over_sampling = 2;
    cfg.magnification = 16;
    cfg.dma_buf_len = 128;
    cfg.dma_buf_count = 8;
    cfg.task_priority = 2;
    M5Cardputer.Mic.config(cfg);
}

bool startSoundRecMic() {
    M5Cardputer.Speaker.setVolume(255);
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.end();
    M5Cardputer.Mic.end();
    delay(50);
    resetSoundRecMicPins();
    delay(20);

    configureSoundRecMic();
    if (M5Cardputer.Mic.begin() && M5Cardputer.Mic.isEnabled()) return true;

    M5Cardputer.Mic.end();
    resetSoundRecMicPins();
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
    return false;
}

void restoreSoundRecAudio() {
    M5Cardputer.Mic.end();
    resetSoundRecMicPins();
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
}

void conditionSoundRecChunk(int16_t* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) return;

    int64_t sum = 0;
    for (size_t i = 0; i < sampleCount; i++) sum += samples[i];
    int32_t mean = (int32_t)(sum / (int64_t)sampleCount);

    for (size_t i = 0; i < sampleCount; i++) {
        int32_t v = (int32_t)samples[i] - mean;
        v *= 2;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        samples[i] = (int16_t)v;
    }
}

bool pollSoundRecStop(unsigned long startMs, bool& stopKeysArmed, bool& lastStopDown) {
    Keyboard_Class::KeysState stopStatus = M5Cardputer.Keyboard.keysState();
    bool stopDown = stopStatus.enter || stopStatus.del;
    for (char c : stopStatus.word) {
        if (c == '`') stopDown = true;
    }

    if (!stopKeysArmed) {
        if (millis() - startMs > 800 && !stopDown) {
            stopKeysArmed = true;
        }
        lastStopDown = stopDown;
        return false;
    }

    bool pressed = stopDown && !lastStopDown;
    lastStopDown = stopDown;
    return pressed;
}

void drawSoundRecScreen(const int16_t* samples, size_t sampleCount, uint64_t recordedSamples, uint64_t maxSamples, uint64_t maxDataBytes) {
    uint64_t dataBytes = recordedSamples * SOUND_REC_BYTES_PER_SAMPLE;
    int progress = maxSamples ? (int)((recordedSamples * 100ULL) / maxSamples) : 0;
    if (progress > 100) progress = 100;

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_CYAN);
    drawChippedButton(8, 7, 224, 120, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- SOUND REC ---", 120, 11);
    canvas.setTextColor(CP_CYAN);
    canvas.drawCenterString("TIME " + formatSoundRecTime(recordedSamples) + " / " + formatSoundRecTime(maxSamples), 120, 27);
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("DATA " + formatSoundRecSize(dataBytes) + " / " + formatSoundRecSize(maxDataBytes), 120, 40);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("MIC PDM PK " + String(soundRecLivePeak) + " / MAX " + String(soundRecMaxPeak), 120, 50);

    int barX = 20;
    int barY = 62;
    int barW = 200;
    int barH = 8;
    canvas.drawRect(barX, barY, barW, barH, CP_DIM);
    int fillW = (int)((uint64_t)(barW - 2) * progress / 100ULL);
    if (fillW > 0) canvas.fillRect(barX + 1, barY + 1, fillW, barH - 2, CP_RED);

    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("LIVE SPECTRUM", 120, 75);
    if (samples && sampleCount > 0) feedAudioSpectrumBuffer(samples, sampleCount);
    drawAudioSpectrum(14, 114, 212, 38);

    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("ENTER/DEL/ESC STOP", 120, 119);
    pushCanvas();
}

void consumeSoundRecChunkForUi(const int16_t* data, size_t sampleCount) {
    if (!data) return;
    if (sampleCount > SOUND_REC_VIZ_MAX_SAMPLES) sampleCount = SOUND_REC_VIZ_MAX_SAMPLES;
    static int16_t vizData[SOUND_REC_VIZ_MAX_SAMPLES];
    memcpy(vizData, data, sampleCount * sizeof(int16_t));
    conditionSoundRecChunk(vizData, sampleCount);
    soundRecLivePeak = getSoundRecPeak(data, sampleCount);
    if (soundRecLivePeak > soundRecMaxPeak) soundRecMaxPeak = soundRecLivePeak;
    feedAudioSpectrumBuffer(vizData, sampleCount);
}

bool soundRecStreamToSd(const String& path) {
    size_t slotSamples = SOUND_REC_STREAM_RECORD_LENGTH;
    size_t slotBytes = slotSamples * SOUND_REC_BYTES_PER_SAMPLE;
    int16_t* streamData = (int16_t*)malloc(SOUND_REC_STREAM_SLOTS * slotBytes);
    if (!streamData) return false;
    memset(streamData, 0, SOUND_REC_STREAM_SLOTS * slotBytes);

    SD.remove(path.c_str());
    File recFile = SD.open(path.c_str(), FILE_WRITE);
    if (!recFile) {
        free(streamData);
        return false;
    }

    uint8_t wavHeader[44];
    buildSoundRecWavHeader(wavHeader, 0);
    if (recFile.write(wavHeader, sizeof(wavHeader)) != sizeof(wavHeader)) {
        recFile.close();
        SD.remove(path.c_str());
        free(streamData);
        return false;
    }

    drawProgressBar(60, "STREAMING PDM MIC", CP_CYAN);
    if (!startSoundRecMic()) {
        recFile.close();
        SD.remove(path.c_str());
        free(streamData);
        return false;
    }

    bool recordFailed = false;
    bool stopRequested = false;
    bool writeFailed = false;
    bool stopKeysArmed = false;
    bool lastStopDown = true;
    uint64_t queuedChunks = 0;
    uint64_t writtenChunks = 0;
    uint64_t maxSamples = SOUND_REC_STREAM_RECORD_NUMBER * SOUND_REC_STREAM_RECORD_LENGTH;
    uint64_t maxDataBytes = maxSamples * SOUND_REC_BYTES_PER_SAMPLE;
    soundRecLivePeak = 0;
    soundRecMaxPeak = 0;
    resetAudioSpectrum();
    unsigned long startMs = millis();
    unsigned long lastDrawMs = 0;
    drawSoundRecScreen(nullptr, 0, 0, maxSamples, maxDataBytes);

    for (size_t i = 0; i < SOUND_REC_STREAM_RECORD_NUMBER; i++) {
        M5Cardputer.update();
        if (pollSoundRecStop(startMs, stopKeysArmed, lastStopDown)) {
            stopRequested = true;
            break;
        }

        int16_t* data = &streamData[(queuedChunks % SOUND_REC_STREAM_SLOTS) * slotSamples];
        memset(data, 0, slotBytes);
        if (!M5Cardputer.Mic.record(data, slotSamples, SOUND_REC_SAMPLE_RATE, false)) {
            recordFailed = true;
            break;
        }
        queuedChunks++;

        if (queuedChunks > 2) {
            int16_t* ready = &streamData[(writtenChunks % SOUND_REC_STREAM_SLOTS) * slotSamples];
            consumeSoundRecChunkForUi(ready, slotSamples);
            if (recFile.write((const uint8_t*)ready, slotBytes) != slotBytes) {
                writeFailed = true;
                break;
            }
            writtenChunks++;
        }

        unsigned long now = millis();
        if (now - lastDrawMs >= SOUND_REC_DRAW_INTERVAL_MS) {
            lastDrawMs = now;
            drawSoundRecScreen(nullptr, 0, writtenChunks * slotSamples, maxSamples, maxDataBytes);
        }
    }

    while (M5Cardputer.Mic.isRecording()) {
        M5Cardputer.update();
        delay(1);
    }
    restoreSoundRecAudio();

    while (!writeFailed && writtenChunks < queuedChunks) {
        int16_t* ready = &streamData[(writtenChunks % SOUND_REC_STREAM_SLOTS) * slotSamples];
        consumeSoundRecChunkForUi(ready, slotSamples);
        if (recFile.write((const uint8_t*)ready, slotBytes) != slotBytes) {
            writeFailed = true;
            break;
        }
        writtenChunks++;
        drawSoundRecScreen(nullptr, 0, writtenChunks * slotSamples, maxSamples, maxDataBytes);
    }

    uint64_t capturedSamples = writtenChunks * slotSamples;
    uint32_t dataBytes = (uint32_t)(capturedSamples * SOUND_REC_BYTES_PER_SAMPLE);
    buildSoundRecWavHeader(wavHeader, dataBytes);
    bool headerSaved = dataBytes > 0 && recFile.seek(0) && recFile.write(wavHeader, sizeof(wavHeader)) == sizeof(wavHeader);
    recFile.flush();
    recFile.close();
    free(streamData);

    if (!headerSaved || writeFailed || dataBytes == 0) {
        SD.remove(path.c_str());
        Serial.printf("[SOUNDREC] stream failed bytes=%lu max=%u writeFail=%d\n", (unsigned long)dataBytes, soundRecMaxPeak, writeFailed ? 1 : 0);
        return false;
    }

    String fileName = path.substring(path.lastIndexOf('/') + 1);
    Serial.printf("[SOUNDREC] streamed %s bytes=%lu max=%u mic=PDM gpio_data=46 gpio_clk=43\n", path.c_str(), (unsigned long)dataBytes, soundRecMaxPeak);
    if (soundRecMaxPeak < 24) {
        drawMessage("MIC FLATLINE", fileName);
    } else if (stopRequested || recordFailed || writtenChunks < SOUND_REC_STREAM_RECORD_NUMBER) {
        drawMessage("PARTIAL WAV SAVED", fileName);
    } else {
        drawMessage("REC SAVED PK " + String(soundRecMaxPeak), fileName);
    }
    return true;
}

void soundRec() {
    stopMp3Playback();
    drawProgressBar(0, "MOUNTING SD REC VAULT", CP_CYAN);

    SPI.begin(40, 39, 14, 12);
    if (!SD.begin(12, SPI, 25000000) || SD.cardType() == CARD_NONE) {
        drawMessage("SD MOUNT FAIL", "SOUND REC ABORTED");
        delay(1500);
        drawHardwareMenu();
        return;
    }
    if (!ensureSoundRecRecordingsDir()) {
        drawMessage("REC DIR FAIL", "Breach_OS/recordings");
        delay(1500);
        drawHardwareMenu();
        return;
    }

    String path = makeSoundRecPath();
    size_t recordChunks = 0;
    int16_t* recData = allocSoundRecBuffer(recordChunks);
    if (!recData || recordChunks < SOUND_REC_RECORD_NUMBER) {
        if (recData) {
            free(recData);
            recData = nullptr;
        }
        if (soundRecStreamToSd(path)) {
            delay(1600);
            drawHardwareMenu();
            return;
        }
        recData = allocSoundRecFallbackBuffer(recordChunks);
    }
    if (!recData) {
        drawMessage("REC RAM FAIL", "NO AUDIO BUFFER");
        delay(1500);
        drawHardwareMenu();
        return;
    }

    uint64_t maxSamples = recordChunks * SOUND_REC_RECORD_LENGTH;
    uint64_t maxDataBytes = maxSamples * SOUND_REC_BYTES_PER_SAMPLE;

    drawProgressBar(60, "CONFIGURING PDM MIC", CP_CYAN);

    if (!startSoundRecMic()) {
        free(recData);
        drawMessage("MIC INIT FAIL", "SOUND REC ABORTED");
        delay(1500);
        drawHardwareMenu();
        return;
    }

    bool recordFailed = false;
    bool stopRequested = false;
    bool stopKeysArmed = false;
    bool lastStopDown = true;
    uint64_t queuedChunks = 0;
    uint64_t completedChunks = 0;
    soundRecLivePeak = 0;
    soundRecMaxPeak = 0;
    resetAudioSpectrum();
    unsigned long startMs = millis();
    unsigned long lastDrawMs = 0;
    drawSoundRecScreen(nullptr, 0, 0, maxSamples, maxDataBytes);

    for (size_t i = 0; i < recordChunks; i++) {
        M5Cardputer.update();
        if (pollSoundRecStop(startMs, stopKeysArmed, lastStopDown)) {
            stopRequested = true;
            break;
        }

        int16_t* data = &recData[i * SOUND_REC_RECORD_LENGTH];
        memset(data, 0, SOUND_REC_RECORD_LENGTH * sizeof(int16_t));
        if (!M5Cardputer.Mic.record(data, SOUND_REC_RECORD_LENGTH, SOUND_REC_SAMPLE_RATE, false)) {
            recordFailed = true;
            break;
        }
        queuedChunks++;
        if (queuedChunks > 2) {
            consumeSoundRecChunkForUi(&recData[completedChunks * SOUND_REC_RECORD_LENGTH], SOUND_REC_RECORD_LENGTH);
            completedChunks++;
        }

        unsigned long now = millis();
        if (now - lastDrawMs >= SOUND_REC_DRAW_INTERVAL_MS) {
            lastDrawMs = now;
            drawSoundRecScreen(nullptr, 0, completedChunks * SOUND_REC_RECORD_LENGTH, maxSamples, maxDataBytes);
        }
    }

    while (M5Cardputer.Mic.isRecording()) {
        M5Cardputer.update();
        delay(1);
    }
    restoreSoundRecAudio();

    while (completedChunks < queuedChunks) {
        consumeSoundRecChunkForUi(&recData[completedChunks * SOUND_REC_RECORD_LENGTH], SOUND_REC_RECORD_LENGTH);
        completedChunks++;
        drawSoundRecScreen(nullptr, 0, completedChunks * SOUND_REC_RECORD_LENGTH, maxSamples, maxDataBytes);
    }

    uint64_t capturedSamples = completedChunks * SOUND_REC_RECORD_LENGTH;
    drawSoundRecScreen(nullptr, 0, capturedSamples, maxSamples, maxDataBytes);

    uint32_t dataBytes = (uint32_t)(capturedSamples * SOUND_REC_BYTES_PER_SAMPLE);
    uint8_t wavHeader[44];
    buildSoundRecWavHeader(wavHeader, dataBytes);
    bool fileSaved = false;
    if (dataBytes > 0) {
        SD.remove(path.c_str());
        File recFile = SD.open(path.c_str(), FILE_WRITE);
        if (recFile) {
            size_t headerBytes = recFile.write(wavHeader, sizeof(wavHeader));
            size_t dataWritten = recFile.write((const uint8_t*)recData, dataBytes);
            recFile.flush();
            recFile.close();
            fileSaved = headerBytes == sizeof(wavHeader) && dataWritten == dataBytes;
        }
    }
    free(recData);

    if (!fileSaved || dataBytes == 0) {
        SD.remove(path.c_str());
        Serial.printf("[SOUNDREC] save failed bytes=%lu max=%u\n", (unsigned long)dataBytes, soundRecMaxPeak);
        drawMessage("SOUND REC FAIL", "NO WAV SAVED");
    } else {
        String fileName = path.substring(path.lastIndexOf('/') + 1);
        Serial.printf("[SOUNDREC] saved %s bytes=%lu max=%u mic=PDM gpio_data=46 gpio_clk=43\n", path.c_str(), (unsigned long)dataBytes, soundRecMaxPeak);
        if (soundRecMaxPeak < 24) {
            drawMessage("MIC FLATLINE", fileName);
        } else if (stopRequested || recordFailed || queuedChunks < recordChunks) {
            drawMessage("PARTIAL WAV SAVED", fileName);
        } else {
            drawMessage("REC SAVED PK " + String(soundRecMaxPeak), fileName);
        }
    }
    delay(1600);
    drawHardwareMenu();
}

int32_t cachedBatteryLevel = -1;
int16_t cachedBatteryVoltageMv = 0;
unsigned long lastBatteryReadMs = 0;

int32_t estimateBatteryLevelFromVoltage(int16_t voltageMv) {
    if (voltageMv <= 0) return -1;

    static const int16_t volts[] = {3300, 3500, 3600, 3700, 3740, 3790, 3850, 3920, 4000, 4100, 4200};
    static const int8_t pct[] =    {0,    10,   20,   30,   40,   50,   60,   70,   80,   90,   100};
    if (voltageMv <= volts[0]) return 0;
    for (size_t i = 1; i < sizeof(volts) / sizeof(volts[0]); i++) {
        if (voltageMv <= volts[i]) {
            int32_t dv = voltageMv - volts[i - 1];
            int32_t span = volts[i] - volts[i - 1];
            return pct[i - 1] + (dv * (pct[i] - pct[i - 1])) / span;
        }
    }
    return 100;
}

void updateBatteryStatus() {
    unsigned long now = millis();
    if (lastBatteryReadMs != 0 && now - lastBatteryReadMs < 1000) return;

    cachedBatteryVoltageMv = M5Cardputer.Power.getBatteryVoltage();
    cachedBatteryLevel = estimateBatteryLevelFromVoltage(cachedBatteryVoltageMv);
    if (cachedBatteryLevel < 0) {
        cachedBatteryLevel = M5Cardputer.Power.getBatteryLevel();
    }
    if (cachedBatteryLevel < 0 || cachedBatteryLevel > 100) {
        cachedBatteryLevel = estimateBatteryLevelFromVoltage(cachedBatteryVoltageMv);
    }
    lastBatteryReadMs = now;
}

String formatBatteryPercent(int32_t level) {
    if (level < 0) return "--%";
    if (level > 100) level = 100;
    return String(level) + "%";
}

String formatBatteryVoltage(int16_t voltageMv) {
    if (voltageMv <= 0) return "--.-- V";
    int whole = voltageMv / 1000;
    int centi = (voltageMv % 1000) / 10;
    String frac = centi < 10 ? "0" + String(centi) : String(centi);
    return String(whole) + "." + frac + " V";
}

void drawBatteryPercentBox() {
    updateBatteryStatus();
    uint16_t color = cachedBatteryLevel >= 0 && cachedBatteryLevel <= 20 ? CP_RED : CP_CYAN;
    int x = 200;
    int y = 2;
    int w = 38;
    int h = 15;
    int chip = 5;
    canvas.drawLine(x, y, x + w, y, color);
    canvas.drawLine(x + w, y, x + w, y + h, color);
    canvas.drawLine(x + chip, y + h, x + w, y + h, color);
    canvas.drawLine(x, y, x, y + h - chip, color);
    canvas.drawLine(x, y + h - chip, x + chip, y + h, color);
    canvas.setTextColor(color);
    canvas.setTextSize(1);
    canvas.drawCenterString(formatBatteryPercent(cachedBatteryLevel), 219, 6);
}

void enterChargingMode();

void drawBatteryStatusScreen() {
    updateBatteryStatus();

    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_CYAN);
    drawChippedButton(8, 7, 224, 120, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- BATTERY STATUS ---", 120, 12);

    canvas.setTextColor(CP_CYAN);
    canvas.setCursor(22, 44);
    canvas.print("VOLTAGE  ");
    canvas.print(formatBatteryVoltage(cachedBatteryVoltageMv));

    canvas.setCursor(22, 64);
    canvas.print("CAPACITY ");
    canvas.print(formatBatteryPercent(cachedBatteryLevel));

    drawChippedButton(42, 86, 156, 18, CP_YELLOW);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("CHARGING MODE", 120, 91);

    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("ENTER CHARGE | DEL/ESC BACK", 120, 112);
    suppressBatteryPercentBox = true;
    pushCanvas();
    suppressBatteryPercentBox = false;
}

void showBatteryStatus() {
    unsigned long lastDraw = millis() - 500;
    bool exitKeysArmed = false;
    while (true) {
        M5Cardputer.update();
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        bool chargeDown = status.enter;
        bool exitDown = status.del;
        for (char c : status.word) {
            if (c == '`' || c == ',') exitDown = true;
        }
        if (!exitKeysArmed) {
            exitKeysArmed = !chargeDown && !exitDown;
        } else if (chargeDown) {
            playSound(sound_select, sound_select_size);
            enterChargingMode();
            exitKeysArmed = false;
            lastDraw = millis() - 500;
            continue;
        } else if (exitDown) {
            break;
        }

        unsigned long now = millis();
        if (now - lastDraw >= 500) {
            lastDraw = now;
            drawBatteryStatusScreen();
        }
        delay(25);
    }
    drawHardwareMenu();
}

void drawHardwareMenu() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Compact header matches the other node menus.
    drawGlitchText("HARDWARE NODE", 72, 4, 1, CP_CYAN, true, true);
    
    // Draw rotating wheel arc
    canvas.drawCircle(-80, 67, 110, CP_DIM);
    canvas.drawCircle(-80, 67, 109, CP_DIM);
    
    int totalItems = 8;
    std::vector<String> labels = {"FLASH MEMORY", "SD CARD", "USB DRIVE", "BADUSB", "MUSIC PLAYER", "SOUND REC", "BATTERY STATUS", "BACK"};
    
    for (int i = 0; i < totalItems; i++) {
        float rawOffset = i - currentHardwareScroll;
        float offset = fmod(rawOffset, (float)totalItems);
        float halfItems = (float)totalItems / 2.0;
        if (offset > halfItems) offset -= (float)totalItems;
        if (offset < -halfItems) offset += (float)totalItems;
        
        // Keep the upper row visible like NETWORK NODE.
        if (abs(offset) > 1.5) continue;
        
        float angle = offset * 0.391;
        float tickY = 67 + sin(angle) * 110;
        float tickX = -80 + cos(angle) * 110;
        
        bool isSelected = (i == hardwareMenuFocus);
        uint16_t tColor = isSelected ? CP_CYAN : CP_DIM;
        
        float tickEndX = -80 + cos(angle) * (isSelected ? 117 : 115);
        float tickEndY = 67 + sin(angle) * (isSelected ? 117 : 115);
        
        canvas.drawLine(tickX, tickY, tickEndX, tickEndY, tColor);
        canvas.drawLine(tickX, tickY - 1, tickEndX, tickEndY - 1, tColor);
        if (isSelected) {
            canvas.drawLine(tickX, tickY + 1, tickEndX, tickEndY + 1, tColor);
        }
        
        float scale = 1.0 - abs(offset) * 0.3333;
        if (scale < 0.1) scale = 0.1;
        float h = 30.0 * scale;
        float y = tickY - h / 2.0;
        float w = 195.0 * scale;
        float x = tickX + 10;
        
        int textSize = isSelected ? 2 : 1;
        uint16_t color = isSelected ? CP_YELLOW : CP_DIM;
        
        drawChippedButton(x, y, w, h, color);
        canvas.setTextColor(color);
        canvas.setTextSize(textSize);
        
        float textY = y + (isSelected ? 7 : 6);
        float textX = x + 15;
        canvas.setCursor(textX, textY);
        canvas.print(labels[i]);
    }

    drawWheelPositionIndicator(currentHardwareScroll, totalItems);

    if (hardwareDescAnimWidth >= 10.0) {
        int x = 40;
        int y = 52;
        int h = 30;
        canvas.fillRect(x, y, (int)hardwareDescAnimWidth, h, CP_BG);
        drawChippedButton(x, y, (int)hardwareDescAnimWidth, h, CP_YELLOW);
        
        if (hardwareDescAnimWidth > 160.0) {
            canvas.setTextColor(CP_YELLOW);
            String label = labels[hardwareMenuFocus];
            String line1 = "";
            String line2 = "";
            if (label == "FLASH MEMORY") {
                line1 = "Flash";
                line2 = "memory";
            } else if (label == "SD CARD") {
                line1 = "SD card";
                line2 = "storage";
            } else if (label == "USB DRIVE") {
                line1 = "SD over";
                line2 = "USB";
            } else if (label == "BADUSB") {
                line1 = "Ducky";
                line2 = "executor";
            } else if (label == "MUSIC PLAYER") {
                line1 = "Offline";
                line2 = "player";
            } else if (label == "SOUND REC") {
                line1 = "Mic WAV";
                line2 = "recorder";
            } else if (label == "BATTERY STATUS") {
                line1 = "Battery";
                line2 = "status";
            } else if (label == "BACK") {
                line1 = "Return to";
                line2 = "terminal";
            }
            
            if (line1 != "") {
                canvas.setTextSize(2);
                canvas.setCursor(x + 10, y + 0);
                canvas.print(line1);
                canvas.setCursor(x + 10, y + 14);
                canvas.print(line2);
            }
        }
    }
    
    pushCanvas();
}

void handleHardwareMenuInput(Keyboard_Class::KeysState status) {
    bool hasUp = false, hasDown = false;
    bool hasLeft = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
        if (c == ',') hasLeft = true;
    }
    
    if (showHardwareDesc) {
        if (hasLeft || hasUp || hasDown) {
            playSound(sound_select, sound_select_size);
            showHardwareDesc = false;
            return;
        }
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        showHardwareDesc = false;
        hardwareDescAnimWidth = 0.0;
        
        if (hardwareMenuFocus == 0) {
            isSDCardManager = false;
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            showFileContent = false;
        } else if (hardwareMenuFocus == 1) {
            isSDCardManager = true;
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            showFileContent = false;
        } else if (hardwareMenuFocus == 2) {
            enterUsbDriveMode();
        } else if (hardwareMenuFocus == 3) {
            enterBadUsbMode();
        } else if (hardwareMenuFocus == 4) {
            appState = STATE_MUSIC_PLAYER;
            playlistFocus = 0;
            playlistScrollOffset = 0;
            populatePlaylist();
            drawMusicPlayer();
        } else if (hardwareMenuFocus == 5) {
            soundRec();
        } else if (hardwareMenuFocus == 6) {
            showBatteryStatus();
        } else if (hardwareMenuFocus == 7) {
            appState = STATE_SPLASH;
            showSplashBootMenu = true;
            splashBootFocus = 1;
            resetSplashBootScroll();
            logOffset = 0;
            drawSplash();
        }
        return;
    }
    
    if (!showHardwareDesc) {
        int maxFocus = 7;
        if (hasUp) {
            playSound(sound_hover, sound_hover_size);
            hardwareMenuFocus--;
            if (hardwareMenuFocus < 0) hardwareMenuFocus = maxFocus;
            targetHardwareScroll -= 1.0;
        }
        if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            hardwareMenuFocus++;
            if (hardwareMenuFocus > maxFocus) hardwareMenuFocus = 0;
            targetHardwareScroll += 1.0;
        }
    }
}

bool chargingModeAnyKeyDown() {
    auto status = M5Cardputer.Keyboard.keysState();
    if (status.enter || status.del) return true;
    for (char c : status.word) {
        (void)c;
        return true;
    }
    return false;
}

void enterChargingMode() {
    drawMessage("CHARGING MODE", "ANY KEY WAKES");
    delay(700);

    M5Cardputer.update();
    bool wakeKeysArmed = !chargingModeAnyKeyDown();

    suppressBatteryPercentBox = true;
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    pushCanvas();
    M5Cardputer.Display.sleep();

    while (true) {
        M5Cardputer.update();
        bool keyDown = chargingModeAnyKeyDown();
        if (!wakeKeysArmed) {
            wakeKeysArmed = !keyDown;
        } else if (keyDown) {
            break;
        }
        delay(25);
    }

    M5Cardputer.Display.wakeup();
    M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
    suppressBatteryPercentBox = false;
}

void drawHardwareSettings() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- SYSTEM CONFIG NODE ---", 120, 10);
    
    // Adjust settingsTabScrollOffset
    if (settingsTab < settingsTabScrollOffset) {
        settingsTabScrollOffset = settingsTab;
    } else if (settingsTab >= settingsTabScrollOffset + 3) {
        settingsTabScrollOffset = settingsTab - 2;
    }
    
    // Draw scrollable tabs
    int tabX[3] = {18, 86, 154};
    int tabW[3] = {64, 64, 64};
    String tabNames[6] = {"HARDWARE", "NETWORK", "OFFLINE", "  OTA  ", "APPEAR", "CREDITS"};
    for (int t = 0; t < 3; t++) {
        int idx = settingsTabScrollOffset + t;
        if (idx >= 6) break;
        
        bool isActive = (settingsTab == idx);
        bool hasFocus = (settingsFocus == -1 && isActive);
        
        uint16_t borderCol = hasFocus ? CP_YELLOW : (isActive ? CP_CYAN : CP_DIM);
        canvas.drawRect(tabX[t], 22, tabW[t], 14, borderCol);
        if (hasFocus) {
            canvas.fillRect(tabX[t] + 1, 23, tabW[t] - 2, 12, canvas.color565(30, 30, 20));
        }
        canvas.setTextColor(isActive ? CP_YELLOW : WHITE);
        // Center text in smaller tabs: width of character size 1 is 6 pixels.
        // E.g. "NETWORK" (7 chars * 6 = 42 pixels), tab width 64 -> center offset (64 - 42) / 2 = 11 pixels!
        int textOffset = (64 - tabNames[idx].length() * 6) / 2;
        canvas.setCursor(tabX[t] + textOffset, 25);
        canvas.print(tabNames[idx]);
    }
    
    // Draw concealed indicators next to tabs on the sides
    if (settingsTabScrollOffset > 0) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawChar('<', 11, 25);
    }
    if (settingsTabScrollOffset < 3) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawChar('>', 221, 25);
    }
    
    // Determine rowCount
    int rowCount = 3;
    if (settingsTab == 1) rowCount = 2; // NETWORK: SSID, PASSWORD (scan nets removed)
    else if (settingsTab == 2) rowCount = 2; // OFFLINE: PLAY LOOP, MUSIC DIR
    else if (settingsTab == 3) rowCount = 1; // OTA: SORT BY ONLY (open list removed)
    else if (settingsTab == 4) rowCount = 4; // APPEARANCE: GLITCH TEXT, BRIGHTNESS, VOLUME, CHARGING MODE
    else if (settingsTab == 5) rowCount = 2; // CREDITS: OPEN CREDITS, GITHUB QR
    
    int startY = 41;
    
    for (int i = 0; i < rowCount; i++) {
        bool isFocus = (settingsFocus == i);
        uint16_t borderCol = isFocus ? CP_YELLOW : CP_DIM;
        int rowY = startY + i * 17;
        
        canvas.fillRect(15, rowY, 210, 15, isFocus ? canvas.color565(30, 30, 30) : CP_BG);
        canvas.drawRect(15, rowY, 210, 15, borderCol);
        
        canvas.setTextColor(isFocus ? CP_YELLOW : WHITE);
        canvas.setCursor(22, rowY + 3);
        
        if (settingsTab == 0) { // HARDWARE
            if (i == 0) {
                canvas.print("SORT BY:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print(currentSortField == SORT_FIELD_NAME ? "< NAME >" : "< TYPE >");
            } else if (i == 1) {
                canvas.print("ORDER:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print(currentSortOrder == SORT_ORDER_ASC ? "< ASCENDING >" : "< DESCENDING >");
            } else if (i == 2) {
                canvas.print("SYS FILES:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print(showSystemFiles ? "< SHOW >" : "< HIDE >");
            }
        } else if (settingsTab == 1) { // NETWORK
            if (i == 0) {
                canvas.print("WIFI SSID:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String displaySsid = savedSSID == "" ? "< NONE >" : savedSSID;
                if (displaySsid.length() > 14) displaySsid = displaySsid.substring(0, 11) + "...";
                canvas.print(displaySsid);
            } else if (i == 1) {
                canvas.print("WIFI PASS:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String displayPass = savedWifiPass == "" ? "< NONE >" : "********";
                canvas.print(displayPass);
            }
        } else if (settingsTab == 2) { // OFFLINE
            if (i == 0) {
                canvas.print("PLAY LOOP:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                if (mp3PlayLoopMode == "random") canvas.print("< RANDOM >");
                else canvas.print("< BY NAME >");
            } else if (i == 1) {
                canvas.print("MUSIC DIR:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String displayDir = musicDir;
                if (displayDir.length() > 14) displayDir = displayDir.substring(0, 11) + "...";
                canvas.print(displayDir + " [SET]");
            }
        } else if (settingsTab == 3) { // OTA
            if (i == 0) {
                canvas.print("SORT BY:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                if (otaSortField == "downloads") canvas.print("< DOWNLOADS >");
                else if (otaSortField == "date") canvas.print("< TIME >");
                else canvas.print("< NAME >");
            }
        } else if (settingsTab == 4) { // APPEARANCE
            if (i == 0) {
                canvas.print("GLITCH TEXT:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                String glitchLabel = "";
                if (insaneMode == 0) glitchLabel = "< OFF >";
                else if (insaneMode == 1) glitchLabel = "< ON >";
                else glitchLabel = "< INSANE >";
                canvas.print(glitchLabel);
            } else if (i == 1) {
                canvas.print("BRIGHTNESS:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< " + String(globalBrightness) + "% >");
            } else if (i == 2) {
                canvas.print("VOLUME:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< " + String(globalVolume) + "% >");
            } else if (i == 3) {
                canvas.print("CHARGING MODE:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< ENTER >");
            }
        } else if (settingsTab == 5) { // CREDITS
            if (i == 0) {
                canvas.print("CREDITS:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< OPEN >");
            } else if (i == 1) {
                canvas.print("GITHUB QR:");
                canvas.setCursor(120, rowY + 3);
                canvas.setTextColor(isFocus ? WHITE : CP_DIM);
                canvas.print("< OPEN >");
            }
        }
    }
    
    pushCanvas();
}

void handleHardwareSettingsInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_SPLASH;
        showSplashBootMenu = true;
        splashBootFocus = 3;
        resetSplashBootScroll();
        logOffset = 0;
        drawSplash();
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
    
    int maxFocus = 2;
    if (settingsTab == 1) maxFocus = 1; // NETWORK: SSID, PASSWORD
    else if (settingsTab == 2) maxFocus = 1; // OFFLINE: PLAY LOOP, MUSIC DIR
    else if (settingsTab == 3) maxFocus = 0; // OTA: SORT BY
    else if (settingsTab == 4) maxFocus = 3; // APPEARANCE: GLITCH TEXT, BRIGHTNESS, VOLUME, CHARGING MODE
    else if (settingsTab == 5) maxFocus = 1; // CREDITS: OPEN CREDITS, GITHUB QR
    
    if (settingsFocus == -1) {
        if (hasLeft) {
            playSound(sound_hover, sound_hover_size);
            settingsTab = (settingsTab - 1 + 6) % 6;
            drawHardwareSettings();
        } else if (hasRight) {
            playSound(sound_hover, sound_hover_size);
            settingsTab = (settingsTab + 1) % 6;
            drawHardwareSettings();
        } else if (status.enter && settingsTab == 5) {
            playSound(sound_select, sound_select_size);
            appState = STATE_CREDITS;
            drawCreditsScreen();
        } else if (hasDown) {
            playSound(sound_hover, sound_hover_size);
            settingsFocus = 0;
            drawHardwareSettings();
        }
        return;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        if (settingsFocus == 0) {
            settingsFocus = -1;
        } else {
            settingsFocus--;
        }
        drawHardwareSettings();
    } else if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        if (settingsFocus < maxFocus) {
            settingsFocus++;
            drawHardwareSettings();
        }
    }
    
    if (hasLeft || hasRight) {
        playSound(sound_hover, sound_hover_size);
        if (settingsTab == 0) { // HARDWARE
            if (settingsFocus == 0) {
                currentSortField = (currentSortField == SORT_FIELD_NAME) ? SORT_FIELD_TYPE : SORT_FIELD_NAME;
            } else if (settingsFocus == 1) {
                currentSortOrder = (currentSortOrder == SORT_ORDER_ASC) ? SORT_ORDER_DESC : SORT_ORDER_ASC;
            } else if (settingsFocus == 2) {
                showSystemFiles = !showSystemFiles;
            }
        } else if (settingsTab == 2) { // OFFLINE
            if (settingsFocus == 0) {
                if (mp3PlayLoopMode == "name") mp3PlayLoopMode = "random";
                else mp3PlayLoopMode = "name";
                prefs.putString("loopMode", mp3PlayLoopMode);
            }
        } else if (settingsTab == 3) { // OTA Sorting
            if (settingsFocus == 0) { // SORT BY is now row 0
                if (hasLeft) {
                    if (otaSortField == "downloads") otaSortField = "name";
                    else if (otaSortField == "date") otaSortField = "downloads";
                    else otaSortField = "date";
                } else {
                    if (otaSortField == "downloads") otaSortField = "date";
                    else if (otaSortField == "date") otaSortField = "name";
                    else otaSortField = "downloads";
                }
                otaCatalogPage = 1;
                otaCatalogLoaded = false; // refresh next fetch
            }
        } else if (settingsTab == 4) { // APPEARANCE
            if (settingsFocus == 0) {
                if (hasLeft) {
                    insaneMode = (insaneMode - 1 + 3) % 3;
                } else {
                    insaneMode = (insaneMode + 1) % 3;
                }
            } else if (settingsFocus == 1) {
                if (hasLeft) {
                    globalBrightness -= 5;
                    if (globalBrightness < 5) globalBrightness = 5;
                } else {
                    globalBrightness += 5;
                    if (globalBrightness > 100) globalBrightness = 100;
                }
                M5Cardputer.Display.setBrightness((globalBrightness * 255) / 100);
                prefs.putInt("brightness", globalBrightness);
            } else if (settingsFocus == 2) {
                if (hasLeft) {
                    globalVolume -= 5;
                    if (globalVolume < 0) globalVolume = 0;
                } else {
                    globalVolume += 5;
                    if (globalVolume > 100) globalVolume = 100;
                }
                M5Cardputer.Speaker.setVolume((globalVolume * 255) / 100);
                prefs.putInt("volume", globalVolume);
            }
        }
        drawHardwareSettings();
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (settingsTab == 5 && settingsFocus == 0) {
            appState = STATE_CREDITS;
            drawCreditsScreen();
        } else if (settingsTab == 5 && settingsFocus == 1) {
            appState = STATE_GITHUB_QR;
            drawGithubQrScreen();
        } else if (settingsTab == 4 && settingsFocus == 3) {
            enterChargingMode();
            drawHardwareSettings();
        } else if (settingsTab == 2 && settingsFocus == 1) { // OFFLINE: MUSIC DIR -> choose folder location automatically
            isDirSelectionMode = true;
            isSDCardManager = true; // Hardcode to SD card browsing as requested
            appState = STATE_FILE_LOADING;
            loadingProgress = 0;
            fileManagerCurrentPath = "/";
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            showFileContent = false;
        } else { // Exit to Splash screen (Boot Node)
            prefs.putInt("insane", insaneMode);
            populateFileList();
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            appState = STATE_SPLASH; // go directly to splash menu!
            showSplashBootMenu = true;
            splashBootFocus = 3;
            resetSplashBootScroll();
            drawSplash();
        }
    }
}

void drawFileManager(bool push) {
    if (showImage) {
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        
        canvas.drawRect(5, 5, 230, 125, CP_CYAN);
        canvas.drawRect(7, 7, 226, 121, CP_DIM);
        
        String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + openedImageName;
        
        if (isSDCardManager) {
            canvas.drawJpgFile(SD, fullPath.c_str(), 8, 8, 224, 105, 0, 0, imageScale, imageScale);
        } else {
            canvas.drawJpgFile(SPIFFS, fullPath.c_str(), 8, 8, 224, 105, 0, 0, imageScale, imageScale);
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        char scaleStr[48];
        sprintf(scaleStr, "ZOOM: %.2fx | UP/DOWN: ZOOM | ESC: EXIT", imageScale);
        canvas.drawCenterString(scaleStr, 120, 115);
        
        if (push) {
            pushCanvas();
        }
        return;
    }
    
    if (isMp3Playing) {
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        
        canvas.drawRect(5, 5, 230, 125, CP_CYAN);
        canvas.drawRect(7, 7, 226, 121, CP_DIM);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.setTextSize(1);
        canvas.drawCenterString("--- NEURAL MUSIC LINK ---", 120, 12);
        canvas.drawLine(10, 24, 230, 24, CP_CYAN);
        
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString("NOW DECODING:", 120, 32);
        
        canvas.setTextColor(WHITE);
        String nameDisp = mp3Filename;
        if (nameDisp.length() > 22) {
            nameDisp = nameDisp.substring(0, 19) + "...";
        }
        canvas.drawCenterString(nameDisp, 120, 44);
        
        // Progress Bar
        canvas.drawRect(40, 56, 160, 4, CP_DIM);
        uint32_t elapsed = (millis() - mp3StartTime) / 1000;
        uint32_t duration = mp3DurationSeconds;
        int progressW = (elapsed * 160) / duration;
        if (progressW > 160) progressW = 160;
        canvas.fillRect(40, 56, progressW, 4, CP_CYAN);
        
        // Time Label
        char timeStr[32];
        sprintf(timeStr, "%02d:%02d / %02d:%02d", 
                (int)(elapsed / 60), (int)(elapsed % 60), 
                (int)(duration / 60), (int)(duration % 60));
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString(timeStr, 120, 64);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("LIVE SPECTRUM", 120, 78);
        drawAudioSpectrum(18, 106, 204, 28);
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ANY KEY TO STOP", 120, 114);
        
        if (push) {
            pushCanvas();
        }
        return;
    }
    
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    
    // Draw title and outer frames
    canvas.drawRect(5, 5, 230, 125, CP_CYAN);
    canvas.drawRect(7, 7, 226, 121, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    String titlePrefix = isDirSelectionMode ? "[DIR] " : (isSDCardManager ? "SD:" : "FLASH:");
    String title = titlePrefix + fileManagerCurrentPath;
    if (title.length() > 30) {
        title = titlePrefix + "..." + fileManagerCurrentPath.substring(fileManagerCurrentPath.length() - (30 - titlePrefix.length() - 3));
    }
    canvas.drawCenterString("--- " + title + " ---", 120, 12);
    canvas.drawLine(10, 24, 230, 24, CP_CYAN);
    
    if (!showFileContent) {
        bool folderEmpty = fsStatusMessage == "folder empty press esc";
        bool onlyParent = loadedFiles.size() == 1 && loadedFiles[0].name == "..";
        if ((fsStatusMessage != "" && loadedFiles.empty()) || folderEmpty || onlyParent) {
            canvas.setTextColor(folderEmpty || onlyParent ? CP_YELLOW : CP_RED);
            canvas.drawCenterString(folderEmpty || onlyParent ? "folder empty press esc" : fsStatusMessage, 120, 62);
            canvas.setTextColor(CP_DIM);
            canvas.drawCenterString("NO DEMO FILES", 120, 78);
        } else {
            if (fsStatusMessage != "") {
                canvas.setTextColor(CP_YELLOW);
                canvas.setCursor(15, 27);
                canvas.print(fsStatusMessage);
            }
            
            // Draw file list with scroll viewport paging
            int startY = fsStatusMessage != "" ? 42 : 32;
            int maxShow = min(5, (int)loadedFiles.size() - fileManagerScrollOffset);
            for (int i = 0; i < maxShow; i++) {
                int fileIdx = fileManagerScrollOffset + i;
                bool isSel = (fileIdx == fileManagerSelected);
                uint16_t color = isSel ? CP_YELLOW : WHITE;
                
                if (isSel) {
                    canvas.fillRect(10, startY - 2, 210, 14, canvas.color565(30, 30, 30));
                    canvas.drawRect(10, startY - 2, 210, 14, CP_CYAN);
                }
                
                canvas.setTextColor(color);
                canvas.setCursor(15, startY);
                
                // Truncate name if it's too long to fit with the scrollbar, or marquee scroll if selected
                String nameToPrint = loadedFiles[fileIdx].name;
                if (isSel && nameToPrint.length() > 18) {
                    if (millis() - lastFileSelectionTime > 1000) {
                        int maxScroll = nameToPrint.length() - 18;
                        if (marqueeScrollOffset <= maxScroll) {
                            nameToPrint = nameToPrint.substring(marqueeScrollOffset);
                        } else {
                            if (millis() - lastFileSelectionTime > 1000 + maxScroll * 250 + 1000) {
                                lastFileSelectionTime = millis();
                                marqueeScrollOffset = 0;
                            }
                            nameToPrint = nameToPrint.substring(maxScroll);
                        }
                    }
                    if (nameToPrint.length() > 18) {
                        nameToPrint = nameToPrint.substring(0, 18);
                    }
                } else if (nameToPrint.length() > 18) {
                    nameToPrint = nameToPrint.substring(0, 15) + "...";
                }
                canvas.print(nameToPrint);
                
                canvas.setCursor(155, startY);
                canvas.print(loadedFiles[fileIdx].sizeStr);
                
                startY += 14;
            }
            
            // Draw vertical scrollbar on the right side (x = 224)
            int totalFiles = loadedFiles.size();
            int trackY = fsStatusMessage != "" ? 42 : 32;
            int trackH = fsStatusMessage != "" ? 66 : 70;
            canvas.drawFastVLine(224, trackY, trackH, CP_DIM);
            if (totalFiles > 5) {
                int barH = (trackH * 5) / totalFiles;
                if (barH < 10) barH = 10;
                int scrollRange = totalFiles - 5;
                int trackRange = trackH - barH;
                int barY = trackY + (fileManagerScrollOffset * trackRange) / scrollRange;
                canvas.fillRect(223, barY, 3, barH, CP_CYAN);
            } else if (totalFiles > 0) {
                canvas.fillRect(223, trackY, 3, trackH, CP_CYAN);
            }
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString(isDirSelectionMode ? "ENTER: CHOOSE FOLDER | ESC: BACK" : "ENTER: OPEN  |  ESC/COMMA: BACK", 120, 114);
    } else {
        // Draw selected file content panel
        canvas.setTextColor(CP_CYAN);
        canvas.setCursor(15, 32);
        canvas.print("FILE: " + openedFileName);
        canvas.drawLine(12, 44, 228, 44, CP_CYAN);
        
        canvas.setTextColor(WHITE);
        int startY = 50;
        for (size_t i = 0; i < openedFileContent.size(); i++) {
            canvas.setCursor(15, startY);
            canvas.print(openedFileContent[i]);
            startY += 12;
        }
        
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS COMMA OR ESC TO CLOSE", 120, 114);
    }
    
    if (push) {
        pushCanvas();
    }
}

void handleFileManagerInput(Keyboard_Class::KeysState status) {
    if (showImage) {
        bool hasZoomIn = false, hasZoomOut = false;
        bool hasExit = false;
        for (char c : status.word) {
            if (c == ';') hasZoomIn = true;
            if (c == '.') hasZoomOut = true;
            if (c == ',' || c == '`') hasExit = true;
        }
        if (status.enter || status.del) hasExit = true;
        
        if (hasZoomIn) {
            playSound(sound_hover, sound_hover_size);
            imageScale += 0.25f;
            if (imageScale > 4.0f) imageScale = 4.0f;
            drawFileManager();
        } else if (hasZoomOut) {
            playSound(sound_hover, sound_hover_size);
            imageScale -= 0.25f;
            if (imageScale < 0.25f) imageScale = 0.25f;
            drawFileManager();
        } else if (hasExit) {
            playSound(sound_select, sound_select_size);
            showImage = false;
            drawFileManager();
        }
        return;
    }
    
    if (isMp3Playing) {
        // Any keypress stops the MP3 playback
        playSound(sound_select, sound_select_size);
        stopMp3();
        return;
    }
    
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (showFileContent) {
        if (hasBack || status.enter) {
            playSound(sound_select, sound_select_size);
            showFileContent = false;
            drawFileManager();
        }
        return;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (fileManagerCurrentPath != "/") {
            int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
            if (lastSlash == 0) {
                fileManagerCurrentPath = "/";
            } else if (lastSlash > 0) {
                fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
            }
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            populateFileList();
            drawFileManager();
        } else {
            if (isDirSelectionMode) {
                isDirSelectionMode = false;
                appState = STATE_HARDWARE_SETTINGS;
                drawHardwareSettings();
            } else {
                appState = STATE_HARDWARE_MENU;
                drawHardwareMenu();
            }
        }
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (loadedFiles.empty()) {
            if (!isDirSelectionMode && isSDCardManager) {
                appState = STATE_FILE_ACTIONS_MENU;
                fileActionsMenuSelected = 6;
                drawFileActionsMenu();
            } else {
                drawFileManager();
            }
            return;
        }
        if (loadedFiles[fileManagerSelected].name == "..") {
            if (!isDirSelectionMode && isSDCardManager && loadedFiles.size() == 1) {
                appState = STATE_FILE_ACTIONS_MENU;
                fileActionsMenuSelected = 6;
                drawFileActionsMenu();
                return;
            }
            int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
            if (lastSlash == 0) {
                fileManagerCurrentPath = "/";
            } else if (lastSlash > 0) {
                fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
            }
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
            populateFileList();
            drawFileManager();
        } else if (isDirSelectionMode) {
            if (loadedFiles[fileManagerSelected].isDir) {
                pendingSelectedDir = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + loadedFiles[fileManagerSelected].name;
                appState = STATE_DIR_CONFIRM_POPUP;
                drawDirConfirmPopup();
            } else {
                playSound(sound_fail, sound_fail_size);
            }
        } else {
            appState = STATE_FILE_ACTIONS_MENU;
            fileActionsMenuSelected = 0;
            drawFileActionsMenu();
        }
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    int maxIdx = loadedFiles.empty() ? 0 : loadedFiles.size() - 1;
    
    if (hasUp && maxIdx > 0) {
        playSound(sound_hover, sound_hover_size);
        fileManagerSelected--;
        if (fileManagerSelected < 0) {
            fileManagerSelected = maxIdx;
            if (maxIdx > 4) {
                fileManagerScrollOffset = maxIdx - 4;
            } else {
                fileManagerScrollOffset = 0;
            }
        } else {
            if (fileManagerSelected < fileManagerScrollOffset) {
                fileManagerScrollOffset = fileManagerSelected;
            }
        }
        lastFileSelectionTime = millis();
        marqueeScrollOffset = 0;
        drawFileManager();
    }
    if (hasDown && maxIdx > 0) {
        playSound(sound_hover, sound_hover_size);
        fileManagerSelected++;
        if (fileManagerSelected > maxIdx) {
            fileManagerSelected = 0;
            fileManagerScrollOffset = 0;
        } else {
            if (fileManagerSelected > fileManagerScrollOffset + 4) {
                fileManagerScrollOffset = fileManagerSelected - 4;
            }
        }
        lastFileSelectionTime = millis();
        marqueeScrollOffset = 0;
        drawFileManager();
    }
}

void stopMp3Playback() {
    isMp3Playing = false;
    mp3IsPaused = false;
    musicPlaybackDurationMs = 0;
    musicPlaybackElapsedMs = 0;
    if (mp3) {
        mp3->stop();
        delete mp3;
        mp3 = nullptr;
    }
    if (wav) {
        wav->stop();
        delete wav;
        wav = nullptr;
    }
    if (audioBuffer) {
        audioBuffer->close();
        delete audioBuffer;
        audioBuffer = nullptr;
    }
    if (fileSD) {
        fileSD->close();
        delete fileSD;
        fileSD = nullptr;
    }
    if (fileSPIFFS) {
        fileSPIFFS->close();
        delete fileSPIFFS;
        fileSPIFFS = nullptr;
    }
    if (audioOut) {
        audioOut->stop();
        delete audioOut;
        audioOut = nullptr;
    }
    resetAudioSpectrum();
}

void stopMp3() {
    stopMp3Playback();
    appState = STATE_FILE_MANAGER;
    drawFileManager();
}

void startMp3(String fileName) {
    String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + fileName;
    String lowerName = fileName;
    lowerName.toLowerCase();
    resetAudioSpectrum();
    
    audioOut = new AudioOutputM5Speaker(&M5Cardputer.Speaker, 0);
    
    bool started = false;
    if (isSDCardManager) {
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
        mp3DurationSeconds = 180;
        musicPlaybackDurationMs = 0;
        musicPlaybackElapsedMs = 0;
        appState = STATE_FILE_MANAGER;
        showFileContent = false;
    } else {
        if (mp3) { delete mp3; mp3 = nullptr; }
        if (wav) { delete wav; wav = nullptr; }
        if (audioBuffer) { delete audioBuffer; audioBuffer = nullptr; }
        if (fileSD) { delete fileSD; fileSD = nullptr; }
        if (fileSPIFFS) { delete fileSPIFFS; fileSPIFFS = nullptr; }
        if (audioOut) { delete audioOut; audioOut = nullptr; }
        
        canvas.startWrite();
        canvas.fillScreen(CP_BG);
        canvas.setTextColor(CP_RED);
        canvas.drawCenterString("FILE IO ERROR", 120, 50);
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("PRESS ANY KEY", 120, 80);
        pushCanvas();
        delay(1500);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
    }
}

void drawFileActionsMenu() {
    // First, draw the file manager background behind the pop-up menu!
    drawFileManager(false);
    
    // Draw pop-up overlay
    canvas.startWrite();
    
    // Pop-up border and box
    canvas.fillRect(35, 14, 170, 112, CP_BG);
    canvas.drawRect(35, 14, 170, 112, CP_CYAN);
    canvas.drawRect(37, 16, 166, 108, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- ACTION MENU ---", 120, 20);
    canvas.drawLine(40, 31, 200, 31, CP_CYAN);
    
    int startY = 36;
    bool hasPaste = (clipboardSourcePath != "");
    bool canNew = isSDCardManager;
    bool canEdit = !loadedFiles.empty() && fileManagerSelected >= 0 && fileManagerSelected < (int)loadedFiles.size() && isEditableTextFileName(loadedFiles[fileManagerSelected].name, loadedFiles[fileManagerSelected].isDir);
    std::vector<String> options = {"1. OPEN", "2. EDIT", "3. RENAME", "4. DELETE", "5. COPY", "6. PASTE", "7. NEW"};
    
    for (int i = 0; i < 7; i++) {
        bool isSel = (i == fileActionsMenuSelected);
        uint16_t color = isSel ? CP_YELLOW : WHITE;
        
        if (i == 1 && !canEdit) { // EDIT is only for .txt files
            color = CP_DIM;
        }
        if (i == 5 && !hasPaste) { // PASTE is disabled
            color = CP_DIM;
        }
        if (i == 6 && !canNew) { // NEW is SD-only
            color = CP_DIM;
        }
        
        if (isSel) {
            canvas.fillRect(42, startY - 2, 156, 12, canvas.color565(30, 30, 30));
            canvas.drawRect(42, startY - 2, 156, 12, CP_CYAN);
        }
        
        canvas.setTextColor(color);
        canvas.setCursor(48, startY);
        canvas.print(options[i]);
        
        if (i == 1 && !canEdit) {
            canvas.setCursor(120, startY);
            canvas.print("[TXT]");
        }
        if (i == 5 && !hasPaste) {
            canvas.setCursor(120, startY);
            canvas.print("[EMPTY]");
        }
        if (i == 6 && !canNew) {
            canvas.setCursor(120, startY);
            canvas.print("[SD]");
        }
        
        startY += 12;
    }
    
    pushCanvas();
}

void drawFileNewTypeMenu() {
    drawFileManager(false);

    canvas.startWrite();
    canvas.fillRect(35, 36, 170, 74, CP_BG);
    canvas.drawRect(35, 36, 170, 74, CP_CYAN);
    canvas.drawRect(37, 38, 166, 70, CP_DIM);

    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    canvas.drawCenterString("--- NEW ITEM ---", 120, 43);
    canvas.drawLine(40, 53, 200, 53, CP_CYAN);

    std::vector<String> options = {"1. FOLDER", "2. TXT FILE"};
    int startY = 61;
    for (int i = 0; i < 2; i++) {
        bool isSel = (i == fileNewTypeMenuSelected);
        uint16_t color = isSel ? CP_YELLOW : WHITE;
        if (isSel) {
            canvas.fillRect(42, startY - 2, 156, 12, canvas.color565(30, 30, 30));
            canvas.drawRect(42, startY - 2, 156, 12, CP_CYAN);
        }
        canvas.setTextColor(color);
        canvas.setCursor(48, startY);
        canvas.print(options[i]);
        startY += 14;
    }

    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("ENTER SELECT | ESC BACK", 120, 94);
    pushCanvas();
}

void handleFileNewTypeMenuInput(Keyboard_Class::KeysState status) {
    bool hasBack = false, hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_FILE_ACTIONS_MENU;
        drawFileActionsMenu();
        return;
    }

    if (hasUp || hasDown) {
        playSound(sound_hover, sound_hover_size);
        fileNewTypeMenuSelected = fileNewTypeMenuSelected == 0 ? 1 : 0;
        drawFileNewTypeMenu();
        return;
    }

    if (status.enter) {
        playSound(sound_select, sound_select_size);
        fileNameInputMode = fileNewTypeMenuSelected == 0 ? FILE_NAME_MODE_NEW_FOLDER : FILE_NAME_MODE_NEW_TXT;
        renameInputText = fileNameInputMode == FILE_NAME_MODE_NEW_FOLDER ? "new_folder" : "new.txt";
        appState = STATE_FILE_RENAME_INPUT;
        drawFileRenameInput();
    }
}

void handleFileActionsMenuInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }
    
    bool hasUp = false, hasDown = false;
    for (char c : status.word) {
        if (c == ';') hasUp = true;
        if (c == '.') hasDown = true;
    }
    
    if (hasUp) {
        playSound(sound_hover, sound_hover_size);
        fileActionsMenuSelected--;
        if (fileActionsMenuSelected < 0) fileActionsMenuSelected = 6;
        drawFileActionsMenu();
    }
    if (hasDown) {
        playSound(sound_hover, sound_hover_size);
        fileActionsMenuSelected++;
        if (fileActionsMenuSelected > 6) fileActionsMenuSelected = 0;
        drawFileActionsMenu();
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        bool hasPaste = (clipboardSourcePath != "");
        bool canNew = isSDCardManager;
        bool canEdit = !loadedFiles.empty() && fileManagerSelected >= 0 && fileManagerSelected < (int)loadedFiles.size() && isEditableTextFileName(loadedFiles[fileManagerSelected].name, loadedFiles[fileManagerSelected].isDir);
        
        if (fileActionsMenuSelected == 1 && !canEdit) {
            // Edit is only enabled for .txt files.
            return;
        }
        if (fileActionsMenuSelected == 5 && !hasPaste) {
            // Paste is disabled
            return;
        }
        if (fileActionsMenuSelected == 6 && !canNew) {
            // New item creation is only supported in SD CARD.
            return;
        }
        if (fileActionsMenuSelected == 6) { // NEW
            fileNewTypeMenuSelected = 0;
            appState = STATE_FILE_NEW_TYPE_MENU;
            drawFileNewTypeMenu();
            return;
        }
        if (loadedFiles.empty()) {
            appState = STATE_FILE_MANAGER;
            drawFileManager();
            return;
        }
        
        RealFile targetFile = loadedFiles[fileManagerSelected];
        String fullPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + targetFile.name;
        
        if (fileActionsMenuSelected == 0) { // OPEN
            if (targetFile.name == "..") {
                // Navigate up
                int lastSlash = fileManagerCurrentPath.lastIndexOf('/');
                if (lastSlash == 0) {
                    fileManagerCurrentPath = "/";
                } else if (lastSlash > 0) {
                    fileManagerCurrentPath = fileManagerCurrentPath.substring(0, lastSlash);
                }
                fileManagerSelected = 0;
                fileManagerScrollOffset = 0;
                populateFileList();
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else if (targetFile.isDir) {
                // Navigate down
                if (fileManagerCurrentPath == "/") {
                    fileManagerCurrentPath = "/" + targetFile.name;
                } else {
                    fileManagerCurrentPath = fileManagerCurrentPath + "/" + targetFile.name;
                }
                fileManagerSelected = 0;
                fileManagerScrollOffset = 0;
                populateFileList();
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                String lowerName = targetFile.name;
                lowerName.toLowerCase();
                if (lowerName.endsWith(".mp3") || lowerName.endsWith(".wav")) {
                    startMp3(targetFile.name);
                } else if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) {
                    playSound(sound_select, sound_select_size);
                    openedImageName = targetFile.name;
                    imageScale = 1.0f;
                    showImage = true;
                    appState = STATE_FILE_MANAGER;
                    drawFileManager();
                } else {
                    // Open file content
                    readSelectedFileContent(targetFile.name);
                    showFileContent = true;
                    appState = STATE_FILE_MANAGER;
                    drawFileManager();
                }
            }
        }
        else if (fileActionsMenuSelected == 1) { // EDIT
            if (isEditableTextFileName(targetFile.name, targetFile.isDir)) {
                openTextEditor(targetFile.name);
            } else {
                appState = STATE_FILE_ACTIONS_MENU;
                drawFileActionsMenu();
            }
        }
        else if (fileActionsMenuSelected == 2) { // RENAME
            if (targetFile.name != "..") {
                fileNameInputMode = FILE_NAME_MODE_RENAME;
                renameInputText = targetFile.name;
                appState = STATE_FILE_RENAME_INPUT;
                drawFileRenameInput();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 3) { // DELETE
            if (targetFile.name != "..") {
                if (isSDCardManager) {
                    if (isSDFallback) {
                        // Mock delete in demo mode
                        loadedFiles.erase(loadedFiles.begin() + fileManagerSelected);
                    } else {
                        // Physical delete
                        deleteRecursive(fullPath);
                        populateFileList();
                    }
                } else {
                    if (isFlashFallback) {
                        loadedFiles.erase(loadedFiles.begin() + fileManagerSelected);
                    } else {
                        deleteRecursive(fullPath);
                        populateFileList();
                    }
                }
                if (fileManagerSelected >= (int)loadedFiles.size()) {
                    fileManagerSelected = (int)loadedFiles.size() - 1;
                }
                if (fileManagerSelected < 0) {
                    fileManagerSelected = 0;
                }
                
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected < fileManagerScrollOffset) {
                        fileManagerScrollOffset = fileManagerSelected;
                    } else if (fileManagerSelected > fileManagerScrollOffset + 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    }
                    if (fileManagerScrollOffset + 5 > (int)loadedFiles.size()) {
                        fileManagerScrollOffset = (int)loadedFiles.size() - 5;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 4) { // COPY
            if (targetFile.name != "..") {
                clipboardSourcePath = fullPath;
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            } else {
                appState = STATE_FILE_MANAGER;
                drawFileManager();
            }
        }
        else if (fileActionsMenuSelected == 5) { // PASTE
            String sourceFilename = clipboardSourcePath;
            int lastSlash = sourceFilename.lastIndexOf('/');
            if (lastSlash >= 0) sourceFilename = sourceFilename.substring(lastSlash + 1);
            
            String destPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + sourceFilename;
            
            if (isSDCardManager) {
                if (isSDFallback) {
                    RealFile pf = {sourceFilename, "1.0 KB", false};
                    loadedFiles.push_back(pf);
                } else {
                    // Physical paste
                    File src = SD.open(clipboardSourcePath, FILE_READ);
                    File dst = SD.open(destPath, FILE_WRITE);
                    if (src && dst) {
                        uint8_t buf[256];
                        while (src.available()) {
                            int len = src.read(buf, sizeof(buf));
                            dst.write(buf, len);
                        }
                    }
                    if (src) src.close();
                    if (dst) dst.close();
                    populateFileList();
                }
            } else {
                if (isFlashFallback) {
                    RealFile pf = {sourceFilename, "1.0 KB", false};
                    loadedFiles.push_back(pf);
                } else {
                    // Physical paste
                    File src = SPIFFS.open(clipboardSourcePath, FILE_READ);
                    File dst = SPIFFS.open(destPath, FILE_WRITE);
                    if (src && dst) {
                        uint8_t buf[256];
                        while (src.available()) {
                            int len = src.read(buf, sizeof(buf));
                            dst.write(buf, len);
                        }
                    }
                    if (src) src.close();
                    if (dst) dst.close();
                    populateFileList();
                }
            }
            int foundIdx = -1;
            for (size_t i = 0; i < loadedFiles.size(); i++) {
                if (loadedFiles[i].name == sourceFilename) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx >= 0) {
                fileManagerSelected = foundIdx;
                if ((int)loadedFiles.size() > 5) {
                    if (fileManagerSelected > 4) {
                        fileManagerScrollOffset = fileManagerSelected - 4;
                    } else {
                        fileManagerScrollOffset = 0;
                    }
                } else {
                    fileManagerScrollOffset = 0;
                }
            }
            appState = STATE_FILE_MANAGER;
            drawFileManager();
        }
    }
}

void drawFileRenameInput() {
    // First, draw the file manager background behind the rename dialog!
    drawFileManager(false);
    
    // Draw dialog overlay
    canvas.startWrite();
    canvas.fillRect(25, 35, 190, 65, CP_BG);
    canvas.drawRect(25, 35, 190, 65, CP_CYAN);
    canvas.drawRect(27, 37, 186, 61, CP_DIM);
    
    canvas.setTextColor(CP_YELLOW);
    canvas.setTextSize(1);
    String title = "--- RENAME FILE ---";
    if (fileNameInputMode == FILE_NAME_MODE_NEW_FOLDER) title = "--- NEW FOLDER ---";
    else if (fileNameInputMode == FILE_NAME_MODE_NEW_TXT) title = "--- NEW TXT FILE ---";
    canvas.drawCenterString(title, 120, 42);
    
    // Input box
    canvas.drawRect(35, 56, 170, 16, CP_CYAN);
    canvas.fillRect(36, 57, 168, 14, canvas.color565(30, 30, 30));
    
    canvas.setTextColor(WHITE);
    canvas.setCursor(40, 60);
    // Draw typed name with a blink cursor
    String disp = renameInputText;
    if (disp.length() > 20) disp = disp.substring(disp.length() - 20);
    if (blinkState) disp += "_";
    canvas.print(disp);
    
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString(fileNameInputMode == FILE_NAME_MODE_RENAME ? "ENTER: RENAME  |  ESC/COMMA: BACK" : "ENTER: CREATE  |  ESC/COMMA: BACK", 120, 80);
    
    pushCanvas();
}

void handleFileRenameInput(Keyboard_Class::KeysState status) {
    bool hasBack = false;
    for (char c : status.word) {
        if (c == ',' || c == '`') hasBack = true;
    }
    
    if (hasBack) {
        playSound(sound_select, sound_select_size);
        if (fileNameInputMode == FILE_NAME_MODE_RENAME) {
            appState = STATE_FILE_ACTIONS_MENU;
            drawFileActionsMenu();
        } else {
            appState = STATE_FILE_NEW_TYPE_MENU;
            drawFileNewTypeMenu();
        }
        return;
    }
    
    if (status.enter) {
        playSound(sound_select, sound_select_size);
        if (renameInputText.length() > 0) {
            if (fileNameInputMode == FILE_NAME_MODE_RENAME) {
                if (!loadedFiles.empty()) {
                    RealFile targetFile = loadedFiles[fileManagerSelected];
                    String oldPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + targetFile.name;
                    String newPath = fileManagerCurrentPath + (fileManagerCurrentPath == "/" ? "" : "/") + renameInputText;

                    if (isSDCardManager) {
                        if (isSDFallback) {
                            loadedFiles[fileManagerSelected].name = renameInputText;
                        } else {
                            SD.rename(oldPath, newPath);
                            populateFileList();
                        }
                    } else {
                        if (isFlashFallback) {
                            loadedFiles[fileManagerSelected].name = renameInputText;
                        } else {
                            SPIFFS.rename(oldPath, newPath);
                            populateFileList();
                        }
                    }
                    selectFileManagerEntry(renameInputText);
                }
            } else {
                bool folder = fileNameInputMode == FILE_NAME_MODE_NEW_FOLDER;
                String newName = cleanNewFileManagerName(renameInputText, !folder);
                if (newName.length() > 0) {
                    bool created = createFileManagerEntry(newName, folder);
                    if (created) {
                        if (!isSDFallback) populateFileList();
                        selectFileManagerEntry(newName);
                    } else {
                        drawMessage("CREATE FAILED");
                        delay(900);
                    }
                }
            }
        }
        fileNameInputMode = FILE_NAME_MODE_RENAME;
        appState = STATE_FILE_MANAGER;
        drawFileManager();
        return;
    }
    
    if (status.del && renameInputText.length() > 0) {
        renameInputText.remove(renameInputText.length() - 1);
        drawFileRenameInput();
    }
    
    bool typed = false;
    for (char c : status.word) {
        if (c >= 32 && c <= 126 && renameInputText.length() < 24) {
            // Filter out command/path separators from being typed
            if (c != '`' && c != ',' && (fileNameInputMode == FILE_NAME_MODE_RENAME || (c != '/' && c != '\\'))) {
                renameInputText += c;
                typed = true;
            }
        }
    }
    
    if (typed) {
        drawFileRenameInput();
    }
}

void drawFileLoading() {
    String statusText = isSDCardManager ? "READING SD CARD SCHEMA..." : "READING FLASH SCHEMA...";
    drawProgressBar(loadingProgress, statusText, CP_CYAN);
    
    loadingProgress += 4;
    if (loadingProgress >= 100) {
        loadingProgress = 100;
        populateFileList();
        fileManagerSelected = 0;
        fileManagerScrollOffset = 0;
        appState = STATE_FILE_MANAGER;
        drawFileManager();
    } else {
        delay(30);
    }
}
