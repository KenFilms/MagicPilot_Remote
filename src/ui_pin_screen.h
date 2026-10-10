#pragma once

// UI: PIN pairing keypad.

#include "config.h"
#include "globals.h"

// Keypad for the 6-digit pairing PIN that the camera displays.
static const int PIN_KEY_X[3] = {4, 109, 214};  // left edge of each keypad column
static const int PIN_KEY_Y[4] = {30, 83, 136, 189};  // top edge of each keypad row
static const int PIN_KEY_W = 101, PIN_KEY_H = 49;

// Draws the whole keypad, including the six digit boxes at the top.
static void drawPinScreen() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextDatum(MC_DATUM);

    for (uint8_t i = 0; i < 6; ++i) {
        const int x = 18 + i * 48;
        M5.Lcd.drawRoundRect(x, 3, 44, 24, 3, i < pinDigits ? UI_SEL : GREY);
        char digit[2] = {'-', '\0'};
        if (i < pinDigits) digit[0] = pinEntry[i];
        M5.Lcd.setTextColor(WHITE, BLACK);
        M5.Lcd.drawString(digit, x + 22, 15, 2);
    }

    const char* labels[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"DEL", "0", "OK"}};
    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t column = 0; column < 3; ++column) {
            const int x = PIN_KEY_X[column];
            const int y = PIN_KEY_Y[row];
            const bool confirmButton = row == 3 && column == 2;
            const uint16_t buttonColor = confirmButton ? (pinDigits == 6 ? UI_GREEN : DARK_GREY) : GREY;
            const uint16_t buttonFill = confirmButton && pinDigits == 6 ? UI_CONNECT : UI_BAR;
            M5.Lcd.fillRoundRect(x, y, PIN_KEY_W, PIN_KEY_H, 6, buttonFill);
            M5.Lcd.drawRoundRect(x, y, PIN_KEY_W, PIN_KEY_H, 6, buttonColor);
            M5.Lcd.setTextColor(WHITE, buttonFill);
            M5.Lcd.drawString(labels[row][column], x + PIN_KEY_W / 2, y + PIN_KEY_H / 2, 4);
        }
    }
    M5.Lcd.setTextDatum(TL_DATUM);
    pinScreenDrawn = true;
}

// Adds digits or deletes, and submits the PIN once all 6 digits are entered.
static void handlePinTouch(int x, int y) {
    int column = -1, row = -1;
    for (int index = 0; index < 3; ++index) {
        if (x >= PIN_KEY_X[index] && x < PIN_KEY_X[index] + PIN_KEY_W) column = index;
    }
    for (int index = 0; index < 4; ++index) {
        if (y >= PIN_KEY_Y[index] && y < PIN_KEY_Y[index] + PIN_KEY_H) row = index;
    }
    if (row < 0 || column < 0) return;

    if (row == 3 && column == 2) {
        if (pinDigits == 6) {
            pinSubmitted = true;
            pinScreenDrawn = false;
        }
        return;
    }
    if (row == 3 && column == 0) {
        if (pinDigits > 0) pinEntry[--pinDigits] = '\0';
    } else if (pinDigits < 6) {
        // Digit keys: 1-9 on rows 0-2, "0" on row 3 column 1.
        pinEntry[pinDigits++] = row == 3 ? '0' : (char)('0' + row * 3 + column + 1);
    } else {
        return;
    }
    drawPinScreen();
}
