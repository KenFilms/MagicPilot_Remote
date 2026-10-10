#pragma once

// UI: main screen touch routing and dirty-flag redraws.

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"
#include "ble_connection.h"
#include "ui_main_screen.h"
#include "ui_pin_screen.h"
#include "value_stepping.h"
#include "ui_presets_screen.h"
#include "ui_focus_screen.h"

// Switches the timecode row between timecode and the clip counter.
static void toggleTimecodeSource() {
    timecodeSource = timecodeSource == 0 ? 1 : 0;
    timecodeDirty = true;
    sendPacket(TIMECODE_SOURCE_PACKET, timecodeSource, 1);
}

// Routes a touch to the PIN keypad, the preset or focus screen, or a main screen control.
static void handleTouch(int x, int y) {
    if (pinRequested) {
        handlePinTouch(x, y);
        return;
    }
    if (y >= 205) {
        footerExpanded = !footerExpanded;
        drawStatus();
        return;
    }
    if (screen == SCREEN_PRESETS) {
        handlePresetTouch(x, y);
        return;
    }
    if (screen == SCREEN_FOCUS) {
        handleFocusTouch(x, y);
        return;
    }
    if (y < 28) {
        if (cameraConnected && abs(x - modeCenterX()) <= 35) toggleTimecodeSource();
        else {
            headerExpanded = !headerExpanded;
            drawTopBar();
        }
        return;
    }
    if (inside(x, y, RECORD_PAD)) {
        if (cameraConnected) sendPacket(TRANSPORT_PACKET, transportMode == 2 ? 0 : 2, 1);
    } else if (inside(x, y, VALUE_GRID)) {
        const uint8_t column = x < VALUE_COLUMN_SPLIT_X[0] ? 0 : x < VALUE_COLUMN_SPLIT_X[1] ? 1 : 2;
        selectedValue = y < VALUE_ROW_SPLIT_Y ? column : (uint8_t)(VALUE_WB + column);
        drawValues();
        requestScreenshot();
    } else if (inside(x, y, STEP_PAD)) {
        adjustSelected(x < STEP_SPLIT_X ? -1 : 1);
    } else if (inside(x, y, CONNECT_PAD)) {
        if (!cameraConnected) beginConnection();
    } else if (inside(x, y, PRESET_BUTTON)) {
        openScreen(SCREEN_PRESETS);
    } else if (inside(x, y, FOCUS_BUTTON)) {
        openScreen(SCREEN_FOCUS);
    } else if (y >= 28 && y <= 55 && cameraConnected) {
        toggleTimecodeSource();
    }
}

// Redraws only what changed since the last pass; the preset and focus screens handle their own drawing.
static void updateDirtyDisplay() {
    if (focusDirty) {
        focusDirty = false;
        if (screen == SCREEN_FOCUS) drawFocusDynamic();  // focus value changed while that page is open
    }
    if (screen != SCREEN_MAIN) {
        // The preset and focus pages draw everything else themselves; only the shared footer lands here.
        if (statusDirty) { statusDirty = false; drawStatus(); }
        return;
    }
    if (valuesDirty) {
        // ISO, shutter, FPS, white balance, tint or iris changed.
        valuesDirty = false;
        drawValues();
        requestScreenshot();
    }
    if (timecodeDirty) { timecodeDirty = false; drawTimecode(); }  // timecode or clip counter changed
    if (transportDirty) {
        // Record state changed; it also affects the header's REC indicator and the slot markers.
        transportDirty = false;
        drawTopBar(); drawSlots(); drawRecordButton(); drawTimecode();
        requestScreenshot();
    }
    if (statusDirty) { statusDirty = false; drawTopBar(); drawStatus(); }  // voltage, slots, or a new status message
    if (connectionDirty) {
        // Connected or disconnected; redraw everything rather than track what changed.
        connectionDirty = false;
        drawScreen();
        requestScreenshot();
    }
}
