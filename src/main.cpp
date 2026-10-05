/*
 * MagicPilot Remote
 *
 * Author: Ken Friedl https://github.com/KenFilms
 * First Version:   2026-10-04
 * 
 * Current version: 1.0 Beta
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
 */

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

static const char* CAMERA_SERVICE_UUID = "291D567A-6D75-11E6-8B77-86F30CA893D3";
static const char* OUTGOING_UUID = "5DD3465F-1AEE-4299-8493-D2ECA2F8E1BB";
static const char* INCOMING_UUID = "B864E140-76A0-416A-BF30-5876504537D9";
static const char* TIMECODE_UUID = "6D8F2110-86F1-41BF-9AFB-451D87E976C8";
static const char* STATUS_UUID = "7FE8691D-95DC-4FC5-8ABD-CA74339B51B9";
static const char* DEVICE_NAME_UUID = "FFAC0C52-C9FB-41A0-B063-CC76282EB89C";

static const uint16_t GREY = 0x8410;
static const uint16_t DARK_GREY = 0x4208;
static const uint16_t ACCENT_BLUE = 0x051F;
static const uint16_t UI_BAR = 0x1082;
static const uint16_t UI_GREEN = 0x07E0;
static const uint16_t UI_BT_BLUE = 0x2DFF;
static const uint16_t UI_CONNECT = 0x1A91;
static const uint16_t UI_SEL = 0xFD20;

// Cells of the value grid, in on-screen order.
enum CameraValue : uint8_t { VALUE_ISO, VALUE_SHUTTER, VALUE_FPS, VALUE_WB, VALUE_TINT, VALUE_IRIS };

// Recording format as reported by the camera (category 1, parameter 9).
struct RecordingFormat {
    int16_t fileFps;
    int16_t sensorFps;
    int16_t width;
    int16_t height;
    int16_t flags;
};

// Remaining recording time and card type of one media slot.
struct SlotState {
    int32_t remainingSeconds;
    uint8_t medium;
};

static esp_gatt_if_t gattInterface = ESP_GATT_IF_NONE;
static uint16_t gattConnectionId = 0;
static uint16_t serviceStartHandle = 0;
static uint16_t serviceEndHandle = 0;
static uint16_t outgoingHandle = 0;
static uint16_t incomingHandle = 0;
static uint16_t timecodeHandle = 0;
static uint16_t statusHandle = 0;
static uint16_t deviceNameHandle = 0;
static uint16_t pendingDescriptorHandle = 0;
static uint16_t notificationHandles[3] = {0, 0, 0};
static esp_gatt_char_prop_t notificationProperties[3] = {0, 0, 0};
static uint8_t notificationIndex = 0;
static esp_bd_addr_t cameraBda = {};
static esp_bd_addr_t pairingBda = {};
static esp_ble_addr_type_t cameraAddressType = BLE_ADDR_TYPE_PUBLIC;
static esp_ble_scan_params_t scanParameters = {};
static volatile bool cameraConnected = false;
static volatile bool connecting = false;
static volatile bool bleStackReady = false;
static volatile bool scanParametersReady = false;
static volatile bool cameraFound = false;
static volatile bool gattConnectionOpen = false;
static volatile bool securityReady = false;
static volatile bool serviceSearchStarted = false;
static volatile bool passkeyReplyPending = false;
static volatile bool connectionStateDirty = true;
static volatile bool pinRequested = false;
static volatile bool pinReady = false;
static volatile uint32_t enteredPin = 0;
static volatile bool isoDirty = false;
static volatile bool shutterDirty = false;
static volatile bool formatDirty = false;
static volatile bool whiteBalanceDirty = false;
static volatile bool irisDirty = false;
static volatile bool transportDirty = false;
static volatile bool statusDirty = false;
static volatile bool timecodeDirty = false;
static volatile bool sourceDirty = false;
static volatile int32_t cameraIso = 0;
static volatile int32_t cameraShutter = 0;  // degrees * 100
static volatile int16_t cameraWhiteBalance = 0;
static volatile int16_t cameraTint = 0;
static volatile int16_t cameraIris = INT16_MIN;  // fixed16 aperture value (raw / 2048); INT16_MIN = unknown
static volatile RecordingFormat recordingFormat = {0, 0, 0, 0, 0};
static volatile uint8_t transportMode = 0;
static volatile uint8_t transportFlags = 0;  // bit 5 = slot 1 active, bit 6 = slot 2 active
static volatile uint8_t slotMedium[2] = {0, 0};
static volatile int32_t cameraVoltage = 0;
static volatile SlotState slots[3] = {{-1, 0}, {-1, 0}, {-1, 0}};
static volatile uint8_t timecodeBytes[4] = {0, 0, 0, 0};
static volatile uint8_t timecodeSource = 0;  // 0 = timecode, 1 = clip counter (observed on the camera)
static volatile bool haveIso = false;
static volatile bool haveShutter = false;
static volatile bool haveWhiteBalance = false;
static volatile bool haveIris = false;
static volatile bool haveRecordingFormat = false;
static volatile bool haveTransport = false;
static volatile bool haveVoltage = false;
static volatile bool haveTimecode = false;
static volatile int16_t cameraFocus = 0;  // fixed16, 0 (near) to 2048 (far)
static volatile bool haveFocus = false;
static volatile bool focusDirty = false;

static bool sdReady = false;
static bool screenshotQueued = false;
static bool pinScreenDrawn = false;
static bool headerExpanded = false;  // main header shows battery details only after a tap
static bool footerExpanded = false;  // footer shows the card slots only after a tap
static bool presetScreenOpen = false;
static bool focusScreenOpen = false;
static bool touchWasDown = false;
static bool lastConnectionState = false;
static uint32_t nextScreenshotIndex = 0;
static uint32_t lastScreenshotMs = 0;
static uint32_t lastStepMs = 0;
static uint32_t pairingStartMs = 0;
static uint32_t pendingTimecodeCount = 0;
static uint8_t clipHeldBytes[4] = {};  // last clip counter (BCD), kept after recording stops
static bool clipLive = false;          // a timecode packet has arrived since the last transport change
static bool pendingTimecode = false;
static uint8_t selectedValue = VALUE_ISO;
static uint8_t pinDigits = 0;
static char pinEntry[6] = {};
static String cameraAddress;
static char cameraName[32] = "";  // name the camera advertised when it was found
static char statusMessage[44] = "Camera not found (pairing mode?)";

static const int32_t ISO_STEPS[] = {100, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800, 16000, 20000, 25600};
// Shutter angles in camera units (degrees * 100), so 11.2 degrees is 1120.
static const int32_t SHUTTER_STEPS[] = {1120, 1500, 2250, 3000, 3750, 4500, 6000, 7200, 7500, 9000, 10800, 12000, 14400, 15000, 17280, 18000, 21600, 27000, 32400, 36000};
static const int WB_MIN = 2500, WB_MAX = 10000, WB_STEP = 50;  // kelvin
static const int16_t FPS_STEPS[] = {5, 6, 8, 10, 12, 15, 18, 20, 23, 24, 25, 30, 48, 50, 60, 72, 90, 100, 120};

// Packet layout: dest, length, command, reserved, category, parameter, type, operation, then the value padded to 4 bytes.
static const uint8_t ISO_PACKET[] = {0xFF, 0x08, 0x00, 0x00, 0x01, 0x0E, 0x03, 0x00, 0, 0, 0, 0};
static const uint8_t SHUTTER_PACKET[] = {0xFF, 0x08, 0x00, 0x00, 0x01, 0x0B, 0x03, 0x00, 0, 0, 0, 0};
static const uint8_t FRAME_RATE_PACKET[] = {0xFF, 0x0E, 0x00, 0x00, 0x01, 0x09, 0x02, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t WHITE_BALANCE_PACKET[] = {0xFF, 0x08, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0, 0, 0, 0};
// Aperture ordinal with the offset operation: steps one stop in the lens's list. Setting the f-stop directly is ignored until the iris has been changed on the camera.
static const uint8_t IRIS_STEP_PACKET[] = {0xFF, 0x06, 0x00, 0x00, 0x00, 0x04, 0x02, 0x01, 0, 0, 0, 0};
static const uint8_t TRANSPORT_PACKET[] = {0xFF, 0x05, 0x00, 0x00, 0x0A, 0x01, 0x01, 0x00, 0, 0, 0, 0};
static const uint8_t TIMECODE_SOURCE_PACKET[] = {0xFF, 0x05, 0x00, 0x00, 0x04, 0x07, 0x01, 0x00, 0, 0, 0, 0};
static const uint8_t FOCUS_PACKET[] = {0xFF, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0, 0, 0, 0};
static const uint8_t AUTOFOCUS_PACKET[] = {0xFF, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};

static int32_t readInt32(const uint8_t* data) {
    return (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
}

static int16_t readInt16(const uint8_t* data) {
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

// Updates the status text; the main loop redraws it.
static void setStatus(const char* message) {
    snprintf(statusMessage, sizeof(statusMessage), "%s", message);
    statusDirty = true;
}

// Queues a BMP capture of the screen for the SD card.
static void requestScreenshot() {
    if (!CAPTURE_SCREENSHOTS) return;
    screenshotQueued = true;
}

// Packet length with the value padded to a 4-byte boundary.
static size_t alignedPacketLength(uint8_t dataLength) {
    return 4 + ((dataLength + 3) & ~((size_t)3));
}

// True if a control packet header (destination 0xFF, command 0) starts at offset.
static bool startsControlPacket(const uint8_t* bytes, size_t length, size_t offset) {
    return offset + 4 <= length && bytes[offset] == 0xFF && bytes[offset + 2] == 0;
}

// Writes a packet to the camera's outgoing control characteristic.
static bool writeCameraPacket(const uint8_t* packet, size_t packetLength) {
    if (!cameraConnected || outgoingHandle == 0 || gattInterface == ESP_GATT_IF_NONE) return false;
    return esp_ble_gattc_write_char(gattInterface, gattConnectionId, outgoingHandle,
                                    (uint16_t)packetLength, (uint8_t*)packet,
                                    ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_MITM) == ESP_OK;
}

static void writeInt16LE(uint8_t* destination, int16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

// Fills the 32-bit value of a packet template and sends it.
static bool sendInt32Packet(const uint8_t* packetTemplate, int32_t value) {
    uint8_t packet[sizeof(ISO_PACKET)];
    memcpy(packet, packetTemplate, sizeof(packet));
    packet[8] = (uint8_t)value;
    packet[9] = (uint8_t)(value >> 8);
    packet[10] = (uint8_t)(value >> 16);
    packet[11] = (uint8_t)(value >> 24);
    return writeCameraPacket(packet, sizeof(packet));
}

// Fills the 8-bit value of a packet template and sends it.
static bool sendInt8Packet(const uint8_t* packetTemplate, uint8_t value) {
    uint8_t packet[sizeof(TRANSPORT_PACKET)];
    memcpy(packet, packetTemplate, sizeof(packet));
    packet[8] = value;
    return writeCameraPacket(packet, sizeof(packet));
}

// Changes only the frame rate; the camera needs the other format fields sent back unchanged.
static bool sendRecordingFormat(int16_t fps) {
    if (!haveRecordingFormat) return false;
    uint8_t packet[sizeof(FRAME_RATE_PACKET)];
    memcpy(packet, FRAME_RATE_PACKET, sizeof(packet));
    writeInt16LE(packet + 8, fps);
    writeInt16LE(packet + 10, 0);
    writeInt16LE(packet + 12, recordingFormat.width);
    writeInt16LE(packet + 14, recordingFormat.height);
    writeInt16LE(packet + 16, recordingFormat.flags);
    return writeCameraPacket(packet, sizeof(packet));
}

// Also updates the local values so the UI reacts before the camera confirms.
static bool sendWhiteBalance(int16_t kelvin, int16_t tint) {
    uint8_t packet[sizeof(WHITE_BALANCE_PACKET)];
    memcpy(packet, WHITE_BALANCE_PACKET, sizeof(packet));
    writeInt16LE(packet + 8, kelvin);
    writeInt16LE(packet + 10, tint);
    cameraWhiteBalance = kelvin;
    cameraTint = tint;
    haveWhiteBalance = true;
    whiteBalanceDirty = true;
    return writeCameraPacket(packet, sizeof(packet));
}

// Steps the iris one stop wider (-1) or narrower (+1).
static bool sendIrisStep(int direction) {
    uint8_t packet[sizeof(IRIS_STEP_PACKET)];
    memcpy(packet, IRIS_STEP_PACKET, sizeof(packet));
    writeInt16LE(packet + 8, (int16_t)direction);
    return writeCameraPacket(packet, sizeof(packet));
}

// Clears the cached camera state and marks every display element for redraw.
static void markDisconnected() {
    cameraConnected = false;
    connecting = false;
    pinRequested = false;
    pinReady = false;
    passkeyReplyPending = false;
    pinScreenDrawn = false;
    gattConnectionId = 0;
    serviceStartHandle = serviceEndHandle = 0;
    outgoingHandle = incomingHandle = timecodeHandle = statusHandle = deviceNameHandle = 0;
    pendingDescriptorHandle = 0;
    notificationIndex = 0;
    notificationHandles[0] = notificationHandles[1] = notificationHandles[2] = 0;
    notificationProperties[0] = notificationProperties[1] = notificationProperties[2] = 0;
    gattConnectionOpen = false;
    securityReady = false;
    serviceSearchStarted = false;
    haveIso = false;
    haveShutter = false;
    haveWhiteBalance = false;
    haveIris = false;
    haveRecordingFormat = false;
    haveTransport = false;
    haveVoltage = false;
    haveTimecode = false;
    haveFocus = false;
    focusDirty = true;
    isoDirty = shutterDirty = formatDirty = whiteBalanceDirty = irisDirty = true;
    transportDirty = statusDirty = timecodeDirty = true;
    connectionStateDirty = true;
}

// True if both nibbles are decimal digits and the value is below limit.
static bool validBcd(uint8_t value, uint8_t limit) {
    const uint8_t high = value >> 4;
    const uint8_t low = value & 0x0F;
    return high <= 9 && low <= 9 && high * 10 + low < limit;
}

static uint8_t bcdToInt(uint8_t value) {
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

// Camera clock as last reported; secondsOfDay is only meaningful when haveTime is set.
struct CameraClock {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint32_t secondsOfDay;
    bool haveTime;
    uint32_t receivedMs;
    bool valid;
};
static CameraClock cameraClock = {};

// Decodes a BCD date such as 0x20261005.
static bool decodeBcdDate(uint32_t value, uint16_t& year, uint8_t& month, uint8_t& day) {
    const uint8_t century = value >> 24, yearBcd = value >> 16, monthBcd = value >> 8, dayBcd = value;
    if (century != 0x20 || !validBcd(yearBcd, 100) || !validBcd(monthBcd, 13) || monthBcd == 0 || !validBcd(dayBcd, 32) || dayBcd == 0) return false;
    year = 2000 + bcdToInt(yearBcd);
    month = bcdToInt(monthBcd);
    day = bcdToInt(dayBcd);
    return true;
}

// Decodes a BCD time laid out as 0x00HHMMSS or 0xHHMMSS00 into seconds since midnight.
static bool decodeBcdTime(uint32_t value, uint32_t& seconds) {
    const uint8_t b3 = value >> 24, b2 = value >> 16, b1 = value >> 8, b0 = value;
    uint8_t hours, minutes, secs;
    if (b3 == 0 && validBcd(b2, 24) && validBcd(b1, 60) && validBcd(b0, 60)) { hours = b2; minutes = b1; secs = b0; }
    else if (b0 == 0 && validBcd(b3, 24) && validBcd(b2, 60) && validBcd(b1, 60)) { hours = b3; minutes = b2; secs = b1; }
    else return false;
    seconds = bcdToInt(hours) * 3600UL + bcdToInt(minutes) * 60UL + bcdToInt(secs);
    return true;
}

static uint8_t daysInMonth(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) return 29;
    return days[month - 1];
}

// The camera's current date (YYYY-MM-DD) and time (HH:MM:SS), advanced by the time since it was reported.
// clockTime is empty when the camera didn't report a time; returns false when it reported no date.
static bool cameraDateTime(char* clockDate, char* clockTime) {
    const CameraClock clock = cameraClock;
    clockDate[0] = '\0';
    clockTime[0] = '\0';
    if (!clock.valid) return false;
    uint16_t year = clock.year;
    uint8_t month = clock.month, day = clock.day;
    if (clock.haveTime) {
        uint32_t seconds = clock.secondsOfDay + (millis() - clock.receivedMs) / 1000;
        for (uint32_t days = seconds / 86400; days > 0; --days) {
            if (++day > daysInMonth(year, month)) {
                day = 1;
                if (++month > 12) { month = 1; ++year; }
            }
        }
        seconds %= 86400;
        snprintf(clockTime, 9, "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600), (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
    }
    snprintf(clockDate, 11, "%04u-%02u-%02u", (unsigned)year, (unsigned)month, (unsigned)day);
    return true;
}

// Converts a BCD timecode to a frame count, assuming 30 fps while the rate is unknown.
static uint32_t timecodeFrameCount(const uint8_t* bytes) {
    const uint32_t fps = recordingFormat.fileFps > 0 && recordingFormat.fileFps <= 60 ? recordingFormat.fileFps : 30;
    const uint32_t hours = bcdToInt(bytes[3]);
    const uint32_t minutes = bcdToInt(bytes[2]);
    const uint32_t seconds = bcdToInt(bytes[1]);
    const uint32_t frames = bcdToInt(bytes[0]);
    return (((hours * 60 + minutes) * 60 + seconds) * fps) + frames;
}

// Stores a new timecode after rejecting invalid values and one-off jumps.
static void setTimecode(const uint8_t* bytes) {
    if (!validBcd(bytes[0], 60) || !validBcd(bytes[1], 60) || !validBcd(bytes[2], 60) || !validBcd(bytes[3], 24)) return;
    const uint32_t frameCount = timecodeFrameCount(bytes);
    if (haveTimecode) {
        const uint32_t previous = timecodeFrameCount((const uint8_t*)timecodeBytes);
        const uint32_t fps = recordingFormat.fileFps > 0 && recordingFormat.fileFps <= 60 ? recordingFormat.fileFps : 30;
        // Ignore a jump of more than 3 s unless a packet a few frames later confirms it (packets arrive every ~2 frames).
        if (frameCount > previous + fps * 3 || (previous > frameCount + fps * 3)) {
            if (!pendingTimecode || frameCount <= pendingTimecodeCount || frameCount > pendingTimecodeCount + 4) {
                pendingTimecode = true;
                pendingTimecodeCount = frameCount;
                return;
            }
            pendingTimecode = false;
        } else {
            pendingTimecode = false;
        }
    }
    for (uint8_t i = 0; i < 4; ++i) timecodeBytes[i] = bytes[i];
    haveTimecode = true;
    timecodeDirty = true;
    if (transportMode == 2) clipLive = true;
}

// Decodes camera notifications and updates the cached camera state.
static void parseControlPackets(const uint8_t* bytes, size_t length) {
    size_t offset = 0;
    while (offset + 4 <= length) {
        const uint8_t* packet = bytes + offset;
        const uint8_t payloadLength = packet[1];
        const size_t rawPacketLength = 4 + payloadLength;
        const size_t paddedPacketLength = alignedPacketLength(payloadLength);
        const size_t remainingLength = length - offset;
        if (rawPacketLength > remainingLength || packet[2] != 0) break;

        // The camera may or may not pad packets to 4 bytes, so decide by where the next packet starts.
        size_t packetLength = rawPacketLength;
        if (paddedPacketLength > rawPacketLength && paddedPacketLength <= remainingLength) {
            const bool rawNext = startsControlPacket(bytes, length, offset + rawPacketLength);
            const bool paddedNext = startsControlPacket(bytes, length, offset + paddedPacketLength);
            if (paddedNext && !rawNext) packetLength = paddedPacketLength;
            else if (remainingLength == paddedPacketLength) packetLength = paddedPacketLength;
        }
        if (payloadLength >= 4) {
            const uint8_t category = packet[4];
            const uint8_t parameter = packet[5];
            const uint8_t type = packet[6];
            const uint8_t* data = packet + 8;
            const size_t valueLength = payloadLength - 4;
            if (category == 1 && parameter == 14 && type == 3 && valueLength >= 4) {
                cameraIso = readInt32(data);
                haveIso = true;
                isoDirty = true;
            } else if (category == 1 && parameter == 11 && type == 3 && valueLength >= 4) {
                cameraShutter = readInt32(data);
                haveShutter = true;
                shutterDirty = true;
            } else if (category == 1 && parameter == 2 && type == 2 && valueLength >= 4) {
                cameraWhiteBalance = readInt16(data);
                cameraTint = readInt16(data + 2);
                haveWhiteBalance = true;
                whiteBalanceDirty = true;
            } else if (category == 1 && parameter == 9 && type == 2 && valueLength >= 10) {
                recordingFormat.fileFps = readInt16(data);
                recordingFormat.sensorFps = readInt16(data + 2);
                recordingFormat.width = readInt16(data + 4);
                recordingFormat.height = readInt16(data + 6);
                recordingFormat.flags = readInt16(data + 8);
                haveRecordingFormat = true;
                formatDirty = true;
            } else if (category == 0 && parameter == 2 && type == 128 && valueLength >= 2) {
                cameraIris = readInt16(data);
                haveIris = cameraIris != INT16_MIN;
                irisDirty = true;
            } else if (category == 0 && parameter == 0 && type == 128 && valueLength >= 2) {
                cameraFocus = constrain(readInt16(data), 0, 2048);
                haveFocus = true;
                focusDirty = true;
            } else if (category == 10 && parameter == 1 && valueLength >= 1) {
                const uint8_t previousMode = transportMode;
                transportMode = data[0];
                // The clip counter is only valid once a timecode packet arrives after the change.
                if (transportMode != previousMode) clipLive = false;
                if (valueLength >= 5) {
                    transportFlags = data[2];
                    slotMedium[0] = data[3];
                    slotMedium[1] = data[4];
                }
                haveTransport = true;
                transportDirty = true;
            } else if (category == 9 && parameter == 0 && type == 2 && valueLength >= 2) {
                cameraVoltage = readInt16(data);
                haveVoltage = true;
                statusDirty = true;
            } else if (category == 9 && parameter == 2 && type == 2) {
                for (uint8_t i = 0; i < 3 && (size_t)(i + 1) * 2 <= valueLength; ++i) slots[i].remainingSeconds = readInt16(data + i * 2);
                statusDirty = true;
            } else if (category == 4 && parameter == 7 && valueLength >= 1) {
                timecodeSource = data[0];
                sourceDirty = true;
            } else if (category == 9 && parameter == 4 && valueLength >= 4) {
                setTimecode(data);
            } else if (category == 7 && parameter == 0 && valueLength >= 8) {
                // Real time clock: one BCD value is the time and the other the date, so accept either order.
                const uint32_t first = (uint32_t)readInt32(data), second = (uint32_t)readInt32(data + 4);
                Serial.printf("[BLE] camera clock %08lX %08lX\n", (unsigned long)first, (unsigned long)second);
                CameraClock clock = {};
                const bool dateIsSecond = decodeBcdDate(second, clock.year, clock.month, clock.day);
                if (dateIsSecond || decodeBcdDate(first, clock.year, clock.month, clock.day)) {
                    clock.haveTime = decodeBcdTime(dateIsSecond ? first : second, clock.secondsOfDay);
                    clock.receivedMs = millis();
                    clock.valid = true;
                    cameraClock = clock;
                }
            }
        }
        offset += packetLength;
    }
}

static int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

// Converts a UUID string to the reversed byte order Bluedroid uses; len stays 0 if it is malformed.
static esp_bt_uuid_t uuid128FromString(const char* text) {
    esp_bt_uuid_t uuid = {};
    uint8_t bytes[16] = {};
    size_t byteCount = 0;
    for (size_t index = 0; text[index] != '\0' && byteCount < sizeof(bytes);) {
        if (text[index] == '-') { ++index; continue; }
        const int high = hexNibble(text[index]);
        const int low = hexNibble(text[index + 1]);
        if (high < 0 || low < 0) return uuid;
        bytes[byteCount++] = (uint8_t)((high << 4) | low);
        index += 2;
    }
    if (byteCount != sizeof(bytes)) return uuid;
    uuid.len = ESP_UUID_LEN_128;
    for (size_t index = 0; index < sizeof(bytes); ++index) uuid.uuid.uuid128[index] = bytes[15 - index];
    return uuid;
}

static bool uuidMatches(const esp_bt_uuid_t& candidate, const char* text) {
    const esp_bt_uuid_t expected = uuid128FromString(text);
    return candidate.len == ESP_UUID_LEN_128 && expected.len == ESP_UUID_LEN_128 &&
           memcmp(candidate.uuid.uuid128, expected.uuid.uuid128, ESP_UUID_LEN_128) == 0;
}

static bool advertisementHasCameraService(uint8_t* advertisement) {
    uint8_t length = 0;
    uint8_t* uuids = esp_ble_resolve_adv_data(advertisement, ESP_BLE_AD_TYPE_128SRV_CMPL, &length);
    if (uuids == nullptr || length < ESP_UUID_LEN_128) return false;
    const esp_bt_uuid_t service = uuid128FromString(CAMERA_SERVICE_UUID);
    for (uint8_t offset = 0; offset + ESP_UUID_LEN_128 <= length; offset += ESP_UUID_LEN_128) {
        if (memcmp(uuids + offset, service.uuid.uuid128, ESP_UUID_LEN_128) == 0) return true;
    }
    return false;
}

// Copies the advertised device name (complete or shortened) into name; empty if there is none.
static void advertisedName(uint8_t* advertisement, char* name, size_t size) {
    uint8_t nameLength = 0;
    uint8_t* nameData = esp_ble_resolve_adv_data(advertisement, ESP_BLE_AD_TYPE_NAME_CMPL, &nameLength);
    if (nameData == nullptr) nameData = esp_ble_resolve_adv_data(advertisement, ESP_BLE_AD_TYPE_NAME_SHORT, &nameLength);
    memset(name, 0, size);
    if (nameData != nullptr) memcpy(name, nameData, nameLength < size - 1 ? nameLength : size - 1);
}

// A camera advertises its name, the camera service UUID, or both.
static bool isCameraAdvertisement(uint8_t* advertisement) {
    char name[64];
    advertisedName(advertisement, name, sizeof(name));
    return strstr(name, "Blackmagic") != nullptr || strstr(name, "BMPCC") != nullptr ||
           advertisementHasCameraService(advertisement);
}

// Starts the 8 second scan once the scan parameters have been accepted.
static void startCameraScan() {
    if (!connecting || !scanParametersReady) return;
    Serial.println("[BLE] scanning for Blackmagic camera (8 seconds)");
    if (esp_ble_gap_start_scanning(8) != ESP_OK) {
        connecting = false;
        setStatus("Scan failed");
    }
}

// Looks up a characteristic of the camera service; returns 0 if it is missing.
static uint16_t findCharacteristicHandle(const char* uuidText, esp_gatt_char_prop_t* properties) {
    esp_gattc_char_elem_t characteristic = {};
    uint16_t count = 1;
    const esp_bt_uuid_t uuid = uuid128FromString(uuidText);
    if (uuid.len != ESP_UUID_LEN_128 ||
        esp_ble_gattc_get_char_by_uuid(gattInterface, gattConnectionId, serviceStartHandle, serviceEndHandle,
                                       uuid, &characteristic, &count) != ESP_GATT_OK || count == 0) return 0;
    if (properties != nullptr) *properties = characteristic.properties;
    return characteristic.char_handle;
}

static void registerNextNotification();

// Stores the characteristic handles, marks the camera connected, and starts enabling notifications.
static void discoverCameraCharacteristics() {
    esp_gatt_char_prop_t outgoingProperties = 0;
    incomingHandle = findCharacteristicHandle(INCOMING_UUID, &notificationProperties[0]);
    timecodeHandle = findCharacteristicHandle(TIMECODE_UUID, &notificationProperties[1]);
    statusHandle = findCharacteristicHandle(STATUS_UUID, &notificationProperties[2]);
    outgoingHandle = findCharacteristicHandle(OUTGOING_UUID, &outgoingProperties);
    deviceNameHandle = findCharacteristicHandle(DEVICE_NAME_UUID, nullptr);
    notificationHandles[0] = incomingHandle;
    notificationHandles[1] = timecodeHandle;
    notificationHandles[2] = statusHandle;

    Serial.printf("[BLE] characteristics: control-out=%d control-in=%d timecode=%d status=%d name=%d\n",
                  outgoingHandle != 0, incomingHandle != 0, timecodeHandle != 0, statusHandle != 0, deviceNameHandle != 0);
    Serial.printf("[BLE] properties: control-in=%02X timecode=%02X status=%02X\n",
                  notificationProperties[0], notificationProperties[1], notificationProperties[2]);
    if (deviceNameHandle != 0) {
        uint8_t deviceName[] = "MagicPilot Remote";
        esp_ble_gattc_write_char(gattInterface, gattConnectionId, deviceNameHandle, sizeof(deviceName) - 1,
                                 deviceName, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_MITM);
    }
    cameraConnected = outgoingHandle != 0 && (outgoingProperties & ESP_GATT_CHAR_PROP_BIT_WRITE) != 0;
    connecting = false;
    connectionStateDirty = true;
    setStatus(cameraConnected ? "Standby" : "Control unavailable");
    notificationIndex = 0;
    registerNextNotification();
}

// Enables notifications one characteristic at a time; each step continues from its GATT event.
static void registerNextNotification() {
    while (notificationIndex < 3) {
        const uint16_t handle = notificationHandles[notificationIndex];
        const uint8_t properties = notificationProperties[notificationIndex];
        if (handle == 0 || (properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE)) == 0) {
            ++notificationIndex;
            continue;
        }
        if (esp_ble_gattc_register_for_notify(gattInterface, cameraBda, handle) == ESP_OK) return;
        Serial.printf("[BLE] notification registration failed for handle %u\n", handle);
        ++notificationIndex;
    }
    Serial.println("[BLE] notification setup complete");
}

// The service search needs both the open connection and finished pairing.
static void startServiceSearchIfReady() {
    if (!gattConnectionOpen || !securityReady || serviceSearchStarted) return;
    esp_bt_uuid_t service = uuid128FromString(CAMERA_SERVICE_UUID);
    serviceSearchStarted = true;
    if (esp_ble_gattc_search_service(gattInterface, gattConnectionId, &service) != ESP_OK) {
        serviceSearchStarted = false;
        connecting = false;
        setStatus("Service search failed");
    }
}

// Handles scan results and pairing (security request, PIN request, authentication result).
static void gapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* parameter) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            scanParametersReady = parameter->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS;
            if (scanParametersReady) startCameraScan();
            else { connecting = false; setStatus("Scan setup failed"); }
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            if (parameter->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT && connecting && !cameraFound &&
                isCameraAdvertisement(parameter->scan_rst.ble_adv)) {
                cameraFound = true;
                cameraAddressType = parameter->scan_rst.ble_addr_type;
                memcpy(cameraBda, parameter->scan_rst.bda, sizeof(cameraBda));
                char address[18];
                snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                         cameraBda[0], cameraBda[1], cameraBda[2], cameraBda[3], cameraBda[4], cameraBda[5]);
                cameraAddress = address;
                advertisedName(parameter->scan_rst.ble_adv, cameraName, sizeof(cameraName));
                Serial.printf("[BLE] selected camera %s \"%s\"\n", address, cameraName);
                setStatus("Camera found, connecting...");
                esp_ble_gap_stop_scanning();
                if (esp_ble_gattc_open(gattInterface, cameraBda, cameraAddressType, true) != ESP_OK) {
                    connecting = false;
                    setStatus("Connection failed");
                }
            } else if (parameter->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT && connecting && !cameraFound) {
                Serial.println("[BLE] scan ended without a camera; check camera pairing mode and advertising");
                connecting = false;
                setStatus("Camera not found (pairing mode?)");
            }
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            Serial.println("[BLE] security request from camera");
            esp_ble_gap_security_rsp(parameter->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_PASSKEY_REQ_EVT:
            pairingStartMs = millis();
            Serial.println("[BLE] passkey requested; enter the camera PIN on the keypad");
            memcpy(pairingBda, parameter->ble_security.ble_req.bd_addr, sizeof(pairingBda));
            pinDigits = 0;
            memset(pinEntry, 0, sizeof(pinEntry));
            pinReady = false;
            passkeyReplyPending = true;
            pinRequested = true;
            setStatus("Enter camera PIN");
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (parameter->ble_security.auth_cmpl.success) {
                Serial.println("[BLE] encrypted pairing complete");
                securityReady = true;
                startServiceSearchIfReady();
            } else {
                Serial.printf("[BLE] authentication failed: %u, %lu ms after the passkey request\n",
                              parameter->ble_security.auth_cmpl.fail_reason, (unsigned long)(millis() - pairingStartMs));
                connecting = false;
                pinRequested = false;
                passkeyReplyPending = false;
                setStatus("Pairing failed, retry");
                esp_ble_gap_disconnect(parameter->ble_security.auth_cmpl.bd_addr);
            }
            break;
        default:
            break;
    }
}

// Drives the connection: open, pair, find the service, enable notifications, then receive data.
static void gattClientEventHandler(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t* parameter) {
    if (event == ESP_GATTC_REG_EVT) {
        if (parameter->reg.status != ESP_GATT_OK) {
            setStatus("GATT client registration failed");
            return;
        }
        gattInterface = interface;
        scanParameters.scan_type = BLE_SCAN_TYPE_ACTIVE;
        scanParameters.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        scanParameters.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
        scanParameters.scan_interval = 0x50;
        scanParameters.scan_window = 0x30;
        scanParameters.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
        if (esp_ble_gap_set_scan_params(&scanParameters) != ESP_OK) setStatus("Scan setup failed");
        return;
    }
    if (interface != gattInterface) return;

    switch (event) {
        case ESP_GATTC_CONNECT_EVT:
            gattConnectionId = parameter->connect.conn_id;
            memcpy(cameraBda, parameter->connect.remote_bda, sizeof(cameraBda));
            Serial.println("[BLE] GATT connected; requesting encrypted pairing");
            if (esp_ble_set_encryption(cameraBda, ESP_BLE_SEC_ENCRYPT_MITM) != ESP_OK) {
                connecting = false;
                setStatus("Pairing failed, retry");
            }
            break;
        case ESP_GATTC_OPEN_EVT:
            if (parameter->open.status != ESP_GATT_OK) {
                connecting = false;
                setStatus("Connection failed");
                break;
            }
            gattConnectionId = parameter->open.conn_id;
            gattConnectionOpen = true;
            startServiceSearchIfReady();
            break;
        case ESP_GATTC_SEARCH_RES_EVT:
            if (uuidMatches(parameter->search_res.srvc_id.uuid, CAMERA_SERVICE_UUID)) {
                serviceStartHandle = parameter->search_res.start_handle;
                serviceEndHandle = parameter->search_res.end_handle;
            }
            break;
        case ESP_GATTC_SEARCH_CMPL_EVT:
            if (parameter->search_cmpl.status != ESP_GATT_OK || serviceStartHandle == 0) {
                connecting = false;
                setStatus("Camera service unavailable");
                break;
            }
            discoverCameraCharacteristics();
            break;
        case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
            if (parameter->reg_for_notify.status != ESP_GATT_OK || notificationIndex >= 3 ||
                parameter->reg_for_notify.handle != notificationHandles[notificationIndex]) {
                ++notificationIndex;
                registerNextNotification();
                break;
            }
            esp_bt_uuid_t cccdUuid = {};
            cccdUuid.len = ESP_UUID_LEN_16;
            cccdUuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_gattc_descr_elem_t descriptor = {};
            uint16_t count = 1;
            if (esp_ble_gattc_get_descr_by_char_handle(gattInterface, gattConnectionId,
                                                       parameter->reg_for_notify.handle, cccdUuid,
                                                       &descriptor, &count) != ESP_GATT_OK || count == 0) {
                ++notificationIndex;
                registerNextNotification();
                break;
            }
            const uint8_t properties = notificationProperties[notificationIndex];
            // CCCD value: 1 = notifications, 2 = indications.
            uint8_t config[2] = {(uint8_t)((properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ? 1 : 2), 0};
            pendingDescriptorHandle = descriptor.handle;
            if (esp_ble_gattc_write_char_descr(gattInterface, gattConnectionId, descriptor.handle,
                                               sizeof(config), config, ESP_GATT_WRITE_TYPE_RSP,
                                               ESP_GATT_AUTH_REQ_MITM) != ESP_OK) {
                pendingDescriptorHandle = 0;
                ++notificationIndex;
                registerNextNotification();
            }
            break;
        }
        case ESP_GATTC_WRITE_DESCR_EVT:
            if (pendingDescriptorHandle != 0 && parameter->write.handle == pendingDescriptorHandle) {
                if (parameter->write.status != ESP_GATT_OK) Serial.printf("[BLE] CCCD write failed: %u\n", parameter->write.status);
                pendingDescriptorHandle = 0;
                ++notificationIndex;
                registerNextNotification();
            }
            break;
        case ESP_GATTC_NOTIFY_EVT: {
            static uint8_t tracedControlPackets = 0;
            const uint8_t* data = parameter->notify.value;
            const size_t length = parameter->notify.value_len;
            const bool isTimecode = length == 12 && data[0] == 0xFF && data[1] == 0x08 && data[2] == 0 && data[4] == 9 && data[5] == 4;
            if (!isTimecode && tracedControlPackets < 32) {
                Serial.printf("[BLE] notify handle=%u len=%u:", parameter->notify.handle, (unsigned)length);
                for (size_t index = 0; index < length; ++index) Serial.printf(" %02X", data[index]);
                Serial.println();
                ++tracedControlPackets;
            }
            parseControlPackets(data, length);
            break;
        }
        case ESP_GATTC_DISCONNECT_EVT: {
            Serial.printf("[BLE] disconnected, reason 0x%02X\n", (unsigned)parameter->disconnect.reason);
            const bool wasConnected = cameraConnected, wasPairing = pinRequested;
            markDisconnected();
            // A pairing failure has already set its own message, so leave that on screen.
            if (wasConnected) setStatus("Camera disconnected");
            else if (wasPairing) setStatus("Pairing lost, retry");
            break;
        }
        default:
            break;
    }
}

// Shows which startup stage failed, on screen and on serial.
static bool bluetoothInitFailure(const char* stage, esp_err_t error) {
    char message[sizeof(statusMessage)];
    snprintf(message, sizeof(message), "BT %s failed %d", stage, error);
    Serial.printf("[BLE] %s failed: %d\n", stage, error);
    setStatus(message);
    return false;
}

// Starts the BLE host and registers the GATT client; keyboard-only I/O makes the camera ask for a PIN.
static bool initializeBluetooth() {
    esp_err_t result = ESP_OK;
    // Initializing the controller directly failed with ESP_ERR_INVALID_STATE; btStart() handles an existing state.
    if (!btStarted() && !btStart()) return bluetoothInitFailure("controller start", ESP_FAIL);
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        return bluetoothInitFailure("controller state", ESP_ERR_INVALID_STATE);
    }

    esp_bluedroid_status_t hostStatus = esp_bluedroid_get_status();
    if (hostStatus == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        result = esp_bluedroid_init();
        if (result != ESP_OK) return bluetoothInitFailure("host init", result);
        hostStatus = esp_bluedroid_get_status();
    }
    if (hostStatus == ESP_BLUEDROID_STATUS_INITIALIZED) {
        result = esp_bluedroid_enable();
        if (result != ESP_OK) return bluetoothInitFailure("host enable", result);
        hostStatus = esp_bluedroid_get_status();
    }
    if (hostStatus != ESP_BLUEDROID_STATUS_ENABLED) {
        return bluetoothInitFailure("host state", ESP_ERR_INVALID_STATE);
    }

    esp_ble_auth_req_t authRequest = ESP_LE_AUTH_REQ_BOND_MITM;
    esp_ble_io_cap_t ioCapability = ESP_IO_CAP_IN;
    uint8_t keySize = 16;
    uint8_t keyMask = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    result = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authRequest, sizeof(authRequest));
    if (result != ESP_OK) return bluetoothInitFailure("auth config", result);
    result = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ioCapability, sizeof(ioCapability));
    if (result != ESP_OK) return bluetoothInitFailure("I/O config", result);
    result = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize));
    if (result != ESP_OK) return bluetoothInitFailure("key size config", result);
    result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keyMask, sizeof(keyMask));
    if (result != ESP_OK) return bluetoothInitFailure("initiator key config", result);
    result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keyMask, sizeof(keyMask));
    if (result != ESP_OK) return bluetoothInitFailure("responder key config", result);
    result = esp_ble_gatt_set_local_mtu(200);
    if (result != ESP_OK) return bluetoothInitFailure("MTU config", result);

    result = esp_ble_gap_register_callback(gapEventHandler);
    if (result != ESP_OK) return bluetoothInitFailure("GAP callback", result);
    result = esp_ble_gattc_register_callback(gattClientEventHandler);
    if (result != ESP_OK) return bluetoothInitFailure("GATT callback", result);
    result = esp_ble_gattc_app_register(0);
    if (result != ESP_OK) return bluetoothInitFailure("GATT app registration", result);
    bleStackReady = true;
    return true;
}

// Starts scanning for the camera; the rest of the connection runs from the BLE events.
static void beginConnection() {
    if (connecting || cameraConnected) return;
    if (!bleStackReady) {
        setStatus("Bluetooth unavailable");
        return;
    }
    connecting = true;
    cameraFound = false;
    cameraAddress = "";
    gattConnectionOpen = false;
    securityReady = false;
    serviceSearchStarted = false;
    serviceStartHandle = serviceEndHandle = 0;
    setStatus("Searching for camera...");
    startCameraScan();
}

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

// Formats the camera values for the grid; shows "--" until they are known.
static void valueStrings(char* iso, char* shutter, char* fps, char* wb, char* tint, char* iris) {
    if (!cameraConnected) {
        strcpy(iso, "--"); strcpy(shutter, "--"); strcpy(fps, "--");
        strcpy(wb, "--"); strcpy(tint, "--"); strcpy(iris, "--");
        return;
    }
    if (haveIso) snprintf(iso, 12, "%ld", (long)cameraIso); else strcpy(iso, "--");
    if (haveShutter) formatShutter(shutter, 12, cameraShutter); else strcpy(shutter, "--");
    if (haveRecordingFormat) snprintf(fps, 12, "%d", recordingFormat.fileFps); else strcpy(fps, "--");
    if (haveWhiteBalance) snprintf(wb, 12, "%dK", cameraWhiteBalance); else strcpy(wb, "--");
    if (haveWhiteBalance) snprintf(tint, 12, "%+d", cameraTint); else strcpy(tint, "--");
    if (haveIris) {
        const float fNumber = irisFNumber(cameraIris);
        snprintf(iris, 12, "f/%.1f", fNumber);
    } else if (cameraConnected && cameraIris == INT16_MIN) strcpy(iris, "--");
    else strcpy(iris, "--");
}

// Fixed-width cells keep the colons in place while the digits change.
static const int TIMECODE_CHARS = 11, TIMECODE_DIGIT_CELL = 11, TIMECODE_COLON_CELL = 6, TIMECODE_LEFT = 72;
static char drawnTimecode[TIMECODE_CHARS + 1] = {};
static uint16_t drawnTimecodeColor = 0;
static bool timecodeCacheValid = false;  // cleared whenever the screen is wiped

// Timecode, or a zeroed counter in clip mode; the TC badge shows only in timecode mode.
static void drawTimecode() {
    char text[20] = "--:--:--:--";
    if (haveTimecode && timecodeSource == 0) {
        snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X", timecodeBytes[3], timecodeBytes[2], timecodeBytes[1], timecodeBytes[0]);
    } else if (haveTimecode && timecodeSource != 0) {
        // While recording, the camera sends its clip counter in the timecode data; keep the last value after stopping.
        if (transportMode == 2) {
            for (uint8_t i = 0; i < 4; ++i) clipHeldBytes[i] = clipLive ? timecodeBytes[i] : 0;
        }
        snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X", clipHeldBytes[3], clipHeldBytes[2], clipHeldBytes[1], clipHeldBytes[0]);
    }
    const uint16_t color = transportMode == 2 ? RED : WHITE;
    if (!timecodeCacheValid || color != drawnTimecodeColor) {
        memset(drawnTimecode, 0, sizeof(drawnTimecode));
        drawnTimecodeColor = color;
        timecodeCacheValid = true;
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
    // The badge spans the same rows as the digits of the timecode font.
    const int badgeX = 202, badgeY = 36, badgeWidth = 24, badgeHeight = 13;
    if (timecodeSource == 0) {
        M5.Lcd.fillRect(badgeX, badgeY, badgeWidth, badgeHeight, WHITE);
        M5.Lcd.setTextColor(BLACK, WHITE);
        M5.Lcd.setTextDatum(MC_DATUM);
        M5.Lcd.drawString("TC", badgeX + badgeWidth / 2, badgeY + badgeHeight / 2 + 1, 1);
    } else {
        M5.Lcd.fillRect(badgeX, badgeY, badgeWidth, badgeHeight, BLACK);
    }
    M5.Lcd.setTextDatum(ML_DATUM);
}

// Right edge of the Core2 battery icon (including its tip) and left edge of the Cam label.
static int coreBatteryRight() {
    return 8 + M5.Lcd.textWidth("MagicPilot", 2) + 6 + 37;
}

static int camLabelLeft() {
    return 237 - M5.Lcd.textWidth("Camera", 2);
}

// Battery icon with the voltage inside it.
static void drawVoltage() {
    char voltage[10];
    if (haveVoltage) {
        const int tenths = (cameraVoltage + 50) / 100;
        snprintf(voltage, sizeof(voltage), "%d.%dV", tenths / 10, tenths % 10);
    } else {
        snprintf(voltage, sizeof(voltage), "--.-V");
    }
    const bool critical = haveVoltage && cameraVoltage < 6800;  // millivolts
    const uint16_t color = critical ? RED : (haveVoltage ? WHITE : GREY);
    const int clearLeft = camLabelLeft() - 3;
    M5.Lcd.fillRect(clearLeft, 0, 280 - clearLeft, 27, UI_BAR);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(MR_DATUM);
    M5.Lcd.drawString("Camera", 237, 14, 2);
    M5.Lcd.fillRoundRect(241, 7, 34, 14, 2, color);
    M5.Lcd.fillRect(275, 11, 3, 6, color);
    M5.Lcd.setTextColor(BLACK, color);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(voltage, 258, 14, 1);
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
static bool remoteCharging = false;
static uint32_t lastBatteryReadMs = 0;

// The Core2's own battery as a small icon with the percentage inside, next to the title.
static void drawRemoteBattery() {
    if (remoteBatteryPercent < 0) return;
    const int x = 8 + M5.Lcd.textWidth("MagicPilot", 2) + 6;
    char text[6];
    snprintf(text, sizeof(text), "%d%%", remoteBatteryPercent);
    const uint16_t color = remoteCharging ? UI_GREEN : (remoteBatteryPercent < 15 ? RED : WHITE);
    M5.Lcd.fillRect(x - 2, 7, 42, 14, UI_BAR);
    M5.Lcd.fillRoundRect(x, 7, 34, 14, 2, color);
    M5.Lcd.fillRect(x + 34, 11, 3, 6, color);
    M5.Lcd.setTextColor(BLACK, color);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString(text, x + 17, 14, 1);
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
    if (!pinRequested && (headerExpanded || presetScreenOpen || focusScreenOpen)) drawRemoteBattery();
}

// Header: name on the left, camera mode in the middle, link state on the right.
// Tapping it also shows the remote battery and the camera battery; the mode label then sits midway between them.
static int modeCenterX() {
    return headerExpanded ? (coreBatteryRight() + camLabelLeft()) / 2 : 160;
}

static void drawTopBar() {
    M5.Lcd.fillRect(0, 0, 320, 28, UI_BAR);
    M5.Lcd.drawFastHLine(0, 27, 320, GREY);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString("MagicPilot", 8, 14, 2);
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
    char iso[12], shutter[12], fps[12], wb[12], tint[12], iris[12];
    valueStrings(iso, shutter, fps, wb, tint, iris);
    drawCell(6, 56, 78, 34, "ISO", iso, selectedValue == VALUE_ISO);
    drawCell(86, 56, 78, 34, "SHUTTER", shutter, selectedValue == VALUE_SHUTTER, true);
    drawCell(166, 56, 78, 34, "FPS", fps, selectedValue == VALUE_FPS);
    drawCell(6, 93, 78, 34, "WHITE BAL", wb, selectedValue == VALUE_WB);
    drawCell(86, 93, 78, 34, "TINT", tint, selectedValue == VALUE_TINT);
    drawCell(166, 93, 78, 34, "IRIS", iris, selectedValue == VALUE_IRIS);
}

static const char* mediumName(int8_t medium) {
    switch (medium) {
        case 0: return "CFAST";
        case 1: return "SD";
        case 2: return "SSD";
        default: return "--";
    }
}

// 12x16 SD card symbol.
static void drawSdIcon(int x, int y, uint16_t color, uint16_t background) {
    M5.Lcd.fillRoundRect(x, y, 12, 16, 2, color);
    M5.Lcd.fillTriangle(x + 7, y, x + 11, y, x + 11, y + 4, background);
    for (int index = 0; index < 3; ++index) M5.Lcd.fillRect(x + 2 + index * 3, y + 6, 2, 6, background);
}

// 12x16 CFast card symbol.
static void drawCFastIcon(int x, int y, uint16_t color, uint16_t background) {
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
    if (medium == 1 || medium == 0) {
        if (medium == 1) drawSdIcon(cursor, SLOT_ROW_Y, foreground, UI_BAR);
        else drawCFastIcon(cursor, SLOT_ROW_Y, foreground, UI_BAR);
        cursor += 12;
    } else {
        M5.Lcd.drawString(mediumName(medium), cursor, centerY, 1);
        cursor += M5.Lcd.textWidth(mediumName(medium), 1);
    }
    cursor += 4;

    const int32_t seconds = slots[slot].remainingSeconds;
    const bool known = seconds >= 0 && (seconds > 0 || active);
    char minutes[8];
    if (known) snprintf(minutes, sizeof(minutes), "%ld", (long)(seconds / 60));
    else snprintf(minutes, sizeof(minutes), "--");
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString(minutes, cursor, centerY, 2);
    cursor += M5.Lcd.textWidth(minutes, 2) + 2;
    M5.Lcd.drawString("min", cursor, centerY, 1);
}

static void drawSlots() {
    if (!footerExpanded) return;
    M5.Lcd.fillRect(0, SLOT_ROW_Y, 320, 17, UI_BAR);
    for (uint8_t slot = 0; slot < 3; ++slot) drawSlotCell(slot, 4 + slot * 104);
}

// The - / + buttons for the selected value, and the connect button.
static void drawControls() {
    M5.Lcd.fillRect(126, 130, 122, 71, BLACK);
    M5.Lcd.drawRoundRect(131, 130, 54, 71, 8, WHITE);
    M5.Lcd.drawRoundRect(190, 130, 54, 71, 8, WHITE);
    M5.Lcd.fillRect(144, 162, 28, 6, WHITE);
    M5.Lcd.fillRect(203, 162, 28, 6, WHITE);
    M5.Lcd.fillRect(214, 151, 6, 28, WHITE);
    M5.Lcd.fillRect(249, 167, 70, 34, BLACK);
    if (cameraConnected) {
        M5.Lcd.drawRoundRect(249, 167, 70, 34, 4, GREY);
        M5.Lcd.setTextColor(GREY, BLACK);
    } else {
        M5.Lcd.fillRoundRect(249, 167, 70, 34, 4, UI_CONNECT);
        M5.Lcd.setTextColor(WHITE, UI_CONNECT);
    }
    M5.Lcd.setTextDatum(MC_DATUM);
    // Short labels so the larger font fits the 70 px wide button; CONNECT only has room for 1 px between letters.
    drawSpacedString(cameraConnected ? "ONLINE" : connecting ? "SEARCH" : "CONNECT", 284, 184, 2, cameraConnected || connecting ? 2 : 1);
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

static const int PRESET_BUTTON_X = 6, PRESET_BUTTON_Y = 167, PRESET_BUTTON_W = 118, PRESET_BUTTON_H = 34;
static const int FOCUS_BUTTON_X = 6, FOCUS_BUTTON_MAIN_Y = 130, FOCUS_BUTTON_MAIN_W = 118, FOCUS_BUTTON_MAIN_H = 34;

static void drawPresetButton() {
    M5.Lcd.fillRect(PRESET_BUTTON_X, PRESET_BUTTON_Y, PRESET_BUTTON_W, PRESET_BUTTON_H, BLACK);
    M5.Lcd.drawRect(PRESET_BUTTON_X, PRESET_BUTTON_Y, PRESET_BUTTON_W, PRESET_BUTTON_H, GREY);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    drawSpacedString("PRESETS", PRESET_BUTTON_X + PRESET_BUTTON_W / 2, PRESET_BUTTON_Y + PRESET_BUTTON_H / 2, 2, 2);
}

static void drawFocusButton() {
    M5.Lcd.fillRect(FOCUS_BUTTON_X, FOCUS_BUTTON_MAIN_Y, FOCUS_BUTTON_MAIN_W, FOCUS_BUTTON_MAIN_H, BLACK);
    M5.Lcd.drawRect(FOCUS_BUTTON_X, FOCUS_BUTTON_MAIN_Y, FOCUS_BUTTON_MAIN_W, FOCUS_BUTTON_MAIN_H, GREY);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    drawSpacedString("FOCUS", FOCUS_BUTTON_X + FOCUS_BUTTON_MAIN_W / 2, FOCUS_BUTTON_MAIN_Y + FOCUS_BUTTON_MAIN_H / 2, 2, 2);
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
    timecodeCacheValid = false;
    drawTopBar();
    drawTimecode();
    drawValues();
    drawControls();
    drawRecordButton();
    drawPresetButton();
    drawFocusButton();
    drawStatus();
}

// Keypad for the 6-digit pairing PIN that the camera displays.
static const int PIN_KEY_X[3] = {4, 109, 214};
static const int PIN_KEY_Y[4] = {30, 83, 136, 189};
static const int PIN_KEY_W = 101, PIN_KEY_H = 49;

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

    if (row < 3 && pinDigits < 6) {
        pinEntry[pinDigits] = (char)('0' + row * 3 + column + 1);
        ++pinDigits;
        drawPinScreen();
    } else if (row == 3 && column == 0) {
        if (pinDigits > 0) pinEntry[--pinDigits] = '\0';
        drawPinScreen();
    } else if (row == 3 && column == 1 && pinDigits < 6) {
        pinEntry[pinDigits] = '0';
        ++pinDigits;
        drawPinScreen();
    } else if (row == 3 && column == 2 && pinDigits == 6) {
        uint32_t pin = 0;
        for (uint8_t i = 0; i < 6; ++i) pin = pin * 10 + (uint32_t)(pinEntry[i] - '0');
        enteredPin = pin;
        pinReady = true;
        pinScreenDrawn = false;
    }
}

static void drawByte(uint8_t* header, uint32_t value) {
    header[0] = value & 0xFF;
    header[1] = (value >> 8) & 0xFF;
    header[2] = (value >> 16) & 0xFF;
    header[3] = (value >> 24) & 0xFF;
}

// Saves the screen as a numbered BMP, at most once every 1.5 s.
static void writeBmpCapture() {
    if (!CAPTURE_SCREENSHOTS || !sdReady || !screenshotQueued || millis() - lastScreenshotMs < 1500) return;
    screenshotQueued = false;
    char path[48];
    do {
        snprintf(path, sizeof(path), "/ui_captures/ui_%05lu.bmp", (unsigned long)nextScreenshotIndex++);
    } while (SD.exists(path));
    if (!SD.exists("/ui_captures")) SD.mkdir("/ui_captures");
    File file = SD.open(path, FILE_WRITE);
    if (!file) return;

    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    drawByte(header + 2, 54 + 320 * 240 * 3);
    drawByte(header + 10, 54);
    drawByte(header + 14, 40);
    drawByte(header + 18, 320);
    drawByte(header + 22, 240);
    header[26] = 1;
    header[28] = 24;
    drawByte(header + 34, 320 * 240 * 3);
    file.write(header, sizeof(header));

    uint8_t rgbRow[320 * 3];
    for (int y = 239; y >= 0; --y) {
        M5.Lcd.readRectRGB(0, y, 320, 1, rgbRow);
        for (int x = 0; x < 320; ++x) {
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

// Index of the step closest to value.
static int findStepIndex(const int32_t* steps, size_t count, int32_t value) {
    int closest = 0;
    int32_t distance = INT32_MAX;
    for (size_t i = 0; i < count; ++i) {
        const int32_t currentDistance = abs(steps[i] - value);
        if (currentDistance < distance) { distance = currentDistance; closest = (int)i; }
    }
    return closest;
}

// Moves the selected value one step up or down on the camera.
static void adjustSelected(int direction) {
    if (!cameraConnected) return;
    bool wrote = false;
    switch (selectedValue) {
        case VALUE_ISO: {
            const int index = findStepIndex(ISO_STEPS, sizeof(ISO_STEPS) / sizeof(ISO_STEPS[0]), cameraIso);
            const int next = constrain(index + direction, 0, (int)(sizeof(ISO_STEPS) / sizeof(ISO_STEPS[0])) - 1);
            wrote = sendInt32Packet(ISO_PACKET, ISO_STEPS[next]);
            // Show the new value right away; the camera's report corrects it if the change is refused.
            if (wrote) { cameraIso = ISO_STEPS[next]; haveIso = true; isoDirty = true; }
            break;
        }
        case VALUE_SHUTTER: {
            const int index = findStepIndex(SHUTTER_STEPS, sizeof(SHUTTER_STEPS) / sizeof(SHUTTER_STEPS[0]), cameraShutter);
            const int next = constrain(index + direction, 0, (int)(sizeof(SHUTTER_STEPS) / sizeof(SHUTTER_STEPS[0])) - 1);
            wrote = sendInt32Packet(SHUTTER_PACKET, SHUTTER_STEPS[next]);
            if (wrote) { cameraShutter = SHUTTER_STEPS[next]; haveShutter = true; shutterDirty = true; }
            break;
        }
        case VALUE_FPS: {
            if (!haveRecordingFormat) break;
            int index = 0;
            for (size_t i = 0; i < sizeof(FPS_STEPS) / sizeof(FPS_STEPS[0]); ++i) if (FPS_STEPS[i] == recordingFormat.fileFps) index = (int)i;
            const int next = constrain(index + direction, 0, (int)(sizeof(FPS_STEPS) / sizeof(FPS_STEPS[0])) - 1);
            wrote = sendRecordingFormat(FPS_STEPS[next]);
            if (wrote) { recordingFormat.fileFps = FPS_STEPS[next]; formatDirty = true; }
            break;
        }
        case VALUE_WB: {
            // Move to the next multiple of WB_STEP, even if the camera is currently between steps.
            const int kelvin = cameraWhiteBalance;
            const int next = direction > 0 ? (kelvin / WB_STEP + 1) * WB_STEP : ((kelvin + WB_STEP - 1) / WB_STEP - 1) * WB_STEP;
            wrote = sendWhiteBalance(constrain(next, WB_MIN, WB_MAX), cameraTint);
            break;
        }
        case VALUE_TINT: wrote = sendWhiteBalance(cameraWhiteBalance, constrain(cameraTint + direction, -50, 50)); break;
        case VALUE_IRIS: if (haveIris) wrote = sendIrisStep(direction); break;
    }
    if (wrote) requestScreenshot();
}

// Settings stored in one preset slot.
struct Preset {
    bool used;
    int32_t iso;
    int32_t shutter;
    int16_t fps;
    int16_t whiteBalance;
    int16_t tint;
    int16_t iris;
};

static const uint8_t PRESET_COUNT = 2;
// One JSON file per preset, named like 2026-10-05_MagicPilot_Remote_Preset1.json (the date comes from the camera).
static const char* PRESET_DIR = "/presets";
static const int PRESET_ROW_Y[PRESET_COUNT] = {34, 100};
static const int PRESET_ROW_H = 60;
static const int PRESET_ACTION_OFFSET_Y = 8, PRESET_ACTION_H = 44, PRESET_ACTION_W = 56;
static const int PRESET_SAVE_X = 132, PRESET_LOAD_X = 193, PRESET_CLEAR_X = 254;
static const int PRESET_BACK_X = 6, PRESET_BACK_Y = 164, PRESET_BACK_W = 154, PRESET_BACK_H = 38;
// Each write waits for a GATT response, so space them out instead of queueing them back to back.
static const uint32_t PRESET_WRITE_GAP_MS = 60;
static const uint32_t IRIS_REPORT_TIMEOUT_MS = 800;
enum LoadStep : uint8_t { LOAD_IDLE, LOAD_SHUTTER, LOAD_FPS, LOAD_WB, LOAD_IRIS, LOAD_IRIS_WAIT };
static LoadStep loadStep = LOAD_IDLE;
static Preset loadingPreset;
static uint8_t loadingSlot;
static bool loadOk;
static uint32_t loadReadyMs;
static int16_t irisBefore;
static int irisLastDirection;
static uint8_t irisAttempts;
static Preset presets[PRESET_COUNT] = {};
static int clearArmedSlot = -1;  // slot waiting for a second CLEAR tap

// M5.begin() mounts the card at 40 MHz, which some cards can't do; retry slower on the Core2's own chip-select pin.
static bool mountSd() {
    if (SD.cardType() != CARD_NONE) return true;
    SD.end();
    return SD.begin(TFCARD_CS_PIN, SPI, 25000000) || SD.begin(TFCARD_CS_PIN, SPI, 10000000);
}

// Finds the newest file of a slot; names start with the date, so the greatest name is the newest.
static bool findPresetFile(uint8_t slot, char* path, size_t size) {
    char suffix[40];
    snprintf(suffix, sizeof(suffix), "MagicPilot_Remote_Preset%d.json", (int)slot + 1);
    if (!SD.exists(PRESET_DIR)) return false;
    File directory = SD.open(PRESET_DIR);
    if (!directory) return false;
    String newest;
    for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
        String name = entry.name();
        name = name.substring(name.lastIndexOf('/') + 1);
        if (!entry.isDirectory() && name.endsWith(suffix) && name > newest) newest = name;
    }
    directory.close();
    if (newest.length() == 0) return false;
    snprintf(path, size, "%s/%s", PRESET_DIR, newest.c_str());
    return true;
}

// Reads the number after "key" in flat JSON; isNull is set for a JSON null.
static bool jsonNumber(const String& json, const char* key, double& value, bool& isNull) {
    char quoted[24];
    snprintf(quoted, sizeof(quoted), "\"%s\"", key);
    int position = json.indexOf(quoted);
    if (position < 0) return false;
    position = json.indexOf(':', position + (int)strlen(quoted));
    if (position < 0) return false;
    ++position;
    while (position < (int)json.length() && isspace((unsigned char)json[position])) ++position;
    isNull = json.startsWith("null", position);
    if (isNull) return true;
    const char* start = json.c_str() + position;
    char* end = nullptr;
    value = strtod(start, &end);
    return end != start;
}

// Loads one preset file into its slot; a missing or incomplete file leaves the slot empty.
static bool readPresetSlot(uint8_t slot) {
    char path[96];
    if (!findPresetFile(slot, path, sizeof(path))) return true;
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    const String json = file.readString();
    file.close();

    double iso = 0, shutter = 0, fps = 0, whiteBalance = 0, tint = 0, iris = 0;
    bool notNull = false, irisUnset = true;
    const bool complete = jsonNumber(json, "iso", iso, notNull) && jsonNumber(json, "shutter_angle", shutter, notNull) &&
                          jsonNumber(json, "fps", fps, notNull) && jsonNumber(json, "white_balance", whiteBalance, notNull) &&
                          jsonNumber(json, "tint", tint, notNull);
    if (!complete) return true;
    if (!jsonNumber(json, "iris", iris, irisUnset)) irisUnset = true;

    Preset& preset = presets[slot];
    preset.used = true;
    preset.iso = (int32_t)iso;
    preset.shutter = (int32_t)(shutter * 100.0 + 0.5);
    preset.fps = (int16_t)fps;
    preset.whiteBalance = (int16_t)whiteBalance;
    preset.tint = (int16_t)tint;
    preset.iris = irisUnset ? INT16_MIN : (int16_t)iris;
    return true;
}

// Loads both presets from the SD card; returns false when there is no card.
static bool readPresetFile() {
    for (uint8_t slot = 0; slot < PRESET_COUNT; ++slot) presets[slot] = Preset{};
    if (!sdReady) sdReady = mountSd();
    if (!sdReady) return false;
    for (uint8_t slot = 0; slot < PRESET_COUNT; ++slot) {
        if (!readPresetSlot(slot)) return false;
    }
    return true;
}

// Writes the slot's JSON file, or deletes it when the slot is empty.
static bool writePresetSlot(uint8_t slot) {
    if (!sdReady) sdReady = mountSd();
    if (!sdReady) return false;
    char path[96];
    while (findPresetFile(slot, path, sizeof(path))) {
        if (!SD.remove(path)) break;
    }
    const Preset& preset = presets[slot];
    if (!preset.used) return true;
    if (!SD.exists(PRESET_DIR)) SD.mkdir(PRESET_DIR);
    char clockDate[11], clockTime[9];
    const bool haveDate = cameraDateTime(clockDate, clockTime);
    if (haveDate) snprintf(path, sizeof(path), "%s/%s_MagicPilot_Remote_Preset%d.json", PRESET_DIR, clockDate, (int)slot + 1);
    else snprintf(path, sizeof(path), "%s/MagicPilot_Remote_Preset%d.json", PRESET_DIR, (int)slot + 1);
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    // date and time come from the camera and are null when it didn't report them; shutter_angle is in degrees;
    // iris is the camera's raw aperture value, null when the lens doesn't report it.
    char dateField[16] = "null", timeField[16] = "null";
    if (haveDate) snprintf(dateField, sizeof(dateField), "\"%s\"", clockDate);
    if (clockTime[0] != '\0') snprintf(timeField, sizeof(timeField), "\"%s\"", clockTime);
    file.printf("{\n  \"description\": \"MagicPilot Preset\",\n  \"slot\": %d,\n  \"date\": %s,\n  \"time\": %s,\n"
                "  \"iso\": %ld,\n  \"shutter_angle\": %ld.%02ld,\n  \"fps\": %d,\n"
                "  \"white_balance\": %d,\n  \"tint\": %d,\n  \"iris\": ",
                (int)slot + 1, dateField, timeField, (long)preset.iso, (long)(preset.shutter / 100), (long)(preset.shutter % 100),
                (int)preset.fps, (int)preset.whiteBalance, (int)preset.tint);
    if (preset.iris == INT16_MIN) file.print("null");
    else file.print((int)preset.iris);
    file.print("\n}\n");
    file.close();
    return true;
}

// Shows a status message that contains the 1-based preset number.
static void setPresetStatus(const char* format, uint8_t slot) {
    char message[sizeof(statusMessage)];
    snprintf(message, sizeof(message), format, (int)slot + 1);
    setStatus(message);
}

// Stores the camera's current values in a slot; iris is optional because some lenses don't report it.
static void savePreset(uint8_t slot) {
    if (!cameraConnected || !haveIso || !haveShutter || !haveRecordingFormat || !haveWhiteBalance) {
        setStatus("Connect camera to save preset");
        return;
    }
    if (!readPresetFile()) {
        setStatus("No SD card");
        return;
    }
    Preset preset = {};
    preset.used = true;
    preset.iso = cameraIso;
    preset.shutter = cameraShutter;
    preset.fps = recordingFormat.fileFps;
    preset.whiteBalance = cameraWhiteBalance;
    preset.tint = cameraTint;
    preset.iris = haveIris ? cameraIris : INT16_MIN;
    presets[slot] = preset;
    if (writePresetSlot(slot)) {
        setPresetStatus("Preset %d saved", slot);
    } else {
        readPresetFile();
        setStatus("SD write failed");
    }
}

// Re-reads the slot from the card and sends its values to the camera.
static void loadPreset(uint8_t slot) {
    if (!cameraConnected) {
        setStatus("Connect camera to load preset");
        return;
    }
    if (!readPresetFile()) {
        setStatus("No SD card");
        return;
    }
    const Preset preset = presets[slot];
    if (!preset.used) {
        setPresetStatus("Preset %d is empty", slot);
        return;
    }
    if (loadStep != LOAD_IDLE) {
        setStatus("Preset load in progress");
        return;
    }
    loadingPreset = preset;
    loadingSlot = slot;
    irisLastDirection = 0;
    irisAttempts = 0;
    loadOk = sendInt32Packet(ISO_PACKET, preset.iso);
    loadStep = LOAD_SHUTTER;
    loadReadyMs = millis() + PRESET_WRITE_GAP_MS;
    setPresetStatus("Loading preset %d...", slot);
}

static void finishPresetLoad() {
    loadStep = LOAD_IDLE;
    setPresetStatus(loadOk ? "Preset %d loaded" : "Preset %d load incomplete", loadingSlot);
}

// Sends the next preset value once the previous write has had its gap; called every loop pass.
static void advancePresetLoad() {
    if (loadStep == LOAD_IDLE) return;
    if (!cameraConnected) {
        loadStep = LOAD_IDLE;
        return;
    }
    const uint32_t now = millis();
    const bool due = (int32_t)(now - loadReadyMs) >= 0;
    if (loadStep == LOAD_IRIS_WAIT) {
        if (cameraIris != irisBefore) loadStep = LOAD_IRIS;
        else if (due) {
            loadOk = false;
            finishPresetLoad();
        }
        return;
    }
    if (!due) return;

    if (loadStep == LOAD_SHUTTER) {
        loadOk = sendInt32Packet(SHUTTER_PACKET, loadingPreset.shutter) && loadOk;
        loadStep = LOAD_FPS;
    } else if (loadStep == LOAD_FPS) {
        loadOk = sendRecordingFormat(loadingPreset.fps) && loadOk;
        loadStep = LOAD_WB;
    } else if (loadStep == LOAD_WB) {
        loadOk = sendWhiteBalance(loadingPreset.whiteBalance, loadingPreset.tint) && loadOk;
        if (loadingPreset.iris == INT16_MIN || !haveIris) {
            finishPresetLoad();
            return;
        }
        loadStep = LOAD_IRIS;
    } else if (loadStep == LOAD_IRIS) {
        const int16_t before = cameraIris;
        const int direction = loadingPreset.iris > before ? 1 : -1;
        // Reversing direction means the target was passed, so this is the nearest stop.
        if (abs(before - loadingPreset.iris) < 100 || (irisLastDirection != 0 && direction != irisLastDirection)) {
            finishPresetLoad();
            return;
        }
        if (irisAttempts++ >= 30 || !sendIrisStep(direction)) {
            loadOk = false;
            finishPresetLoad();
            return;
        }
        irisBefore = before;
        irisLastDirection = direction;
        loadStep = LOAD_IRIS_WAIT;
        loadReadyMs = now + IRIS_REPORT_TIMEOUT_MS;
        return;
    }
    loadReadyMs = now + PRESET_WRITE_GAP_MS;
}

// Erases the slot on the card.
static void clearPreset(uint8_t slot) {
    if (!readPresetFile()) {
        setStatus("No SD card");
        return;
    }
    if (!presets[slot].used) {
        setPresetStatus("Preset %d is already empty", slot);
        return;
    }
    presets[slot] = Preset{};
    if (writePresetSlot(slot)) {
        setPresetStatus("Preset %d cleared", slot);
    } else {
        readPresetFile();
        setStatus("SD write failed");
    }
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
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.fillRoundRect(PRESET_SAVE_X, buttonY, PRESET_ACTION_W, PRESET_ACTION_H, 4, UI_CONNECT);
    M5.Lcd.setTextColor(WHITE, UI_CONNECT);
    M5.Lcd.drawString("SAVE", PRESET_SAVE_X + PRESET_ACTION_W / 2, buttonY + PRESET_ACTION_H / 2, 2);
    M5.Lcd.fillRoundRect(PRESET_LOAD_X, buttonY, PRESET_ACTION_W, PRESET_ACTION_H, 4, BLACK);
    M5.Lcd.drawRoundRect(PRESET_LOAD_X, buttonY, PRESET_ACTION_W, PRESET_ACTION_H, 4, preset.used ? UI_GREEN : GREY);
    M5.Lcd.setTextColor(preset.used ? WHITE : GREY, BLACK);
    M5.Lcd.drawString("LOAD", PRESET_LOAD_X + PRESET_ACTION_W / 2, buttonY + PRESET_ACTION_H / 2, 2);

    const bool armed = clearArmedSlot == slot;
    if (armed) {
        M5.Lcd.fillRoundRect(PRESET_CLEAR_X, buttonY, PRESET_ACTION_W, PRESET_ACTION_H, 4, RED);
    } else {
        M5.Lcd.fillRoundRect(PRESET_CLEAR_X, buttonY, PRESET_ACTION_W, PRESET_ACTION_H, 4, BLACK);
        M5.Lcd.drawRoundRect(PRESET_CLEAR_X, buttonY, PRESET_ACTION_W, PRESET_ACTION_H, 4, preset.used ? RED : GREY);
    }
    M5.Lcd.setTextColor(preset.used || armed ? WHITE : GREY, armed ? RED : BLACK);
    M5.Lcd.drawString(armed ? "SURE?" : "CLEAR", PRESET_CLEAR_X + PRESET_ACTION_W / 2, buttonY + PRESET_ACTION_H / 2, 2);
}

static void drawPresetScreen() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.fillRect(0, 0, 320, 28, UI_BAR);
    M5.Lcd.drawFastHLine(0, 27, 320, GREY);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString("MagicPilot", 8, 14, 2);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("PRESETS", 160, 14, 2);
    drawRemoteBattery();
    for (uint8_t slot = 0; slot < PRESET_COUNT; ++slot) drawPresetRow(slot);
    M5.Lcd.drawRoundRect(PRESET_BACK_X, PRESET_BACK_Y, PRESET_BACK_W, PRESET_BACK_H, 4, GREY);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    drawSpacedString("BACK", PRESET_BACK_X + PRESET_BACK_W / 2, PRESET_BACK_Y + PRESET_BACK_H / 2, 2, 2);
    drawStatus();
}

static void openPresetScreen() {
    presetScreenOpen = true;
    clearArmedSlot = -1;
    readPresetFile();
    drawPresetScreen();
}

// BACK returns to the main screen; SAVE, LOAD and CLEAR act on their row, and CLEAR needs two taps.
static void handlePresetTouch(int x, int y) {
    if (x >= PRESET_BACK_X && x < PRESET_BACK_X + PRESET_BACK_W && y >= PRESET_BACK_Y && y < PRESET_BACK_Y + PRESET_BACK_H) {
        clearArmedSlot = -1;
        presetScreenOpen = false;
        drawScreen();
        requestScreenshot();
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

static const int16_t FOCUS_MAX = 2048;
static const int FOCUS_SLIDER_X = 20, FOCUS_SLIDER_Y = 64, FOCUS_SLIDER_W = 280, FOCUS_SLIDER_H = 30;
static const int FOCUS_BUTTON_Y = 114, FOCUS_BUTTON_W = 96, FOCUS_BUTTON_H = 50;
static const int FOCUS_MINUS_X = 8, FOCUS_AF_X = 112, FOCUS_PLUS_X = 216;
static const int FOCUS_BACK_X = 6, FOCUS_BACK_Y = 170, FOCUS_BACK_W = 154, FOCUS_BACK_H = 32;
static const uint32_t FOCUS_REPEAT_MS = 100;

static bool insideRect(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

// Sets focus (0 to 2048) and updates the local value right away.
static bool sendFocus(int value) {
    uint8_t packet[sizeof(FOCUS_PACKET)];
    memcpy(packet, FOCUS_PACKET, sizeof(packet));
    const int16_t raw = (int16_t)constrain(value, 0, (int)FOCUS_MAX);
    writeInt16LE(packet + 8, raw);
    cameraFocus = raw;
    haveFocus = true;
    return writeCameraPacket(packet, sizeof(packet));
}

// One-shot autofocus; the camera reports the new focus afterwards.
static bool sendAutofocus() {
    return writeCameraPacket(AUTOFOCUS_PACKET, sizeof(AUTOFOCUS_PACKET));
}

// Redraws only the percentage and slider, so dragging doesn't flicker.
static void drawFocusDynamic() {
    char text[8];
    if (haveFocus) snprintf(text, sizeof(text), "%d%%", ((int)cameraFocus * 100 + FOCUS_MAX / 2) / FOCUS_MAX);
    else snprintf(text, sizeof(text), "--");
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextPadding(100);
    M5.Lcd.drawString(text, 160, 45, 4);
    M5.Lcd.setTextPadding(0);

    M5.Lcd.fillRect(FOCUS_SLIDER_X - 4, FOCUS_SLIDER_Y - 2, FOCUS_SLIDER_W + 8, FOCUS_SLIDER_H + 4, BLACK);
    M5.Lcd.drawRect(FOCUS_SLIDER_X, FOCUS_SLIDER_Y, FOCUS_SLIDER_W, FOCUS_SLIDER_H, GREY);
    if (haveFocus) {
        const int position = (int)cameraFocus * (FOCUS_SLIDER_W - 1) / FOCUS_MAX;
        if (position > 0) M5.Lcd.fillRect(FOCUS_SLIDER_X + 1, FOCUS_SLIDER_Y + 1, position, FOCUS_SLIDER_H - 2, UI_CONNECT);
        M5.Lcd.fillRect(FOCUS_SLIDER_X + position - 2, FOCUS_SLIDER_Y - 2, 5, FOCUS_SLIDER_H + 4, WHITE);
    }
}

static void drawFocusScreen() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.fillRect(0, 0, 320, 28, UI_BAR);
    M5.Lcd.drawFastHLine(0, 27, 320, GREY);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(WHITE, UI_BAR);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString("MagicPilot", 8, 14, 2);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("FOCUS", 160, 14, 2);
    drawRemoteBattery();

    M5.Lcd.setTextColor(GREY, BLACK);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.drawString("NEAR", FOCUS_SLIDER_X, FOCUS_SLIDER_Y + FOCUS_SLIDER_H + 6, 1);
    M5.Lcd.setTextDatum(TR_DATUM);
    M5.Lcd.drawString("FAR", FOCUS_SLIDER_X + FOCUS_SLIDER_W, FOCUS_SLIDER_Y + FOCUS_SLIDER_H + 6, 1);
    drawFocusDynamic();

    const int centerY = FOCUS_BUTTON_Y + FOCUS_BUTTON_H / 2;
    M5.Lcd.drawRoundRect(FOCUS_MINUS_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H, 8, WHITE);
    M5.Lcd.fillRect(FOCUS_MINUS_X + FOCUS_BUTTON_W / 2 - 10, centerY - 2, 20, 4, WHITE);
    M5.Lcd.drawRoundRect(FOCUS_PLUS_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H, 8, WHITE);
    M5.Lcd.fillRect(FOCUS_PLUS_X + FOCUS_BUTTON_W / 2 - 10, centerY - 2, 20, 4, WHITE);
    M5.Lcd.fillRect(FOCUS_PLUS_X + FOCUS_BUTTON_W / 2 - 2, centerY - 10, 4, 20, WHITE);
    M5.Lcd.fillRoundRect(FOCUS_AF_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H, 8, UI_CONNECT);
    M5.Lcd.setTextColor(WHITE, UI_CONNECT);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("AF", FOCUS_AF_X + FOCUS_BUTTON_W / 2, centerY, 4);

    M5.Lcd.drawRoundRect(FOCUS_BACK_X, FOCUS_BACK_Y, FOCUS_BACK_W, FOCUS_BACK_H, 4, GREY);
    M5.Lcd.setTextColor(WHITE, BLACK);
    drawSpacedString("BACK", FOCUS_BACK_X + FOCUS_BACK_W / 2, FOCUS_BACK_Y + FOCUS_BACK_H / 2, 2, 2);
    drawStatus();
}

static bool onFocusSlider(int x, int y) {
    return insideRect(x, y, FOCUS_SLIDER_X - 6, FOCUS_SLIDER_Y - 8, FOCUS_SLIDER_W + 12, FOCUS_SLIDER_H + 16);
}

// The slider and the -/+ buttons repeat while held; AF does not.
static bool focusTouchRepeats(int x, int y) {
    return onFocusSlider(x, y) || insideRect(x, y, FOCUS_MINUS_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H) ||
           insideRect(x, y, FOCUS_PLUS_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H);
}

static void openFocusScreen() {
    focusScreenOpen = true;
    drawFocusScreen();
}

static void handleFocusTouch(int x, int y) {
    if (insideRect(x, y, FOCUS_BACK_X, FOCUS_BACK_Y, FOCUS_BACK_W, FOCUS_BACK_H)) {
        focusScreenOpen = false;
        drawScreen();
        requestScreenshot();
        return;
    }
    const bool onSlider = onFocusSlider(x, y);
    const bool onMinus = insideRect(x, y, FOCUS_MINUS_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H);
    const bool onPlus = insideRect(x, y, FOCUS_PLUS_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H);
    const bool onAutofocus = insideRect(x, y, FOCUS_AF_X, FOCUS_BUTTON_Y, FOCUS_BUTTON_W, FOCUS_BUTTON_H);
    if (!onSlider && !onMinus && !onPlus && !onAutofocus) return;
    if (!cameraConnected) {
        setStatus("Connect camera to set focus");
        return;
    }
    if (onSlider) {
        sendFocus(constrain((x - FOCUS_SLIDER_X) * FOCUS_MAX / FOCUS_SLIDER_W, 0, FOCUS_MAX));
    } else if (onMinus || onPlus) {
        if (!haveFocus) {
            setStatus("Focus unknown, use the slider");
            return;
        }
        // Step in whole percent so the readout changes by exactly 1 per tap.
        const int percent = ((int)cameraFocus * 100 + FOCUS_MAX / 2) / FOCUS_MAX + (onPlus ? 1 : -1);
        sendFocus((constrain(percent, 0, 100) * FOCUS_MAX + 50) / 100);
    } else {
        setStatus(sendAutofocus() ? "Autofocus triggered" : "Autofocus failed");
        return;
    }
    drawFocusDynamic();
}

static void toggleTimecodeSource() {
    timecodeSource = timecodeSource == 0 ? 1 : 0;
    sourceDirty = true;
    sendInt8Packet(TIMECODE_SOURCE_PACKET, timecodeSource);
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
    if (presetScreenOpen) {
        handlePresetTouch(x, y);
        return;
    }
    if (focusScreenOpen) {
        handleFocusTouch(x, y);
        return;
    }
    if (y < 28) {
        if (cameraConnected && abs(x - modeCenterX()) <= 35) {
            toggleTimecodeSource();
            return;
        }
        headerExpanded = !headerExpanded;
        drawTopBar();
        return;
    }
    if (y >= 56 && y <= 163 && x >= 253) {
        if (cameraConnected) sendInt8Packet(TRANSPORT_PACKET, transportMode == 2 ? 0 : 2);
    } else if (y >= 56 && y <= 127 && x >= 6 && x <= 244) {
        const uint8_t column = x < 84 ? 0 : x < 164 ? 1 : 2;
        const uint8_t row = y < 91 ? 0 : 1;
        selectedValue = row == 0 ? column : (uint8_t)(VALUE_WB + column);
        drawValues();
        requestScreenshot();
    } else if (y >= 130 && y <= 200 && x >= 126 && x <= 248) {
        adjustSelected(x < 188 ? -1 : 1);
    } else if (y >= 164 && y <= 201 && x >= 249) {
        if (!cameraConnected) beginConnection();
    } else if (x >= PRESET_BUTTON_X && x < PRESET_BUTTON_X + PRESET_BUTTON_W && y >= PRESET_BUTTON_Y && y < PRESET_BUTTON_Y + PRESET_BUTTON_H) {
        openPresetScreen();
    } else if (insideRect(x, y, FOCUS_BUTTON_X, FOCUS_BUTTON_MAIN_Y, FOCUS_BUTTON_MAIN_W, FOCUS_BUTTON_MAIN_H)) {
        openFocusScreen();
    } else if (y >= 28 && y <= 55 && cameraConnected) {
        toggleTimecodeSource();
    }
}

// Redraws only what changed since the last pass; the preset and focus screens handle their own drawing.
static void updateDirtyDisplay() {
    if (focusDirty) {
        focusDirty = false;
        if (focusScreenOpen) drawFocusDynamic();
    }
    if (presetScreenOpen || focusScreenOpen) {
        if (statusDirty) { statusDirty = false; drawStatus(); }
        return;
    }
    bool redrawValues = false;
    if (isoDirty) { isoDirty = false; redrawValues = true; }
    if (shutterDirty) { shutterDirty = false; redrawValues = true; }
    if (formatDirty) { formatDirty = false; redrawValues = true; }
    if (whiteBalanceDirty) { whiteBalanceDirty = false; redrawValues = true; }
    if (irisDirty) { irisDirty = false; redrawValues = true; }
    if (redrawValues) {
        drawValues();
        requestScreenshot();
    }
    if (timecodeDirty) { timecodeDirty = false; drawTimecode(); }
    if (transportDirty) {
        transportDirty = false;
        drawTopBar(); drawSlots(); drawRecordButton(); drawTimecode();
        requestScreenshot();
    }
    if (statusDirty) { statusDirty = false; drawTopBar(); drawStatus(); }
    if (sourceDirty) { sourceDirty = false; drawTimecode(); }
    if (connectionStateDirty || cameraConnected != lastConnectionState) {
        connectionStateDirty = false;
        lastConnectionState = cameraConnected;
        drawScreen();
        requestScreenshot();
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
    if (sdReady && CAPTURE_SCREENSHOTS) {
        if (!SD.exists("/ui_captures")) SD.mkdir("/ui_captures");
        while (true) {
            char path[48];
            snprintf(path, sizeof(path), "/ui_captures/ui_%05lu.bmp", (unsigned long)nextScreenshotIndex);
            if (!SD.exists(path)) break;
            ++nextScreenshotIndex;
        }
    }

    initializeBluetooth();
    drawScreen();
    Serial.println("[BOOT] BMPCC remote ready; tap CONNECT to scan");
}

// Applies dirty-flag redraws, answers the pairing PIN, handles touch, and saves queued screenshots.
void loop() {
    M5.update();
    updateDirtyDisplay();
    updateRemoteBattery();
    advancePresetLoad();
    if (passkeyReplyPending && pinReady) {
        const esp_err_t result = esp_ble_passkey_reply(pairingBda, true, enteredPin);
        passkeyReplyPending = false;
        pinReady = false;
        pinRequested = false;
        pinScreenDrawn = false;
        drawScreen();
        requestScreenshot();
        if (result != ESP_OK) setStatus("Passkey reply failed");
    }
    if (pinRequested && !pinScreenDrawn) drawPinScreen();

    // The INT pin can go low before the controller is read; that stale point is (-1,-1) and must not consume the tap.
    const TouchPoint_t point = M5.Touch.getPressPoint();
    const bool touchDown = M5.Touch.ispressed() && point.x >= 0 && point.y >= 0;
    if (touchDown) {
        const uint32_t now = millis();
        if (!touchWasDown) Serial.printf("[TOUCH] down x=%d y=%d\n", point.x, point.y);
        const bool repeatAdjustment = !pinRequested && !presetScreenOpen && !focusScreenOpen && point.y >= 130 && point.y <= 200 && point.x >= 126 && point.x <= 248;
        const bool repeatFocus = focusScreenOpen && !pinRequested && focusTouchRepeats(point.x, point.y);
        if (!touchWasDown || (repeatAdjustment && now - lastStepMs >= 220) || (repeatFocus && now - lastStepMs >= FOCUS_REPEAT_MS)) {
            handleTouch(point.x, point.y);
            lastStepMs = now;
        }
    }
    touchWasDown = touchDown;
    writeBmpCapture();
    delay(2);
}