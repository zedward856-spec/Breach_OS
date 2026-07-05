// HARDWARE NODE / USB DRIVE: expose the SD card as a USB mass-storage disk.

static constexpr int USB_DRIVE_SD_CS = 12;
static constexpr uint32_t USB_DRIVE_SD_SPI_HZ = 20000000;
static constexpr uint32_t USB_DRIVE_MAX_SECTOR_SIZE = 512;

#if BREACH_USB_MSC_AVAILABLE
static bool usbDriveTransfer(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize, bool writeMode) {
    if (!usbDriveSdMounted || usbDriveSectorSize == 0 || usbDriveSectorSize > USB_DRIVE_MAX_SECTOR_SIZE) {
        usbDriveErrorOps++;
        return false;
    }
    if (offset >= usbDriveSectorSize) {
        usbDriveErrorOps++;
        return false;
    }

    uint8_t scratch[USB_DRIVE_MAX_SECTOR_SIZE];
    uint32_t remaining = bufsize;
    uint32_t sector = lba;
    uint32_t sectorOffset = offset;
    uint8_t *cursor = buffer;

    while (remaining > 0) {
        if (sector >= usbDriveSectorCount) {
            usbDriveErrorOps++;
            return false;
        }

        uint32_t chunk = usbDriveSectorSize - sectorOffset;
        if (chunk > remaining) chunk = remaining;

        if (!writeMode) {
            if (sectorOffset == 0 && chunk == usbDriveSectorSize) {
                if (!SD.readRAW(cursor, sector)) {
                    usbDriveErrorOps++;
                    return false;
                }
            } else {
                if (!SD.readRAW(scratch, sector)) {
                    usbDriveErrorOps++;
                    return false;
                }
                memcpy(cursor, scratch + sectorOffset, chunk);
            }
        } else {
            if (sectorOffset == 0 && chunk == usbDriveSectorSize) {
                if (!SD.writeRAW(cursor, sector)) {
                    usbDriveErrorOps++;
                    return false;
                }
            } else {
                if (!SD.readRAW(scratch, sector)) {
                    usbDriveErrorOps++;
                    return false;
                }
                memcpy(scratch + sectorOffset, cursor, chunk);
                if (!SD.writeRAW(scratch, sector)) {
                    usbDriveErrorOps++;
                    return false;
                }
            }
        }

        remaining -= chunk;
        cursor += chunk;
        sector++;
        sectorOffset = 0;
    }
    return true;
}

static int32_t usbDriveReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    if (!usbDriveTransfer(lba, offset, static_cast<uint8_t *>(buffer), bufsize, false)) {
        return -1;
    }
    usbDriveReadOps++;
    return bufsize;
}

static int32_t usbDriveWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    if (!usbDriveTransfer(lba, offset, buffer, bufsize, true)) {
        return -1;
    }
    usbDriveWriteOps++;
    return bufsize;
}

static bool usbDriveStartStopCallback(uint8_t powerCondition, bool start, bool loadEject) {
    (void)powerCondition;
    if (!start && loadEject) {
        usbDriveHostEjected = true;
        usbDriveActive = false;
    } else if (start) {
        usbDriveHostEjected = false;
    }
    return true;
}
#endif

static bool mountUsbDriveSd() {
    SPI.begin(40, 39, 14, USB_DRIVE_SD_CS);
    if (!SD.begin(USB_DRIVE_SD_CS, SPI, USB_DRIVE_SD_SPI_HZ) || SD.cardType() == CARD_NONE) {
        usbDriveSdMounted = false;
        usbDriveSectorCount = 0;
        usbDriveSectorSize = 512;
        usbDriveStatus = "SD MOUNT FAIL";
        return false;
    }

    usbDriveSectorCount = SD.numSectors();
    usbDriveSectorSize = SD.sectorSize();
    if (usbDriveSectorCount == 0 || usbDriveSectorSize == 0 || usbDriveSectorSize > USB_DRIVE_MAX_SECTOR_SIZE) {
        usbDriveSdMounted = false;
        usbDriveStatus = "SD RAW FAIL";
        SD.end();
        return false;
    }

    usbDriveSdMounted = true;
    return true;
}

static bool startUsbDriveMsc() {
#if BREACH_USB_MSC_AVAILABLE
    if (!mountUsbDriveSd()) return false;

    usbDriveReadOps = 0;
    usbDriveWriteOps = 0;
    usbDriveErrorOps = 0;
    usbDriveHostEjected = false;

    if (usbDriveMsc == nullptr) {
        usbDriveMsc = new USBMSC();
        if (usbDriveMsc == nullptr) {
            usbDriveConfigured = false;
            usbDriveActive = false;
            usbDriveStatus = "MSC ALLOC FAIL";
            SD.end();
            usbDriveSdMounted = false;
            return false;
        }
    }

    if (usbDriveConfigured) {
        usbDriveMsc->end();
    }

    usbDriveMsc->vendorID("Breach");
    usbDriveMsc->productID("SD USB Drive");
    usbDriveMsc->productRevision("1.2");
    usbDriveMsc->onRead(usbDriveReadCallback);
    usbDriveMsc->onWrite(usbDriveWriteCallback);
    usbDriveMsc->onStartStop(usbDriveStartStopCallback);
    usbDriveMsc->isWritable(true);
    usbDriveMsc->mediaPresent(true);
    if (!usbDriveMsc->begin(usbDriveSectorCount, usbDriveSectorSize)) {
        usbDriveConfigured = false;
        usbDriveActive = false;
        usbDriveStatus = "MSC START FAIL";
        SD.end();
        usbDriveSdMounted = false;
        return false;
    }

    usbDriveConfigured = true;
    usbDriveActive = true;
    usbDriveStatus = "SD EXPOSED AS USB";
    USB.begin();
    return true;
#else
    usbDriveConfigured = false;
    usbDriveActive = false;
    usbDriveSdMounted = false;
    usbDriveStatus = "BUILD NEEDS TINYUSB";
    return false;
#endif
}

static void stopUsbDriveMsc() {
#if BREACH_USB_MSC_AVAILABLE
    if (usbDriveMsc != nullptr && usbDriveConfigured) {
        usbDriveMsc->mediaPresent(false);
        usbDriveMsc->end();
    }
#endif
    usbDriveConfigured = false;
    usbDriveActive = false;
    usbDriveStatus = usbDriveHostEjected ? "HOST EJECTED" : "USB DRIVE EJECTED";
    if (usbDriveSdMounted) {
        SD.end();
        usbDriveSdMounted = false;
    }
}

void resetUsbDriveStateForBoot() {
#if BREACH_USB_MSC_AVAILABLE
    if (usbDriveMsc != nullptr && usbDriveConfigured) {
        usbDriveMsc->mediaPresent(false);
        usbDriveMsc->end();
    }
#endif
    if (usbDriveSdMounted) {
        SD.end();
    }
    usbDriveConfigured = false;
    usbDriveActive = false;
    usbDriveSdMounted = false;
    usbDriveSectorCount = 0;
    usbDriveSectorSize = 512;
    usbDriveReadOps = 0;
    usbDriveWriteOps = 0;
    usbDriveErrorOps = 0;
    usbDriveHostEjected = false;
    usbDriveStatus = "STANDBY";
}

static String usbDriveSizeText() {
    if (usbDriveSectorCount == 0 || usbDriveSectorSize == 0) return "NO MEDIA";
    uint64_t bytes = (uint64_t)usbDriveSectorCount * (uint64_t)usbDriveSectorSize;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return String((double)bytes / (1024.0 * 1024.0 * 1024.0), 1) + "GB";
    }
    return String((uint32_t)(bytes / (1024ULL * 1024ULL))) + "MB";
}

void enterUsbDriveMode() {
    stopMp3Playback();
    resetUsbDriveStateForBoot();
    appState = STATE_USB_DRIVE;
    drawUsbDriveScreen();
}

void drawUsbDriveScreen() {
    canvas.startWrite();
    canvas.fillScreen(CP_BG);
    drawChippedButton(6, 5, 228, 124, CP_CYAN);
    drawChippedButton(8, 7, 224, 120, CP_DIM);

    canvas.setTextSize(1);
    canvas.setTextColor(CP_YELLOW);
    canvas.drawCenterString("--- USB DRIVE NODE ---", 120, 12);
    canvas.drawLine(14, 26, 226, 26, CP_CYAN);

    canvas.setTextColor(usbDriveActive ? CP_GREEN : CP_RED);
    canvas.drawCenterString(usbDriveStatus, 120, 36);

#if BREACH_USB_MSC_AVAILABLE
    if (usbDriveActive && usbDriveSdMounted) {
        canvas.setTextColor(CP_CYAN);
        canvas.drawCenterString("SD CARD IS COMPUTER DRIVE", 120, 50);
        canvas.setTextColor(WHITE);
        canvas.drawCenterString("SIZE " + usbDriveSizeText(), 120, 62);
        canvas.setTextColor(CP_YELLOW);
        canvas.drawCenterString("EJECT ON COMPUTER FIRST", 120, 78);
        canvas.setTextColor(CP_DIM);
        canvas.drawCenterString("R" + String((uint32_t)usbDriveReadOps) + " W" + String((uint32_t)usbDriveWriteOps) + " ERR" + String((uint32_t)usbDriveErrorOps), 120, 94);
        canvas.drawCenterString("DEL/ESC BACK AFTER EJECT", 120, 112);
    } else {
        canvas.setTextColor(CP_DIM);
        if (usbDriveStatus == "STANDBY") {
            canvas.drawCenterString("NO USB DRIVE ACTIVE", 120, 58);
            canvas.drawCenterString("ENTER EXPORTS SD", 120, 76);
        } else {
            canvas.drawCenterString("CHECK SD OR HOST", 120, 58);
            canvas.drawCenterString("ENTER RETRY", 120, 76);
        }
        canvas.drawCenterString("DEL/ESC BACK", 120, 112);
    }
#else
    canvas.setTextColor(CP_DIM);
    canvas.drawCenterString("COMPILE USBMode=default", 120, 58);
    canvas.drawCenterString("THEN REFLASH", 120, 74);
    canvas.drawCenterString("DEL/ESC BACK", 120, 112);
#endif

    pushCanvas();
}

void handleUsbDriveInput(Keyboard_Class::KeysState status) {
    bool hasBack = status.del;
    for (char c : status.word) {
        if (c == '`' || c == ',') hasBack = true;
    }

    if (hasBack) {
        playSound(sound_select, sound_select_size);
        stopUsbDriveMsc();
        appState = STATE_HARDWARE_MENU;
        hardwareMenuFocus = 2;
        currentHardwareScroll = 2;
        targetHardwareScroll = 2;
        drawHardwareMenu();
        return;
    }

    if (status.enter && !usbDriveActive) {
        playSound(sound_select, sound_select_size);
        startUsbDriveMsc();
        drawUsbDriveScreen();
    } else if (status.enter) {
        drawMessage("EJECT ON COMPUTER", "DEL/ESC WHEN SAFE");
        delay(900);
        drawUsbDriveScreen();
    }
}
