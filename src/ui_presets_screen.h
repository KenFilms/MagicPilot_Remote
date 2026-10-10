#pragma once

// UI: the preset page — row layout, SAVE/LOAD/CLEAR buttons and touch handling.

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"
#include "ui_main_screen.h"
#include "presets.h"

static const int PRESET_ROW_Y[PRESET_COUNT] = {34, 100};  // top edge of each preset row
static const int PRESET_ROW_H = 60;  // height of the box around one preset
static const int PRESET_ACTION_OFFSET_Y = 8, PRESET_ACTION_H = 44, PRESET_ACTION_W = 56;  // buttons inside a row
static const int PRESET_SAVE_X = 132, PRESET_LOAD_X = 193, PRESET_CLEAR_X = 254;
static const Rect PRESET_BACK = {6, 164, 154, 38};
static int clearArmedSlot = -1;  // slot waiting for a second CLEAR tap

// One action key (SAVE, LOAD or CLEAR); the outline is only drawn where it differs from the fill.
static void drawActionButton(int x, int y, uint16_t fill, uint16_t outline, uint16_t textColor, const char* label) {
    M5.Lcd.fillRoundRect(x, y, PRESET_ACTION_W, PRESET_ACTION_H, 4, fill);
    if (outline != fill) M5.Lcd.drawRoundRect(x, y, PRESET_ACTION_W, PRESET_ACTION_H, 4, outline);
    M5.Lcd.setTextColor(textColor, fill);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(label, x + PRESET_ACTION_W / 2, y + PRESET_ACTION_H / 2, 2);
}

// One preset row: summary of the saved values with SAVE, LOAD and CLEAR buttons.
static void drawPresetRow(uint8_t slot) {
    const int y = PRESET_ROW_Y[slot];
    const Preset& preset = presets[slot];
    char text[40];
    M5.Lcd.fillRect(5, y, 310, PRESET_ROW_H, BLACK);
    M5.Lcd.drawRoundRect(5, y, 310, PRESET_ROW_H, 4, GREY);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setTextColor(WHITE, BLACK);
    snprintf(text, sizeof(text), "PRESET %d", (int)slot + 1);
    M5.Lcd.drawString(text, 12, y + 6, 2);
    if (preset.used) {
        char shutter[12];
        formatShutter(shutter, sizeof(shutter), preset.shutter);
        snprintf(text, sizeof(text), "ISO %ld  FPS %d", (long)preset.iso, (int)preset.fps);
        M5.Lcd.drawString(text, 12, y + 24, 1);
        snprintf(text, sizeof(text), "SHUT %s  %dK", shutter, (int)preset.whiteBalance);
        M5.Lcd.drawString(text, 12, y + 34, 1);
        if (preset.iris != INT16_MIN) {
            snprintf(text, sizeof(text), "TINT %+d  f/%.1f", (int)preset.tint, irisFNumber(preset.iris));
        } else {
            snprintf(text, sizeof(text), "TINT %+d", (int)preset.tint);
        }
        M5.Lcd.drawString(text, 12, y + 44, 1);
    } else {
        M5.Lcd.setTextColor(GREY, BLACK);
        M5.Lcd.drawString("EMPTY", 12, y + 32, 1);
    }

    const int buttonY = y + PRESET_ACTION_OFFSET_Y;
    const bool armed = clearArmedSlot == slot;
    drawActionButton(PRESET_SAVE_X, buttonY, UI_CONNECT, UI_CONNECT, WHITE, "SAVE");
    drawActionButton(PRESET_LOAD_X, buttonY, BLACK, preset.used ? UI_GREEN : GREY, preset.used ? WHITE : GREY, "LOAD");
    drawActionButton(PRESET_CLEAR_X, buttonY, armed ? RED : BLACK, armed ? RED : (preset.used ? RED : GREY),
                     armed || preset.used ? WHITE : GREY, armed ? "SURE?" : "CLEAR");
}

// Draws the preset page: a row per slot, the BACK button and the shared footer.
static void drawPresetScreen() {
    drawSubHeader("PRESETS");
    for (uint8_t slot = 0; slot < PRESET_COUNT; ++slot) drawPresetRow(slot);
    drawOutlineButton(PRESET_BACK, "BACK", 4);
    drawStatus();
}

static void drawFocusScreen();  // defined in ui_focus_screen.h, so openScreen can reach it

// Switches page and draws it; the main screen is always redrawn in full.
static void openScreen(uint8_t next) {
    screen = next;
    if (next == SCREEN_PRESETS) {
        clearArmedSlot = -1;
        readPresetFile();
        drawPresetScreen();
        return;
    }
    if (next == SCREEN_FOCUS) {
        drawFocusScreen();
        return;
    }
    drawScreen();
    requestScreenshot();
}

// BACK returns to the main screen; SAVE, LOAD and CLEAR act on their row, and CLEAR needs two taps.
static void handlePresetTouch(int x, int y) {
    if (inside(x, y, PRESET_BACK)) {
        clearArmedSlot = -1;
        openScreen(SCREEN_MAIN);
        return;
    }
    for (uint8_t slot = 0; slot < PRESET_COUNT; ++slot) {
        const int actionTop = PRESET_ROW_Y[slot] + PRESET_ACTION_OFFSET_Y;
        if (y < actionTop || y >= actionTop + PRESET_ACTION_H) continue;
        if (x >= PRESET_CLEAR_X && x < PRESET_CLEAR_X + PRESET_ACTION_W) {
            if (clearArmedSlot == slot) {
                clearArmedSlot = -1;
                clearPreset(slot);
            } else {
                clearArmedSlot = slot;
                setPresetStatus("Tap CLEAR again to erase preset %d", slot);
            }
            drawPresetScreen();
            return;
        }
        clearArmedSlot = -1;
        if (x >= PRESET_SAVE_X && x < PRESET_SAVE_X + PRESET_ACTION_W) savePreset(slot);
        else if (x >= PRESET_LOAD_X && x < PRESET_LOAD_X + PRESET_ACTION_W) loadPreset(slot);
        drawPresetScreen();
        return;
    }
    if (clearArmedSlot >= 0) {
        clearArmedSlot = -1;
        drawPresetScreen();
    }
}
