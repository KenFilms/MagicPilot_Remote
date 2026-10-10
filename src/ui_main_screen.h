#pragma once

// UI DRAWING — shared helpers and the main screen.

#include "config.h"
#include "globals.h"

// Draws centred text with extra pixels between the letters.
static void drawSpacedString(const char* text, int centerX, int centerY, uint8_t font, int spacing) {
    const int count = (int)strlen(text);
    int total = spacing * (count - 1);
    for (int i = 0; i < count; ++i) {
        const char glyph[2] = {text[i], '\0'};
        total += M5.Lcd.textWidth(glyph, font);
    }
    int x = centerX - total / 2;
    M5.Lcd.setTextDatum(ML_DATUM);
    for (int i = 0; i < count; ++i) {
        const char glyph[2] = {text[i], '\0'};
        M5.Lcd.drawString(glyph, x, centerY, font);
        x += M5.Lcd.textWidth(glyph, font) + spacing;
    }
}

// True if the touch point falls inside the area.
static bool inside(int x, int y, const Rect& area) {
    return x >= area.x && x < area.x + area.w && y >= area.y && y < area.y + area.h;
}

// Main screen geometry. Drawing and hit testing read the same rectangles so they cannot drift apart.
// The *_PAD areas are the generous touch targets around the smaller shapes that are actually drawn.
static const Rect RECORD_PAD = {253, 56, 67, 108};        // the record circle and the column under it
static const Rect VALUE_GRID = {6, 56, 239, 72};          // both rows of value cells
static const Rect STEP_PAD = {126, 130, 123, 71};         // spans both stepper buttons and the gap between them
static const Rect STEP_MINUS = {131, 130, 54, 71};        // drawn outline of the - button
static const Rect STEP_PLUS = {190, 130, 54, 71};         // drawn outline of the + button
static const Rect CONNECT_PAD = {249, 164, 71, 38};
static const Rect CONNECT_BUTTON = {249, 167, 70, 34};    // drawn outline, inset from CONNECT_PAD
static const Rect FOCUS_BUTTON = {6, 130, 118, 34};       // opens the focus page
static const Rect PRESET_BUTTON = {6, 167, 118, 34};      // opens the preset page
static const int STEP_SPLIT_X = 188;                     // left of this steps down, right of it steps up
static const int VALUE_COLUMN_SPLIT_X[2] = {84, 164};    // right edge of the first and second value column
static const int VALUE_ROW_SPLIT_Y = 91;                 // bottom edge of the first value row

// Outlined button with a letter-spaced label; radius 0 gives square corners.
static void drawOutlineButton(const Rect& button, const char* label, int radius) {
    M5.Lcd.fillRect(button.x, button.y, button.w, button.h, BLACK);
    if (radius > 0) M5.Lcd.drawRoundRect(button.x, button.y, button.w, button.h, radius, GREY);
    else M5.Lcd.drawRect(button.x, button.y, button.w, button.h, GREY);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    drawSpacedString(label, button.x + button.w / 2, button.y + button.h / 2, 2, 2);
}

// Minus or plus sign centred in a button.
static void drawStepGlyph(const Rect& button, bool plus, int length, int thickness) {
    const int centerX = button.x + button.w / 2, centerY = button.y + button.h / 2;
    M5.Lcd.fillRect(centerX - length / 2, centerY - thickness / 2, length, thickness, WHITE);
    if (plus) M5.Lcd.fillRect(centerX - thickness / 2, centerY - length / 2, thickness, length, WHITE);
}

// A - and + button pair, each outlined and given its glyph. Used on the main screen and the focus page.
static void drawStepperPair(const Rect& minusButton, const Rect& plusButton, int glyphLength, int glyphThickness) {
    M5.Lcd.drawRoundRect(minusButton.x, minusButton.y, minusButton.w, minusButton.h, 8, WHITE);
    M5.Lcd.drawRoundRect(plusButton.x, plusButton.y, plusButton.w, plusButton.h, 8, WHITE);
    drawStepGlyph(minusButton, false, glyphLength, glyphThickness);
    drawStepGlyph(plusButton, true, glyphLength, glyphThickness);
}

// A labelled value box; the selected one gets a double orange border.
static void drawCell(int x, int y, int w, int h, const char* title, const char* value, bool selected, bool degree = false) {
    const uint16_t color = selected ? UI_SEL : GREY;
    M5.Lcd.fillRect(x, y, w, h, BLACK);
    M5.Lcd.drawRect(x, y, w, h, color);
    if (selected) M5.Lcd.drawRect(x + 1, y + 1, w - 2, h - 2, color);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setTextColor(color, BLACK);
    M5.Lcd.drawString(title, x + 6, y + 4, 1);
    // No background colour: the free font's fill box is taller than the text and would erase the bottom border.
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setFreeFont(FSS9);
    M5.Lcd.drawString(value, x + 6, y + 16);
    if (degree) M5.Lcd.drawCircle(x + 6 + M5.Lcd.textWidth(value) + 4, y + 19, 2, WHITE);
    M5.Lcd.setFreeFont(nullptr);
    M5.Lcd.setTextDatum(ML_DATUM);
}

// f-number = sqrt(2^AV); interpolates a 1/8-stop table so math.h isn't needed.
static float irisFNumber(int16_t raw) {
    static const float fractionalStops[] = {1.0f, 1.0442738f, 1.0905077f, 1.1387886f,
                                             1.1892071f, 1.2418578f, 1.2968396f, 1.3542555f,
                                             1.41421356f};
    int wholeStops = raw / 2048;
    int remainder = raw % 2048;
    if (remainder < 0) {
        --wholeStops;
        remainder += 2048;
    }

    const int fractionIndex = remainder / 256;
    const int fractionPart = remainder % 256;
    const float lowerFactor = fractionalStops[fractionIndex];
    const float upperFactor = fractionalStops[fractionIndex + 1];
    float fNumber = lowerFactor + (upperFactor - lowerFactor) * (fractionPart / 256.0f);
    while (wholeStops > 0) {
        fNumber *= 1.41421356f;
        --wholeStops;
    }
    while (wholeStops < 0) {
        fNumber /= 1.41421356f;
        ++wholeStops;
    }
    return fNumber;
}

// Shutter angle from camera units (degrees * 100), with a decimal only when needed.
static void formatShutter(char* out, size_t size, int32_t units) {
    const int32_t tenths = (units + 5) / 10;
    if (tenths % 10 == 0) snprintf(out, size, "%ld", (long)(tenths / 10));
    else snprintf(out, size, "%ld.%ld", (long)(tenths / 10), (long)(tenths % 10));
}

// Position and label of each value cell, in CameraValue order.
static const struct { int16_t x, y; const char* title; } VALUE_CELLS[6] = {
    {6, 56, "ISO"}, {86, 56, "SHUTTER"}, {166, 56, "FPS"},
    {6, 93, "WHITE BAL"}, {86, 93, "TINT"}, {166, 93, "IRIS"},
};

// Formats one camera value for the grid; "--" until the camera has reported it.
static void valueText(uint8_t cell, char* out, size_t size) {
    strcpy(out, "--");
    if (!cameraConnected) return;
    switch (cell) {
        case VALUE_ISO: if (cameraIso > 0) snprintf(out, size, "%ld", (long)cameraIso); break;
        case VALUE_SHUTTER: if (cameraShutter > 0) formatShutter(out, size, cameraShutter); break;
        case VALUE_FPS: if (recordingFormat.fileFps > 0) snprintf(out, size, "%d", recordingFormat.fileFps); break;
        case VALUE_WB: if (cameraWhiteBalance > 0) snprintf(out, size, "%dK", cameraWhiteBalance); break;
        case VALUE_TINT: if (cameraWhiteBalance > 0) snprintf(out, size, "%+d", cameraTint); break;
        case VALUE_IRIS: if (cameraIris != INT16_MIN) snprintf(out, size, "f/%.1f", irisFNumber(cameraIris)); break;
    }
}

// Fixed-width cells keep the colons in place while the digits change.
static const int TIMECODE_CHARS = 11, TIMECODE_DIGIT_CELL = 11, TIMECODE_COLON_CELL = 6, TIMECODE_LEFT = 72;
static const Rect TIMECODE_BADGE = {202, 36, 24, 13};  // spans the same rows as the timecode digits
static char drawnTimecode[TIMECODE_CHARS + 1] = {};  // last glyph drawn per cell, so only changes are repainted
static uint16_t drawnTimecodeColor = BLACK;  // BLACK means the cache is stale, as it is never a text colour

// The timecode, or the clip counter when that is the selected source.
static void timecodeText(char* out, size_t size) {
    snprintf(out, size, "--:--:--:--");
    if (!haveTimecode) return;
    if (timecodeSource == 0) {
        snprintf(out, size, "%02X:%02X:%02X:%02X", timecodeBytes[3], timecodeBytes[2], timecodeBytes[1], timecodeBytes[0]);
        return;
    }
    // While recording, the camera sends its clip counter in the timecode data; keep the last value after stopping.
    if (transportMode == 2) {
        for (uint8_t i = 0; i < 4; ++i) clipHeldBytes[i] = clipLive ? timecodeBytes[i] : 0;
    }
    snprintf(out, size, "%02X:%02X:%02X:%02X", clipHeldBytes[3], clipHeldBytes[2], clipHeldBytes[1], clipHeldBytes[0]);
}

// The TC badge shows only in timecode mode.
static void drawTimecodeBadge() {
    if (timecodeSource != 0) {
        M5.Lcd.fillRect(TIMECODE_BADGE.x, TIMECODE_BADGE.y, TIMECODE_BADGE.w, TIMECODE_BADGE.h, BLACK);
        return;
    }
    M5.Lcd.fillRect(TIMECODE_BADGE.x, TIMECODE_BADGE.y, TIMECODE_BADGE.w, TIMECODE_BADGE.h, WHITE);
    M5.Lcd.setTextColor(BLACK, WHITE);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("TC", TIMECODE_BADGE.x + TIMECODE_BADGE.w / 2, TIMECODE_BADGE.y + TIMECODE_BADGE.h / 2 + 1, 1);
}

// Redraws only the character cells whose glyph changed, so the digits don't flicker.
static void drawTimecode() {
    char text[20];
    timecodeText(text, sizeof(text));
    const uint16_t color = transportMode == 2 ? RED : WHITE;
    if (color != drawnTimecodeColor) {
        memset(drawnTimecode, 0, sizeof(drawnTimecode));
        drawnTimecodeColor = color;
    }
    M5.Lcd.setTextColor(color, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextPadding(0);
    M5.Lcd.setFreeFont(FSS9);
    int cellX = TIMECODE_LEFT;
    for (int index = 0; index < TIMECODE_CHARS && text[index] != '\0'; ++index) {
        const int cellWidth = text[index] == ':' ? TIMECODE_COLON_CELL : TIMECODE_DIGIT_CELL;
        if (text[index] != drawnTimecode[index]) {
            const char glyph[2] = {text[index], '\0'};
            M5.Lcd.fillRect(cellX, 34, cellWidth, 20, BLACK);
            M5.Lcd.drawString(glyph, cellX + cellWidth / 2, 41);
            drawnTimecode[index] = text[index];
        }
        cellX += cellWidth;
    }
    M5.Lcd.setFreeFont(nullptr);
    M5.Lcd.setTextFont(1);
    drawTimecodeBadge();
    M5.Lcd.setTextDatum(ML_DATUM);
}

// Right edge of the Core2 battery icon (including its tip) and left edge of the Cam label.
static int coreBatteryRight() {
    return 8 + M5.Lcd.textWidth("MagicPilot", 2) + 6 + 37;
}

// Left edge of the Camera label, which the voltage readout clears up to.
static int camLabelLeft() {
    return 237 - M5.Lcd.textWidth("Camera", 2);
}

// Battery pill (34x14 rounded box with a tip), 7 px down from the top, with text centred inside.
static void drawBatteryPill(int x, const char* text, uint16_t color) {
    M5.Lcd.fillRoundRect(x, 7, 34, 14, 2, color);
    M5.Lcd.fillRect(x + 34, 11, 3, 6, color);
    M5.Lcd.setTextColor(BLACK, color);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(text, x + 17, 14, 1);
}

// Battery icon with the voltage inside it.
static void drawVoltage() {
    char voltage[10] = "--.-V";
    if (cameraVoltage > 0) {
        const int tenths = (cameraVoltage + 50) / 100;
        snprintf(voltage, sizeof(voltage), "%d.%dV", tenths / 10, tenths % 10);
    }
    const uint16_t color = cameraVoltage <= 0 ? GREY : (cameraVoltage < 6800 ? RED : WHITE);  // millivolts
    const int clearLeft = camLabelLeft() - 3;
    M5.Lcd.fillRect(clearLeft, 0, 280 - clearLeft, 27, UI_BAR);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(MR_DATUM);
    M5.Lcd.drawString("Camera", 237, 14, 2);
    drawBatteryPill(241, voltage, color);
}

// Bluetooth rune, 9x14 px, centered on centerX.
static void drawBluetoothIcon(int centerX, int topY, uint16_t color) {
    const int bottomY = topY + 14;
    for (int offset = 0; offset < 2; ++offset) {
        const int x = centerX + offset;
        M5.Lcd.drawLine(x, topY, x, bottomY, color);
        M5.Lcd.drawLine(x, topY, x + 4, topY + 4, color);
        M5.Lcd.drawLine(x + 4, topY + 4, x - 4, topY + 10, color);
        M5.Lcd.drawLine(x, bottomY, x + 4, bottomY - 4, color);
        M5.Lcd.drawLine(x + 4, bottomY - 4, x - 4, topY + 4, color);
    }
}

static int remoteBatteryPercent = -1;  // the Core2's own battery; -1 until first read
static bool remoteCharging = false;    // USB power present
static uint32_t lastBatteryReadMs = 0;  // the AXP is only polled every 10 s

// The Core2's own battery as a small icon with the percentage inside, next to the title.
static void drawRemoteBattery() {
    if (remoteBatteryPercent < 0) return;
    const int x = 8 + M5.Lcd.textWidth("MagicPilot", 2) + 6;
    char text[6];
    snprintf(text, sizeof(text), "%d%%", remoteBatteryPercent);
    const uint16_t color = remoteCharging ? UI_GREEN : (remoteBatteryPercent < 15 ? RED : WHITE);
    M5.Lcd.fillRect(x - 2, 7, 42, 14, UI_BAR);
    drawBatteryPill(x, text, color);
}

// Reads the Core2's battery every 10 s and redraws the icon only when it changed.
static void updateRemoteBattery() {
    const uint32_t now = millis();
    if (remoteBatteryPercent >= 0 && now - lastBatteryReadMs < 10000) return;
    lastBatteryReadMs = now;
    const int percent = constrain((int)(M5.Axp.GetBatteryLevel() + 0.5f), 0, 100);
    const bool charging = M5.Axp.isCharging();
    if (percent == remoteBatteryPercent && charging == remoteCharging) return;
    remoteBatteryPercent = percent;
    remoteCharging = charging;
    if (!pinRequested && (headerExpanded || screen != SCREEN_MAIN)) drawRemoteBattery();
}

// Bar across the top of every page: background, hairline and the app name on the left.
static void drawTitleBar() {
    M5.Lcd.fillRect(0, 0, 320, 28, UI_BAR);
    M5.Lcd.drawFastHLine(0, 27, 320, GREY);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString("MagicPilot", 8, 14, 2);
}

// Title bar of the preset and focus pages.
static void drawSubHeader(const char* title) {
    M5.Lcd.fillScreen(BLACK);
    drawTitleBar();
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(title, 160, 14, 2);
    drawRemoteBattery();
}

// Header: name on the left, camera mode in the middle, link state on the right.
// Tapping it also shows the remote battery and the camera battery; the mode label then sits midway between them.
static int modeCenterX() {
    return headerExpanded ? (coreBatteryRight() + camLabelLeft()) / 2 : 160;
}

// Draws the main page header in its current expanded or collapsed form.
static void drawTopBar() {
    drawTitleBar();
    const char* cameraMode = !cameraConnected ? "STBY" : transportMode == 2 ? "REC" : transportMode == 1 ? "PLAY" : "STBY";
    if (transportMode == 2 && cameraConnected) {
        const int recWidth = M5.Lcd.textWidth("REC", 2);
        const int groupLeft = modeCenterX() - (10 + 6 + recWidth) / 2;
        M5.Lcd.fillCircle(groupLeft + 5, 14, 5, RED);
        M5.Lcd.setTextColor(RED, UI_BAR);
        M5.Lcd.drawString("REC", groupLeft + 16, 14, 2);
    } else {
        M5.Lcd.setTextColor(WHITE, UI_BAR);
        M5.Lcd.setTextDatum(MC_DATUM);
        M5.Lcd.drawString(cameraMode, modeCenterX(), 14, 2);
    }
    const uint16_t linkColor = cameraConnected ? UI_BT_BLUE : GREY;
    drawBluetoothIcon(288, 7, linkColor);
    M5.Lcd.fillCircle(304, 14, 6, linkColor);
    if (!headerExpanded) return;
    drawVoltage();
    drawRemoteBattery();
    M5.Lcd.drawFastVLine(coreBatteryRight() + 5, 0, 27, GREY);
    M5.Lcd.drawFastVLine(camLabelLeft() - 5, 0, 27, GREY);
}

// Grid of two rows: ISO, shutter, FPS above white balance, tint, iris.
static void drawValues() {
    char text[12];
    for (uint8_t cell = 0; cell < 6; ++cell) {
        valueText(cell, text, sizeof(text));
        drawCell(VALUE_CELLS[cell].x, VALUE_CELLS[cell].y, 78, 34, VALUE_CELLS[cell].title, text,
                 selectedValue == cell, cell == VALUE_SHUTTER);
    }
}

// 12x16 card symbol: SD is a filled body with a cut corner, CFast an outline with contacts.
static void drawCardIcon(int x, int y, uint16_t color, uint16_t background, bool sdCard) {
    if (sdCard) {
        M5.Lcd.fillRoundRect(x, y, 12, 16, 2, color);
        M5.Lcd.fillTriangle(x + 7, y, x + 11, y, x + 11, y + 4, background);
        for (int index = 0; index < 3; ++index) M5.Lcd.fillRect(x + 2 + index * 3, y + 6, 2, 6, background);
        return;
    }
    M5.Lcd.drawRoundRect(x, y, 12, 16, 2, color);
    M5.Lcd.fillTriangle(x, y, x + 4, y, x, y + 4, background);
    M5.Lcd.drawLine(x + 4, y, x, y + 4, color);
    for (int index = 0; index < 4; ++index) M5.Lcd.fillRect(x + 2 + index * 3, y + 9, 2, 5, color);
}

static const int SLOT_ROW_Y = 222;  // inside the status bar

// One footer slot: active marker, number, card icon or name, then minutes remaining.
static void drawSlotCell(uint8_t slot, int x0) {
    const int centerY = SLOT_ROW_Y + 8;
    const bool active = cameraConnected && slot < 2 && (transportFlags & (1 << (5 + slot)));
    const uint16_t foreground = active ? WHITE : GREY;
    if (active) M5.Lcd.fillCircle(x0 + 6, centerY, 3, transportMode == 2 ? RED : UI_GREEN);

    char slotNumber[2] = {(char)('1' + slot), '\0'};
    M5.Lcd.setTextColor(foreground, UI_BAR);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString(slotNumber, x0 + 11, centerY, 2);

    const int8_t medium = cameraConnected && slot < 2 ? (int8_t)slotMedium[slot] : -1;
    int cursor = x0 + 22;
    if (medium == 0 || medium == 1) {
        drawCardIcon(cursor, SLOT_ROW_Y, foreground, UI_BAR, medium == 1);
        cursor += 12;
    } else {
        const char* name = medium == 2 ? "SSD" : "--";
        M5.Lcd.drawString(name, cursor, centerY, 1);
        cursor += M5.Lcd.textWidth(name, 1);
    }
    cursor += 4;

    const int32_t seconds = slotRemaining[slot];
    char minutes[8] = "--";
    if (seconds >= 0 && (seconds > 0 || active)) snprintf(minutes, sizeof(minutes), "%ld", (long)(seconds / 60));
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString(minutes, cursor, centerY, 2);
    cursor += M5.Lcd.textWidth(minutes, 2) + 2;
    M5.Lcd.drawString("min", cursor, centerY, 1);
}

// The three media slots, drawn only while the footer is expanded.
static void drawSlots() {
    if (!footerExpanded) return;
    M5.Lcd.fillRect(0, SLOT_ROW_Y, 320, 17, UI_BAR);
    for (uint8_t slot = 0; slot < 3; ++slot) drawSlotCell(slot, 4 + slot * 104);
}

// The - / + buttons for the selected value, and the connect button.
static void drawControls() {
    M5.Lcd.fillRect(STEP_PAD.x, STEP_PAD.y, STEP_PAD.w, STEP_PAD.h, BLACK);
    drawStepperPair(STEP_MINUS, STEP_PLUS, 28, 6);

    M5.Lcd.fillRect(CONNECT_BUTTON.x, CONNECT_BUTTON.y, CONNECT_BUTTON.w, CONNECT_BUTTON.h, BLACK);
    if (cameraConnected) {
        M5.Lcd.drawRoundRect(CONNECT_BUTTON.x, CONNECT_BUTTON.y, CONNECT_BUTTON.w, CONNECT_BUTTON.h, 4, GREY);
        M5.Lcd.setTextColor(GREY, BLACK);
    } else {
        M5.Lcd.fillRoundRect(CONNECT_BUTTON.x, CONNECT_BUTTON.y, CONNECT_BUTTON.w, CONNECT_BUTTON.h, 4, UI_CONNECT);
        M5.Lcd.setTextColor(WHITE, UI_CONNECT);
    }
    M5.Lcd.setTextDatum(MC_DATUM);
    // Short labels so the larger font fits the 70 px wide button; CONNECT only has room for 1 px between letters.
    drawSpacedString(cameraConnected ? "ONLINE" : connecting ? "SEARCH" : "CONNECT",
                     CONNECT_BUTTON.x + CONNECT_BUTTON.w / 2, CONNECT_BUTTON.y + CONNECT_BUTTON.h / 2,
                     2, cameraConnected || connecting ? 2 : 1);
}

// Red dot when idle, white stop square while recording.
static void drawRecordButton() {
    M5.Lcd.fillRect(255, 79, 63, 63, BLACK);
    M5.Lcd.drawCircle(286, 110, 30, WHITE);
    M5.Lcd.drawCircle(286, 110, 29, WHITE);
    M5.Lcd.fillCircle(286, 110, 15, RED);
    // 17 px wide so it has a centre pixel, like the circles.
    if (transportMode == 2) M5.Lcd.fillRoundRect(278, 102, 17, 17, 2, WHITE);
}

// Footer: the status message, with the card slot row below it once expanded.
static void drawStatus() {
    M5.Lcd.fillRect(0, 206, 320, 34, UI_BAR);
    M5.Lcd.drawFastHLine(0, 205, 320, GREY);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(MC_DATUM);
    const uint8_t font = M5.Lcd.textWidth(statusMessage, 2) <= 304 ? 2 : 1;
    M5.Lcd.drawString(statusMessage, 160, footerExpanded ? 214 : 223, font);
    drawSlots();
}

// Redraws the whole main screen.
static void drawScreen() {
    M5.Lcd.fillScreen(BLACK);
    drawnTimecodeColor = BLACK;
    drawTopBar();
    drawTimecode();
    drawValues();
    drawControls();
    drawRecordButton();
    drawOutlineButton(FOCUS_BUTTON, "FOCUS", 0);
    drawOutlineButton(PRESET_BUTTON, "PRESETS", 0);
    drawStatus();
}
