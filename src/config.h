#pragma once

// Shared includes, constants, colours and small value types used across the firmware.

#include <M5Core2.h>
#include <esp32-hal-bt.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_gatt_common_api.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

// 0 = never write UI screenshots to the SD card (and don't create /ui_captures). Leave default value 0!!! It will slow down the UI if set to 1.
static const uint8_t CAPTURE_SCREENSHOTS = 0;

// Blackmagic Camera Control service and its characteristics, as advertised by the camera.
static const char* CAMERA_SERVICE_UUID = "291D567A-6D75-11E6-8B77-86F30CA893D3";
static const char* OUTGOING_UUID = "5DD3465F-1AEE-4299-8493-D2ECA2F8E1BB";   // control packets we send
static const char* INCOMING_UUID = "B864E140-76A0-416A-BF30-5876504537D9";   // control packets the camera reports
static const char* TIMECODE_UUID = "6D8F2110-86F1-41BF-9AFB-451D87E976C8";   // timecode notifications
static const char* STATUS_UUID = "7FE8691D-95DC-4FC5-8ABD-CA74339B51B9";     // camera status notifications
static const char* DEVICE_NAME_UUID = "FFAC0C52-C9FB-41A0-B063-CC76282EB89C";  // the name this remote reports to the camera

// RGB565 colours. The M5 library already defines BLACK, WHITE and RED.
static const uint16_t GREY = 0x8410;        // inactive borders and labels
static const uint16_t DARK_GREY = 0x4208;   // disabled keypad border
static const uint16_t ACCENT_BLUE = 0x051F;  // unused, kept with the palette
static const uint16_t UI_BAR = 0x1082;      // header and footer background
static const uint16_t UI_GREEN = 0x07E0;    // charging and active slot marker
static const uint16_t UI_BT_BLUE = 0x2DFF;  // Bluetooth icon while connected
static const uint16_t UI_CONNECT = 0x1A91;  // filled action buttons
static const uint16_t UI_SEL = 0xFD20;      // border of the selected value cell

// Cells of the value grid, in on-screen order.
enum CameraValue : uint8_t { VALUE_ISO, VALUE_SHUTTER, VALUE_FPS, VALUE_WB, VALUE_TINT, VALUE_IRIS };

// Which page is on screen.
enum Screen : uint8_t { SCREEN_MAIN, SCREEN_PRESETS, SCREEN_FOCUS };

// Recording format as reported by the camera (category 1, parameter 9).
struct RecordingFormat {
    int16_t fileFps;    // frame rate written to the card
    int16_t sensorFps;  // sensor rate; differs from fileFps in off-speed modes
    int16_t width;
    int16_t height;
    int16_t flags;      // codec and sensor window bits, echoed back unchanged when setting the rate
};

// Screen area, used for both drawing and hit testing.
struct Rect {
    int16_t x, y, w, h;
};

static const int16_t FOCUS_MAX = 2048;  // camera focus is fixed16: 0 is near, 2048 is far
