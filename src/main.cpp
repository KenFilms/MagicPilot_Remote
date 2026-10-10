/*
 * MagicPilot Remote
 *
 * Author: Ken Friedl https://github.com/KenFilms
 * First Version:   2026-10-04
 * 
 * Current version: 1.0.0
 *
 * Touchscreen remote for Blackmagic cameras (tested with the Pocket Cinema Camera 6K), running on an
 * M5Stack Core2. It talks to the camera over Bluetooth LE with the Blackmagic Camera Control protocol,
 * using the ESP-IDF Bluedroid GATT client.
 *
 * Capabilities:
 *  - Pairs with the camera using the 6-digit PIN entered on an on-screen keypad.
 *  - Live readout of ISO, shutter angle, FPS, white balance, tint, iris, timecode, battery voltage,
 *    record state and card slots.
 *  - Changes ISO, shutter angle, FPS, white balance, tint and iris with the - / + buttons.
 *  - Starts and stops recording, and switches between timecode and clip counter.
 *  - Focus page with a slider, fine steps and one-shot autofocus.
 *  - Presets page with two presets that are saved to, loaded from and cleared on the SD card.
 *  - Optional UI screenshots to the SD card (see CAPTURE_SCREENSHOTS).
 *
 * The firmware is split into headers by responsibility:
 *  - config.h          shared includes, constants, colours and small value types
 *  - globals.h         mutable state, value-step tables and outgoing packet templates
 *  - ble_protocol.h    Blackmagic packet encoding/decoding and camera state updates
 *  - ble_connection.h  BLE scanning, pairing, GATT discovery and notifications
 *  - ui_main_screen.h  shared drawing helpers and the main screen
 *  - ui_pin_screen.h   the pairing PIN keypad
 *  - screenshot.h      BMP screenshot capture to the SD card
 *  - value_stepping.h  - / + button handling for the value grid
 *  - presets.h         preset storage on the SD card
 *  - ui_presets_screen.h the preset page
 *  - ui_focus_screen.h the focus page
 *  - touch_router.h    touch routing and dirty-flag redraws
 */

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"
#include "ble_connection.h"
#include "ui_main_screen.h"
#include "ui_pin_screen.h"
#include "screenshot.h"
#include "value_stepping.h"
#include "presets.h"
#include "ui_presets_screen.h"
#include "ui_focus_screen.h"
#include "touch_router.h"

// ============================================================================
// ARDUINO ENTRY POINTS
// ============================================================================

// Skips past the captures already on the card so a reboot doesn't overwrite them.
static void seekFirstFreeCaptureIndex() {
    if (!SD.exists(CAPTURE_DIR)) SD.mkdir(CAPTURE_DIR);
    char path[48];
    while (true) {
        snprintf(path, sizeof(path), "%s/ui_%05lu.bmp", CAPTURE_DIR, (unsigned long)nextScreenshotIndex);
        if (!SD.exists(path)) return;
        ++nextScreenshotIndex;
    }
}

// Initializes the display and SD card, starts Bluetooth, and draws the main screen.
void setup() {
    M5.begin(true, true, true, true);
    M5.Lcd.setRotation(1);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);
    Serial.begin(115200);
    sdReady = mountSd();
    Serial.printf("[SD] %s, card type %d, %llu MB\n", sdReady ? "mounted" : "not mounted", (int)SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL));
    if (sdReady && CAPTURE_SCREENSHOTS) seekFirstFreeCaptureIndex();

    initializeBluetooth();
    drawScreen();
    Serial.println("[BOOT] BMPCC remote ready; tap CONNECT to scan");
}

// Answers the camera's passkey request with the digits typed on the keypad.
static void replyWithEnteredPin() {
    uint32_t pin = 0;
    for (uint8_t i = 0; i < 6; ++i) pin = pin * 10 + (uint32_t)(pinEntry[i] - '0');
    const esp_err_t result = esp_ble_passkey_reply(pairingBda, true, pin);
    passkeyReplyPending = false;
    pinSubmitted = false;
    pinRequested = false;
    pinScreenDrawn = false;
    drawScreen();
    requestScreenshot();
    if (result != ESP_OK) setStatus("Passkey reply failed");
}

// Dispatches the current touch, repeating it while a stepper or the focus slider is held.
static void handleTouchInput() {
    // The INT pin can go low before the controller is read; that stale point is (-1,-1) and must not consume the tap.
    const TouchPoint_t point = M5.Touch.getPressPoint();
    const bool touchDown = M5.Touch.ispressed() && point.x >= 0 && point.y >= 0;
    if (touchDown) {
        const uint32_t now = millis();
        if (!touchWasDown) Serial.printf("[TOUCH] down x=%d y=%d\n", point.x, point.y);
        const bool repeatStep = !pinRequested && screen == SCREEN_MAIN && inside(point.x, point.y, STEP_PAD);
        const bool repeatFocus = !pinRequested && screen == SCREEN_FOCUS && focusTouchRepeats(point.x, point.y);
        if (!touchWasDown || (repeatStep && now - lastStepMs >= 220) || (repeatFocus && now - lastStepMs >= FOCUS_REPEAT_MS)) {
            handleTouch(point.x, point.y);
            lastStepMs = now;
        }
    }
    touchWasDown = touchDown;
}

// Applies dirty-flag redraws, answers the pairing PIN, handles touch, and saves queued screenshots.
void loop() {
    M5.update();
    updateDirtyDisplay();
    updateRemoteBattery();
    advancePresetLoad();
    if (passkeyReplyPending && pinSubmitted) replyWithEnteredPin();
    if (pinRequested && !pinScreenDrawn) drawPinScreen();
    handleTouchInput();
    writeBmpCapture();
    delay(2);
}