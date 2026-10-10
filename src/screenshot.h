#pragma once

// UI: screenshot capture to the SD card.

#include "config.h"
#include "globals.h"

static const int SCREEN_W = 320, SCREEN_H = 240;  // the Core2 panel, in landscape
static const char* CAPTURE_DIR = "/ui_captures";

// Writes a 32-bit field of the BMP header in little-endian order.
static void writeUint32LE(uint8_t* destination, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) destination[i] = (uint8_t)(value >> (8 * i));
}

// 54-byte BITMAPINFOHEADER for a bottom-up 24-bit image of the whole screen.
static void fillBmpHeader(uint8_t* header) {
    const uint32_t pixelBytes = (uint32_t)SCREEN_W * SCREEN_H * 3;
    header[0] = 'B';
    header[1] = 'M';
    writeUint32LE(header + 2, 54 + pixelBytes);
    writeUint32LE(header + 10, 54);
    writeUint32LE(header + 14, 40);
    writeUint32LE(header + 18, SCREEN_W);
    writeUint32LE(header + 22, SCREEN_H);
    header[26] = 1;
    header[28] = 24;
    writeUint32LE(header + 34, pixelBytes);
}

// First unused capture path, counting on from the last one written.
static void nextCapturePath(char* path, size_t size) {
    do {
        snprintf(path, size, "%s/ui_%05lu.bmp", CAPTURE_DIR, (unsigned long)nextScreenshotIndex++);
    } while (SD.exists(path));
}

// Saves the screen as a numbered BMP, at most once every 1.5 s.
static void writeBmpCapture() {
    if (!CAPTURE_SCREENSHOTS || !sdReady || !screenshotQueued || millis() - lastScreenshotMs < 1500) return;
    screenshotQueued = false;
    char path[48];
    nextCapturePath(path, sizeof(path));
    if (!SD.exists(CAPTURE_DIR)) SD.mkdir(CAPTURE_DIR);
    File file = SD.open(path, FILE_WRITE);
    if (!file) return;

    uint8_t header[54] = {};
    fillBmpHeader(header);
    file.write(header, sizeof(header));

    uint8_t rgbRow[SCREEN_W * 3];
    for (int y = SCREEN_H - 1; y >= 0; --y) {
        M5.Lcd.readRectRGB(0, y, SCREEN_W, 1, rgbRow);
        for (int x = 0; x < SCREEN_W; ++x) {  // the BMP rows are BGR
            const uint8_t red = rgbRow[x * 3];
            rgbRow[x * 3] = rgbRow[x * 3 + 2];
            rgbRow[x * 3 + 2] = red;
        }
        file.write(rgbRow, sizeof(rgbRow));
    }
    file.close();
    lastScreenshotMs = millis();
    Serial.printf("Saved screenshot: %s\n", path);
}
