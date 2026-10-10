#pragma once

// UI: focus page.

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"
#include "ui_main_screen.h"
#include "ui_presets_screen.h"  // openScreen(SCREEN_MAIN)

// Focus page geometry. FOCUS_SLIDER_PAD is the slack around the slider that still counts as a drag.
enum FocusKey : uint8_t { FOCUS_KEY_MINUS, FOCUS_KEY_AF, FOCUS_KEY_PLUS };
static const Rect FOCUS_SLIDER = {20, 64, 280, 30};       // drawn track
static const Rect FOCUS_SLIDER_PAD = {14, 56, 292, 46};   // slack around the track that still counts as a drag
static const Rect FOCUS_KEYS[3] = {{8, 114, 96, 50}, {112, 114, 96, 50}, {216, 114, 96, 50}};  // in FocusKey order
static const Rect FOCUS_BACK = {6, 170, 154, 32};
static const uint32_t FOCUS_REPEAT_MS = 100;              // repeat interval while a focus key or the slider is held

// Redraws only the percentage and slider, so dragging doesn't flicker.
static void drawFocusDynamic() {
    char text[8] = "--";
    if (cameraFocus >= 0) snprintf(text, sizeof(text), "%d%%", ((int)cameraFocus * 100 + FOCUS_MAX / 2) / FOCUS_MAX);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextPadding(100);
    M5.Lcd.drawString(text, 160, 45, 4);
    M5.Lcd.setTextPadding(0);

    M5.Lcd.fillRect(FOCUS_SLIDER.x - 4, FOCUS_SLIDER.y - 2, FOCUS_SLIDER.w + 8, FOCUS_SLIDER.h + 4, BLACK);
    M5.Lcd.drawRect(FOCUS_SLIDER.x, FOCUS_SLIDER.y, FOCUS_SLIDER.w, FOCUS_SLIDER.h, GREY);
    if (cameraFocus < 0) return;
    const int position = (int)cameraFocus * (FOCUS_SLIDER.w - 1) / FOCUS_MAX;
    if (position > 0) M5.Lcd.fillRect(FOCUS_SLIDER.x + 1, FOCUS_SLIDER.y + 1, position, FOCUS_SLIDER.h - 2, UI_CONNECT);
    M5.Lcd.fillRect(FOCUS_SLIDER.x + position - 2, FOCUS_SLIDER.y - 2, 5, FOCUS_SLIDER.h + 4, WHITE);
}

// Draws the focus page: slider, - / AF / + keys and the BACK button.
static void drawFocusScreen() {
    drawSubHeader("FOCUS");
    M5.Lcd.setTextColor(GREY, BLACK);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.drawString("NEAR", FOCUS_SLIDER.x, FOCUS_SLIDER.y + FOCUS_SLIDER.h + 6, 1);
    M5.Lcd.setTextDatum(TR_DATUM);
    M5.Lcd.drawString("FAR", FOCUS_SLIDER.x + FOCUS_SLIDER.w, FOCUS_SLIDER.y + FOCUS_SLIDER.h + 6, 1);
    drawFocusDynamic();

    const Rect& af = FOCUS_KEYS[FOCUS_KEY_AF];
    drawStepperPair(FOCUS_KEYS[FOCUS_KEY_MINUS], FOCUS_KEYS[FOCUS_KEY_PLUS], 20, 4);
    M5.Lcd.fillRoundRect(af.x, af.y, af.w, af.h, 8, UI_CONNECT);
    M5.Lcd.setTextColor(WHITE, UI_CONNECT);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("AF", af.x + af.w / 2, af.y + af.h / 2, 4);

    drawOutlineButton(FOCUS_BACK, "BACK", 4);
    drawStatus();
}

// The slider and the -/+ keys repeat while held; AF does not.
static bool focusTouchRepeats(int x, int y) {
    return inside(x, y, FOCUS_SLIDER_PAD) || inside(x, y, FOCUS_KEYS[FOCUS_KEY_MINUS]) ||
           inside(x, y, FOCUS_KEYS[FOCUS_KEY_PLUS]);
}

// BACK leaves the page; the slider jumps to the tapped position and the keys nudge by one percent.
static void handleFocusTouch(int x, int y) {
    if (inside(x, y, FOCUS_BACK)) {
        openScreen(SCREEN_MAIN);
        return;
    }
    const bool onSlider = inside(x, y, FOCUS_SLIDER_PAD);
    int key = -1;
    for (uint8_t index = 0; index < 3; ++index) {
        if (inside(x, y, FOCUS_KEYS[index])) key = index;
    }
    if (!onSlider && key < 0) return;
    if (!cameraConnected) {
        setStatus("Connect camera to set focus");
        return;
    }
    if (key == FOCUS_KEY_AF) {
        // One-shot autofocus; the camera reports the new focus afterwards.
        setStatus(writeCameraPacket(AUTOFOCUS_PACKET, sizeof(AUTOFOCUS_PACKET)) ? "Autofocus triggered" : "Autofocus failed");
        return;
    }
    if (onSlider) {
        sendFocus((x - FOCUS_SLIDER.x) * FOCUS_MAX / FOCUS_SLIDER.w);
    } else {
        if (cameraFocus < 0) {
            setStatus("Focus unknown, use the slider");
            return;
        }
        // Step in whole percent so the readout changes by exactly 1 per tap.
        const int percent = ((int)cameraFocus * 100 + FOCUS_MAX / 2) / FOCUS_MAX + (key == FOCUS_KEY_PLUS ? 1 : -1);
        sendFocus((constrain(percent, 0, 100) * FOCUS_MAX + 50) / 100);
    }
    drawFocusDynamic();
}
