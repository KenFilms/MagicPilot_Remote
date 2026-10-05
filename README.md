# ✨ MagicPilot Remote

### 🎬 A tiny touchscreen remote for the BMPCC6K.

![BlackmagicCameraControl](https://img.shields.io/badge/BlackmagicCameraControl-Aug_2025-green) 
![arduino-esp32](https://img.shields.io/badge/arduino--esp32-2.0.17-green)
![Platform](https://img.shields.io/badge/platform-M5Stack%20Core2-blue)
![MCU](https://img.shields.io/badge/MCU-ESP32-E7352C)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D)
![Camera](https://img.shields.io/badge/camera-BMPCC6K-black)
<!--![GitHub stars](https://img.shields.io/github/stars/MagicPilot/MagicPilot-Remote)-->

https://github.com/user-attachments/assets/88d4dcbc-c6db-4bb7-9184-9daaf6ef9eb5

[Watch the demo video](docs/video/MagicPilot_Remote.mp4)

Touchscreen remote for Blackmagic cameras (tested against the Pocket Cinema Camera 6K), running on an M5Stack Core2. It talks to the camera over Bluetooth LE using the Blackmagic Camera Control protocol.

## Table of contents

- [Overview](#overview)
  - [Why MagicPilot?](#why-magicpilot)
  - [What can it control?](#-what-can-it-control)
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
  - [Recording and timecode](#recording-and-timecode)
  - [Focus](#focus)
  - [Presets](#presets)
  - [SD card](#sd-card)
- [Project layout](#project-layout)
- [Disclaimer](#disclaimer)

## Overview

### Why MagicPilot?

The BMPCC6K is an incredibly capable camera. But many times you want more flexibility when controlling it.

**MagicPilot puts the controls you need on a tiny touchscreen that you can keep beside the camera.**

- **Next to your external monitor.** An external monitor is brighter than the camera's built-in screen, so that is where you look. MagicPilot mounts right beside it, so the settings are in your line of sight.
- **When the camera is out of reach.** In many filming situations you can't touch the camera's own controls, for example with the camera against a wall or mounted very high, such as on a crane. The remote changes settings and starts or stops recording from wherever you stand.


### 🎛 What can it control?

MagicPilot puts the most important camera controls on a **compact, responsive touchscreen**—giving you a clear view of your camera's status and quick access to the settings you need while shooting.

### ✨ Everything You need at a Glance

| | Feature | What it does |
 |---|---|---| 
 | 🔭 | **Focus Control** | Dedicated focus screen with a **NEAR → FAR** slider, precise 1% adjustments, and one-shot autofocus. | 
 | 📊 | **Live Camera Readout** | See ISO, shutter angle, FPS, white balance, tint, iris, timecode, battery levels and card status directly from the camera. | 
 | 🎛️ | **Instant Parameter Control** | Tap any adjustable value, select it, and use − / + to change it directly on the camera. |
  | 🎥 | **Record Control** | Start and stop recording with a single dedicated button. | 
  | 🕐 | **Timecode & Clip Counter** | Switch between timecode and clip count with a simple tap. | 
  | 💾 | **Camera Presets** | Save and recall two complete camera configurations, including exposure and color settings. | 
  | 🔐 | **Bluetooth Pairing** | Connect securely using the camera's standard 6-digit Bluetooth PIN. |


## Getting started

### 🧰 Hardware

MagicPilot is designed around a small amount of readily available hardware:

- **M5Stack Core2** — touchscreen remote and ESP32 platform
- **BMPCC6K** — with Bluetooth enabled
- **microSD card** — optional; required for preset storage and screenshots
That's it.

**No phone. No Wi-Fi network. No external computer.**

Just a small touchscreen remote dedicated to your camera.

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

- **Selecting**: the orange border marks the value the - / + buttons change. Holding a button repeats the step.
- **Immediate feedback**: the new value shows as soon as it is sent. The camera's own report replaces it if the camera refused the change.
- **Waiting for the camera**: values show "--" until the camera has reported them.
- **Header**: normally it shows only the title, the camera mode (STBY, REC or PLAY) and the Bluetooth icon with a blue circle once connected. Tap the header (away from the mode label) to also show the remote's battery next to the title and the camera battery marked "Camera", separated by thin vertical lines. Tap it again to hide them.
- **Footer**: normally it shows only the status message, such as Standby or Camera found. Tap the footer to also show the three card slots with their type and minutes remaining. Tap it again to hide them.

### Recording and timecode

- **Record button**: the red dot starts recording and becomes a stop square while recording. The header reads STBY, REC or PLAY.
- **Timecode**: the large readout turns red while recording. A TC badge marks the timecode view.
- **Clip counter**: tap the timecode area, or the mode label (STBY, REC or PLAY) in the header, to switch between timecode and the camera's clip counter. During recording the counter follows the camera's own clip count and keeps its last value after you stop.

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

The date in the file name comes from the camera's clock. If the camera hasn't reported it, the file is saved without a date. Saving again replaces the slot's earlier file, even if the date has changed.

A preset file looks like this. `date` and `time` are the camera's clock at the moment of saving, and are `null` if the camera hasn't reported them. `shutter_angle` is in degrees and `iris` is the camera's raw aperture value, or `null` when the lens doesn't report it:

```json
{
  "description": "MagicPilot Preset",
  "slot": 1,
  "date": "2026-10-05",
  "time": "14:32:10",
  "iso": 800,
  "shutter_angle": 180.00,
  "fps": 24,
  "white_balance": 5600,
  "tint": 0,
  "iris": 4096
}
```

Presets saved by earlier versions in `/presets.txt` are not read.

Without a card the remote still works, but saving and loading presets shows "No SD card".


## Project layout

- `src/main.cpp`: the whole firmware (BLE client, camera protocol, UI).
- `platformio.ini`: board, framework and library settings.

Bluetooth uses the ESP-IDF Bluedroid API directly. Camera commands are fixed packet templates with the value bytes filled in at send time.

## Disclaimer

⚠️ Use at your own risk. MagicPilot is provided "as is" without warranty. The developer is not responsible or liable for damage, malfunction, loss of footage, data loss, or any other loss or damage to cameras, lenses, recording media, accessories, equipment, or property resulting from the use of this software or firmware. Always verify settings, compatibility, and operation before using MagicPilot with valuable or professional equipment.

MagicPilot is an independent open-source project and is not affiliated with, endorsed by, or sponsored by Blackmagic Design Pty. Ltd.
Blackmagic Design, Blackmagic Pocket Cinema Camera, BMPCC, and related names and trademarks are the property of Blackmagic Design Pty. Ltd.
MagicPilot is developed independently for use with compatible Blackmagic cameras.
