# Release notes

## v1.0.0 (2026-11-15)

This is the first public release of **MagicPilot Remote**, an open-source touchscreen remote for the Blackmagic Pocket Cinema Camera 6K. It runs on an M5Stack Core2, CoreS3 or CoreS3-SE and talks to the camera directly over Bluetooth LE. You don't need a phone, a Wi-Fi network or the Blackmagic Camera app.

### ⚡ Install

**In your browser (easiest):** open the **[web installer](https://kenfilms.github.io/MagicPilot_Remote/install/)** in Chrome, Edge or Opera on a desktop computer. Connect the M5Stack by USB and click **Install**. The installer detects the board and writes the matching firmware.

**Manually:** download the image for your board and write it at offset `0x0`:

| Board | Chip | Image |
|---|---|---|
| M5Stack Core2 | ESP32 | [`MagicPilot_Remote_m5stack-core2.bin`](https://kenfilms.github.io/MagicPilot_Remote/firmware/MagicPilot_Remote_m5stack-core2.bin) |
| M5Stack CoreS3 / CoreS3-SE | ESP32-S3 | [`MagicPilot_Remote_m5stack-cores3.bin`](https://kenfilms.github.io/MagicPilot_Remote/firmware/MagicPilot_Remote_m5stack-cores3.bin) |

```sh
esptool.py --chip esp32   write_flash 0x0 MagicPilot_Remote_m5stack-core2.bin
esptool.py --chip esp32s3 write_flash 0x0 MagicPilot_Remote_m5stack-cores3.bin
```

**From source:** see [Build and flash](https://github.com/KenFilms/MagicPilot_Remote#build-and-flash) for the Arduino IDE and PlatformIO.

### ✨ Features

- **Bluetooth LE connection** to the camera with the official Blackmagic Camera Control protocol. You pair by entering the camera's 6-digit PIN on an on-screen keypad.
- **Live camera readout:** ISO, shutter angle, FPS, white balance, tint, iris, timecode or clip counter, record state, camera battery voltage, remote battery and storage card status.
- **Parameter control:** tap a value, then use − / + to change it on the camera.
  - ISO 100 – 25,600
  - Shutter 11.2 – 360°
  - 5 – 120 fps
  - White balance 2,500 – 10,000 K in 50 K steps
  - Tint −50 to +50
  - Iris one lens stop per tap
- **Record control:** start and stop recording with one button.
- **Focus page:** NEAR → FAR slider, − / + in 1% steps (repeats while held) and one-shot autofocus.
- **Two camera presets** on the microSD card. Each one stores ISO, shutter, FPS, white balance, tint and iris, and you can save, load or clear it. Presets survive power-off.
- **Automatic display brightness** on CoreS3 and CoreS3-SE, driven by the ambient light sensor.

### 🧰 Supported hardware

| Device | Status |
|---|---|
| M5Stack Core2 | 🟢 Supported |
| M5Stack CoreS3 | 🟢 Supported |
| M5Stack CoreS3-SE | 🟢 Supported (CoreS3 image) |

| Camera | Status |
|---|---|
| Blackmagic Pocket Cinema Camera 6K | 🟢 Tested |
| Pocket Cinema Camera 6K G2, 6K Pro, 4K | 🟡 Not tested |

### 🔧 Build and tooling

- The images are built with PlatformIO `espressif32@6.12.0` (arduino-esp32 2.0.17, ESP-IDF 4.4.7), M5Unified 0.2.26 and M5GFX 0.2.33.
- The platform is now pinned in `platformio.ini`. The firmware uses the Bluedroid BLE stack, and arduino-esp32 3.3.x (tested with 3.3.12) ships NimBLE instead for the ESP32-S3, so the CoreS3 build fails with that core.
- The new `merged` target (`pio run -e <env> -t merged`) writes a single flash image per board to `docs/firmware`, which the web installer uses.
- `scripts/upload_auto.py` detects a connected Core2 or CoreS3 and uploads the matching build.

### ⚠️ Known limitations

- Iris and focus control only reach lenses the camera can drive electronically.
- Presets need a microSD card. Without one, the remote still works, but saving and loading presets shows "No SD card".
- The Core2 has no ambient light sensor, so it keeps a fixed display brightness.

### 🔒 SHA-256 checksums

```
588b34da6efb6a5cd72268b246bec0131034a1bf16a7dd10f6dcf7111d8a096c  MagicPilot_Remote_m5stack-core2.bin
2a5a601500c80f0b4d8022e21c7381b2eaab0079fc4191d31dacf2a167f840c5  MagicPilot_Remote_m5stack-cores3.bin
```
