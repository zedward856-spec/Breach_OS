# Breach_OS

![Splash](Images/IMG_20260704_191059.jpg)
The splash Menu serves no purpose other than looking cool and showing the version number
![Boot Menu](Images/IMG_20260704_191122.jpg)
This is the boot menu it divides the functions into different categories
![SSH](Images/IMG_20260704_191209.jpg)
SSH
![Voice Recorder](Images/IMG_20260704_191232.jpg)
Voice Recorder

Breach_OS is a cyberpunk M5Stack Cardputer firmware centered on a Breach Protocol-style hacking minigame, plus a growing set of Cardputer utility nodes: Wi-Fi/account play, global scores, SSH/Telnet terminals, SD/flash file tools, USB mass storage, a DuckyScript HID executor, music playback, WAV recording, OTA catalog browsing, and device settings.

The Arduino sketch lives in `Breach_OS/` and keeps Arduino IDE compatibility by matching the folder and primary sketch name: `Breach_OS/Breach_OS.ino`.

## Current firmware map

### Breach_OS / SELECT BOOT NODE

Visible boot choices:

- `BREACH`
- `HARDWARE NODE`
- `NETWORK NODE`
- `SYSTEM SETTINGS`

The boot UI uses the shared neon wheel/arc selector with a smooth position indicator.

### BREACH MODE

Rows:

- `ONLINE` - connect through Wi-Fi/auth, then launch the grid-select/game flow.
- `OFFLINE` - start local guest play without auth.
- `ACCOUNT` - open the online operative profile from BREACH MODE.
- `LEADERBOARD` - fetch and display GLOBAL SCORES.
- `BACK` - return to SELECT BOOT NODE.

Gameplay features:

- Active-line grid movement: horizontal/vertical movement alternates after each selected code.
- Randomized 3x3, 4x4, and 5x5 grids.
- Dynamic target sequence lengths.
- Solvable sequence generation.
- Phase progression and scoring.
- Local high score storage.
- Online score submission through the bundled API URL.
- GLOBAL SCORES screen with neon-yellow score dates.

### NETWORK NODE

Rows:

- `SSH`
- `TELNET BBS`
- `OTA CATALOG`
- `BACK`

Network functions:

- `SSH` opens a Cardputer SSH profile/terminal screen.
  - Target format supports entries like `ssh user@host`.
  - Port defaults to `22`.
  - `REMEMBER` saves target/user/host/port, but not passwords.
- `TELNET BBS` opens a raw Telnet BBS client.
  - Supports host/port setup, Telnet negotiation, ANSI cursor basics, CP437 fallback characters, and panning/scrolling.
- `OTA CATALOG` browses Cardputer firmware entries from LauncherHub-style catalog endpoints.
  - Supports catalog sorting, paged browsing, detail/version view, streamed description loading, and SD download storage.
  - Downloads save under `/Breach_OS/firmware` on SD as sanitized firmware/version `.bin` files.
  - It does not flash downloaded firmware in-app.
- `BACK` returns to SELECT BOOT NODE focused on NETWORK NODE.

### HARDWARE NODE

Rows:

- `FLASH MEMORY`
- `SD CARD`
- `USB DRIVE`
- `BADUSB`
- `MUSIC PLAYER`
- `SOUND REC`
- `BATTERY STATUS`
- `BACK`

Hardware functions:

- `FLASH MEMORY` - browse SPIFFS files.
- `SD CARD` - browse the SD card.
- `USB DRIVE` - expose the SD card to the connected computer as a writable USB mass-storage disk.
  - Uses ESP32-S3 TinyUSB MSC and raw SD sectors.
  - Eject the drive on the host computer before leaving this screen.
- `BADUSB` - execute DuckyScript files from SD `/Breach_OS/Ducky` as a TinyUSB HID keyboard.
  - Creates `/Breach_OS/Ducky` if missing.
  - Lists `.txt`, `.duck`, `.ducky`, and `.ds` scripts.
  - Requires script confirmation and shows an `AUTHORIZED HOSTS ONLY` warning.
  - Supports `REM`, `DELAY`, `DEFAULT_DELAY`, `DEFAULTDELAY`, `STRING`, `STRINGLN`, `TEXT`, `TEXTLN`, `REPEAT`, common special keys, and modifier combos like `GUI R`, `CTRL ALT DELETE`, and `ALT F4`.
  - Use only on computers you own or are explicitly authorized to test. DuckyScript files are plaintext; do not store real passwords on the SD card unless you accept that risk.
- `MUSIC PLAYER` - SD music player with playlist focus, MP3/WAV playback, loop mode, progress/time display, and decoded-sample spectrum visualization.
- `SOUND REC` - record microphone audio to WAV on SD.
  - Saves under `/Breach_OS/recordings/`.
  - Shows recording time and live spectrum.
- `BATTERY STATUS` - shows voltage and calculated capacity.
- `BACK` - returns to SELECT BOOT NODE focused on HARDWARE NODE.

### File manager actions

The flash/SD file action menu has:

1. `OPEN`
2. `EDIT`
3. `RENAME`
4. `DELETE`
5. `COPY`
6. `PASTE`
7. `NEW`

Details:

- `EDIT` is enabled only for non-directory `.txt` files.
- The `TXT EDITOR` loads files up to 4 KB, strips CR characters, inserts printable typed characters, uses ENTER for newline, DEL for backspace, and ESC/backtick to save and return.
- `NEW` is SD-only and opens:
  - `FOLDER`
  - `TXT FILE`
- TXT creation auto-appends `.txt` when needed.
- New names strip `/` and `\` before creation.

### SYSTEM SETTINGS

Tabs:

- `HARDWARE`
  - Sort by name/type.
  - Ascending/descending order.
  - Show/hide system files.
- `NETWORK`
  - Saved Wi-Fi SSID.
  - Saved Wi-Fi password display is masked.
- `OFFLINE`
  - Music loop mode.
  - Music directory picker.
- `OTA`
  - OTA catalog sort mode.
- `APPEAR`
  - Glitch text mode.
  - Brightness.
  - Volume.
  - Charging mode.
- `CREDITS`
  - `CREDITS: < OPEN >`
  - `GITHUB QR: < OPEN >`

The GitHub QR screen displays a fixed QR code for:

```text
https://github.com/zedward856-spec/Breach_OS
```

## SD card layout

Breach_OS creates or expects these SD paths for current functions:

```text
/Breach_OS/
  Ducky/          DuckyScript files for BADUSB
  firmware/       OTA catalog firmware downloads
  recordings/     SOUND REC WAV files
  wifi.txt        generated Wi-Fi profile used for reconnect/migration
```

`/Breach_OS/wifi.txt` is plaintext (`ssid=` and `password=`), so keep the SD card private.

Music can be browsed from any selected SD directory via SYSTEM SETTINGS -> OFFLINE -> MUSIC DIR.

## Controls

Common controls:

- Navigate up/down: `;` / `.`
- Left/right: `,` / `/` where a screen supports horizontal movement
- Select/confirm: ENTER or `/`
- Back/cancel: `,`, ESC, or backtick `` ` `` depending on screen
- Delete/backspace: DEL/BACKSPACE
- Volume: `-` / `+`
- Brightness: `[` / `]`

Game controls:

- Grid movement follows the active line: row movement, then column movement, alternating after each selected code.
- ENTER selects the current code.
- ESC/back returns out of menu screens where supported.

## Build and flash

### Arduino CLI board

Use the ESP32 Arduino core board ID:

```bash
esp32:esp32:m5stack_cardputer
```

Because the current firmware uses TinyUSB MSC/HID for `USB DRIVE` and `BADUSB`, compile with USB OTG/TinyUSB enabled:

```bash
.tools/arduino-cli/arduino-cli.exe compile \
  --fqbn 'esp32:esp32:m5stack_cardputer:USBMode=default,CDCOnBoot=cdc,UploadMode=cdc,PartitionScheme=huge_app' \
  Breach_OS
```

Upload example:

```bash
.tools/arduino-cli/arduino-cli.exe upload \
  -p COM6 \
  --fqbn 'esp32:esp32:m5stack_cardputer:USBMode=default,CDCOnBoot=cdc,UploadMode=cdc,PartitionScheme=huge_app' \
  Breach_OS
```

The serial port can change after flashing TinyUSB builds. If upload cannot sync, put the Cardputer into ESP32-S3 download mode and retry.

### Prebuilt binary

The repository root contains:

```text
Breach_OS.ino.merged.bin
```

Flash that merged image at address `0x0` with esptool or a compatible ESP32 flasher.

## Repository layout

```text
Breach_OS/
  Breach_OS.ino                 shared includes, globals, AppState, prototypes
  01_core_ui.ino                core UI, SSH terminal, credits, GitHub QR
  02_hardware_file_manager.ino  hardware menu, settings, file manager, recorder
  03_splash_network_menus.ino   Breach_OS boot, BREACH MODE, auth, network node
  04_gameplay.ino               Breach Protocol gameplay and leaderboard drawing
  05_setup.ino                  setup, preferences, boot-time init
  06_account.ino                account/profile screen and backend sync
  07_loop.ino                   main app loop and state dispatch
  08_ota.ino                    OTA catalog/detail/download flow
  09_music_player.ino           music player and audio playback helpers
  10_telnet_bbs.ino             Telnet BBS terminal client
  11_usb_drive.ino              SD-backed USB mass-storage mode
  12_badusb.ino                 DuckyScript HID executor
  sounds.h                      embedded UI sounds

website/                        Next.js API/frontend companion app
Images/                         README screenshots
Breach_OS.ino.merged.bin         merged ESP32 flash image
MENU_SYSTEM_BRANCH.txt           menu-map documentation artifact
```

## Website/backend companion

The `website/` folder is a Next.js app that includes API routes used by the firmware:

- `/api/auth`
- `/api/account`
- `/api/leaderboard`
- `/api/admin`

The firmware currently points `API_URL` at the deployed Vercel API in `Breach_OS/Breach_OS.ino`.

## Safety notes

- `BADUSB` sends keystrokes as a USB keyboard. Use it only on authorized hosts.
- Do not store real passwords in DuckyScript unless you are comfortable with plaintext credentials on the SD card.
- `USB DRIVE` exposes the SD card as writable storage. Eject from the host computer before leaving USB DRIVE mode to avoid filesystem corruption.
- SSH/Telnet/OTA features require Wi-Fi and valid network targets.

## Credits

- Firmware development/port: `sl01220`
- Original Cyberpunk 2077 Breach Protocol visual/gameplay inspiration: CD PROJEKT RED
