# ✨ MagicPilot Remote

### 🎬 A tiny touchscreen remote for the BMPCC6K.

![BlackmagicCameraControl](https://img.shields.io/badge/BlackmagicCameraControl-Aug_2025-green) 
![arduino-esp32](https://img.shields.io/badge/arduino--esp32-2.0.17-green)
![Platform](https://img.shields.io/badge/platform-M5Stack%20Core2-blue)
![MCU](https://img.shields.io/badge/MCU-ESP32-E7352C)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
![Camera](https://img.shields.io/badge/camera-BMPCC6K-black)

https://github.com/user-attachments/assets/88d4dcbc-c6db-4bb7-9184-9daaf6ef9eb5

[Watch the demo video](docs/video/MagicPilot_Remote.mp4)

Touchscreen remote for Blackmagic cameras (tested against the Pocket Cinema Camera 6K), running on an M5Stack Core2. It talks to the camera over Bluetooth LE using the Blackmagic Camera Control protocol.

## Table of contents

- [Overview](#overview)
  - [Why MagicPilot?](#why-magicpilot)
  - [Everything you need at a glance](#-everything-you-need-at-a-glance)
- [Getting started](#getting-started)
  - [Hardware](#-hardware)
  - [Build and flash](#build-and-flash)
    - [Arduino IDE](#arduino-ide)
    - [PaltformIO](#paltformio)
  - [Camera compatibility](#-camera-compatibility)
- [Using the remote](#using-the-remote)
  - [Pairing](#pairing)
  - [Live readout and control](#live-readout-and-control)
  - [Focus](#focus)
  - [Presets](#presets)
  - [SD card](#sd-card)
- [Project layout](#project-layout)
- [Disclaimer](#disclaimer)

## Overview

### Why MagicPilot?

**MagicPilot puts the controls you need on a tiny touchscreen that you can keep beside the camera.**

- **Next to your external monitor.** An external monitor is brighter than the camera's built-in screen, so that is where you look. MagicPilot mounts right beside it, so the settings are in your line of sight.
- **When the camera is out of reach.** With the camera against a wall or mounted very high, such as on a crane, the remote changes settings and starts or stops recording from wherever you stand.

### ✨ Everything You need at a Glance

| | Feature | What it does |
|---|---|---|
| 🔭 | **Focus Control** | **NEAR → FAR** slider, 1% steps and one-shot autofocus. |
| 📊 | **Live Camera Readout** | ISO, shutter angle, FPS, white balance, tint, iris, timecode, battery and card status. |
| 🎛️ | **Parameter Control** | Tap a value, then use − / + to change it on the camera. |
| 🎥 | **Record Control** | Start and stop recording with one button. |
| 💾 | **Camera Presets** | Save and recall two complete camera configurations. |


## Getting started

### 🧰 Hardware

MagicPilot is designed around a small amount of readily available hardware:

- **M5Stack Core2** — touchscreen remote and ESP32 platform
- **BMPCC6K** — with Bluetooth enabled
- **microSD card** — optional; required for preset storage.

That's it.

**No phone. No Wi-Fi network. No external computer.**

### Build and flash

#### Arduino IDE

1. Install the **esp32 by Espressif Systems** board package, version **2.0.17**, and the **M5Core2** library (0.1.6 or newer) from the Library Manager.
2. Open `MagicPilot_Remote.ino`. Keep the folder name `MagicPilot_Remote` and the `src` folder next to it, because the firmware is in `src/main.cpp`.
3. Select the board **M5Stack-Core2** and a partition scheme with at least 1.5 MB of app space (for example **16M Flash (3MB APP/9.9MB FATFS)**).
4. Select the serial port and click Upload.

#### PaltformIO

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -e m5stack-core2              # build
pio run -e m5stack-core2 -t upload    # build and flash
pio device monitor                    # serial log at 115200 baud
```

### 🎥 Camera compatibility

| Camera | Status |
|---|---|
| Blackmagic Pocket Cinema Camera 6K | 🟢 Supported |
| Pocket Cinema Camera 6K G2 | 🟡 Not Tested |
| Pocket Cinema Camera 6K Pro | 🟡 Not Tested |
| Pocket Cinema Camera 4K | 🟡 Not Tested |

## Using the remote

### Pairing

1. On the camera, open the Bluetooth setup menu and switch on Bluetooth.
2. On the remote, tap the CONNECT button. The button reads SEARCH while it looks for the camera.
3. Enter the 6-digit PIN shown on the camera on the keypad and tap OK.

The button then reads ONLINE. The status bar at the bottom shows the connection state, and the Bluetooth icon in the header turns blue when connected.

### Live readout and control

The main screen shows the camera's settings as they change. To adjust one, tap its box (it gets an orange border), then use the - / + buttons.

| Parameter | Readout | Control with - / + |
| --- | --- | --- |
| ISO | Current ISO | Fixed steps from 100 to 25600 |
| Shutter | Angle in degrees | Fixed steps from 11.2 to 360 degrees |
| FPS | Recording frame rate | Steps from 5 to 120 |
| White balance | Kelvin | 50 K steps, 2500 to 10000 K |
| Tint | Signed value | 1 step, -50 to +50 |
| Iris | f-number | One lens stop per tap |
| Timecode | Timecode or clip counter | Tap it to switch between the two |
| Record | STBY / REC / PLAY in the middle of the header | Record button |
| Camera battery | Voltage inside the battery icon after the "Camera" label, red below 6.8 V (tap the header to show) | Read only |
| Remote battery | The Core2's own charge in the icon next to the title, red below 15%, green while charging (tap the header to show) | Read only |
| Storage Cards | Type, minutes left and the active slot in the footer (tap the footer to show) | Read only |

Iris changes can only reach lenses the camera can drive.

### Focus

Tap **FOCUS** on the main screen to open the focus page.

- **Slider**: drag from NEAR to FAR to set focus directly. The current position shows as a percentage.
- **- / +**: steps of exactly 1%, repeating while held. They work once the camera has reported its focus position; until then, move the slider once.
- **AF**: triggers the camera's one-shot autofocus.

Focus changes only reach lenses the camera can drive.

### Presets

Tap **PRESETS** on the main screen. The page has two slots, each with its own **SAVE**, **LOAD** and **CLEAR** button and a short summary of what it holds.

- **SAVE**: stores ISO, shutter, FPS, white balance, tint and iris as they are on the camera.
- **LOAD**: sends the stored values to the camera. The iris is stepped to the stored f-number one stop at a time, so loading takes a moment.
- **CLEAR**: needs two taps. The first turns the button red and shows "SURE?"; any other tap cancels.
- **BACK**: returns to the main screen.

Presets live on the SD card, so they survive power-off.

### SD card

| Path | Content |
| --- | --- |
| `/presets/<date>_MagicPilot_Remote_Preset1.json`, `..._Preset2.json` | One JSON file per saved preset, for example `2026-10-05_MagicPilot_Remote_Preset1.json` |

Without a card the remote still works, but saving and loading presets shows "No SD card".


## Project layout

- `src/main.cpp`: the whole firmware (BLE client, camera protocol, UI).
- `platformio.ini`: board, framework and library settings.

## Disclaimer

⚠️ Use at your own risk. MagicPilot is provided "as is" without warranty. The developer is not responsible or liable for damage, malfunction, loss of footage, data loss, or any other loss or damage to cameras, lenses, recording media, accessories, equipment, or property resulting from the use of this software or firmware. Always verify settings, compatibility, and operation before using MagicPilot with valuable or professional equipment.

MagicPilot is an independent open-source project and is not affiliated with, endorsed by, or sponsored by Blackmagic Design Pty. Ltd.
Blackmagic Design, Blackmagic Pocket Cinema Camera, BMPCC, and related names and trademarks are the property of Blackmagic Design Pty. Ltd.
