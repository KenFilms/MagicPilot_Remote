#pragma once

// BLE COMMUNICATION — packet encoding/decoding for the Blackmagic protocol.

#include "config.h"
#include "globals.h"

// Reads a little-endian signed value out of a packet payload.
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

// Writes a signed 16-bit value into a packet in little-endian order.
static void writeInt16LE(uint8_t* destination, int16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

// Fills the little-endian value of a 12-byte packet template and sends it. Bytes past
// valueBytes stay zero, because the camera treats them as padding rather than payload.
static bool sendPacket(const uint8_t* packetTemplate, int32_t value, uint8_t valueBytes) {
    uint8_t packet[12];
    memcpy(packet, packetTemplate, sizeof(packet));
    for (uint8_t i = 0; i < valueBytes; ++i) packet[8 + i] = (uint8_t)(value >> (8 * i));
    return writeCameraPacket(packet, sizeof(packet));
}

// Changes only the frame rate; the camera needs the other format fields sent back unchanged.
static bool sendRecordingFormat(int16_t fps) {
    if (recordingFormat.fileFps <= 0) return false;
    // Five sequential int16 fields starting at offset 8: fps, a reserved zero, width, height, flags.
    const int16_t fields[5] = {fps, 0, recordingFormat.width, recordingFormat.height, recordingFormat.flags};
    uint8_t packet[sizeof(FRAME_RATE_PACKET)];
    memcpy(packet, FRAME_RATE_PACKET, sizeof(packet));
    for (uint8_t i = 0; i < 5; ++i) writeInt16LE(packet + 8 + i * 2, fields[i]);
    return writeCameraPacket(packet, sizeof(packet));
}

// Kelvin and tint are two int16 inside the 4-byte value. Also updates the local values so
// the UI reacts before the camera confirms.
static bool sendWhiteBalance(int16_t kelvin, int16_t tint) {
    cameraWhiteBalance = kelvin;
    cameraTint = tint;
    valuesDirty = true;
    return sendPacket(WHITE_BALANCE_PACKET, (int32_t)((uint16_t)kelvin | ((uint32_t)(uint16_t)tint << 16)), 4);
}

// Sets focus (0 to FOCUS_MAX) and updates the local value right away.
static bool sendFocus(int value) {
    cameraFocus = (int16_t)constrain(value, 0, (int)FOCUS_MAX);
    return sendPacket(FOCUS_PACKET, cameraFocus, 2);
}

// Clears the cached camera state and marks every display element for redraw.
static void markDisconnected() {
    cameraConnected = false;
    connecting = false;
    pinRequested = false;
    pinSubmitted = false;
    passkeyReplyPending = false;
    pinScreenDrawn = false;
    gattConnectionId = 0;
    serviceStartHandle = serviceEndHandle = 0;
    outgoingHandle = deviceNameHandle = pendingDescriptorHandle = 0;
    notificationIndex = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        notificationHandles[i] = 0;
        notificationProperties[i] = 0;
    }
    gattConnectionOpen = securityReady = serviceSearchStarted = false;
    cameraIso = cameraShutter = cameraVoltage = 0;
    cameraWhiteBalance = cameraTint = 0;
    cameraIris = INT16_MIN;
    cameraFocus = -1;
    recordingFormat.fileFps = 0;
    haveTimecode = false;
    valuesDirty = timecodeDirty = transportDirty = true;
    statusDirty = focusDirty = connectionDirty = true;
}

// True if both nibbles are decimal digits and the value is below limit.
static bool validBcd(uint8_t value, uint8_t limit) {
    const uint8_t high = value >> 4;
    const uint8_t low = value & 0x0F;
    return high <= 9 && low <= 9 && high * 10 + low < limit;
}

// Converts one packed BCD byte, such as 0x24, to its decimal value.
static uint8_t bcdToInt(uint8_t value) {
    return (uint8_t)((value >> 4) * 10 + (value & 0x0F));
}

// Camera clock as last reported; secondsOfDay is only meaningful when haveTime is set.
struct CameraClock {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint32_t secondsOfDay;
    bool haveTime;        // the camera reported a date but not always a time
    uint32_t receivedMs;  // millis() at the report, so the time can be advanced since
    bool valid;           // false until a usable date has been decoded
};
static CameraClock cameraClock = {};  // last clock report, used to date the preset files

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

// Length of the month, taking leap years into account.
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

// Frame rate to use for timecode maths; 30 while the camera hasn't reported one.
static uint32_t timecodeFps() {
    return recordingFormat.fileFps > 0 && recordingFormat.fileFps <= 60 ? (uint32_t)recordingFormat.fileFps : 30;
}

// Converts a BCD timecode to a frame count.
static uint32_t timecodeFrameCount(const uint8_t* bytes, uint32_t fps) {
    return ((bcdToInt(bytes[3]) * 60UL + bcdToInt(bytes[2])) * 60UL + bcdToInt(bytes[1])) * fps + bcdToInt(bytes[0]);
}

// Stores a new timecode after rejecting invalid values and one-off jumps.
static void setTimecode(const uint8_t* bytes) {
    if (!validBcd(bytes[0], 60) || !validBcd(bytes[1], 60) || !validBcd(bytes[2], 60) || !validBcd(bytes[3], 24)) return;
    const uint32_t fps = timecodeFps();
    const uint32_t frameCount = timecodeFrameCount(bytes, fps);
    if (haveTimecode) {
        const uint32_t previous = timecodeFrameCount((const uint8_t*)timecodeBytes, fps);
        // Ignore a jump of more than 3 s unless a packet a few frames later confirms it (packets arrive every ~2 frames).
        if (frameCount > previous + fps * 3 || previous > frameCount + fps * 3) {
            if (pendingTimecodeCount == UINT32_MAX || frameCount <= pendingTimecodeCount || frameCount > pendingTimecodeCount + 4) {
                pendingTimecodeCount = frameCount;
                return;
            }
        }
        pendingTimecodeCount = UINT32_MAX;
    }
    for (uint8_t i = 0; i < 4; ++i) timecodeBytes[i] = bytes[i];
    haveTimecode = true;
    timecodeDirty = true;
    if (transportMode == 2) clipLive = true;
}

// One cached field, updated the same way for every "simple" report: copy bytes, mark a flag dirty.
struct PacketHandler {
    uint8_t category, parameter, type;  // type TYPE_ANY matches any type byte
    uint8_t minLength;                  // minimum valueLength for the report to be applied
    void (*apply)(const uint8_t* data, size_t valueLength);
};
static const uint8_t TYPE_ANY = 0xFF;

static void applyIso(const uint8_t* data, size_t) { cameraIso = readInt32(data); valuesDirty = true; }
static void applyShutter(const uint8_t* data, size_t) { cameraShutter = readInt32(data); valuesDirty = true; }

static void applyWhiteBalance(const uint8_t* data, size_t) {
    cameraWhiteBalance = readInt16(data);
    cameraTint = readInt16(data + 2);
    valuesDirty = true;
}

static void applyRecordingFormat(const uint8_t* data, size_t) {
    recordingFormat.fileFps = readInt16(data);
    recordingFormat.sensorFps = readInt16(data + 2);
    recordingFormat.width = readInt16(data + 4);
    recordingFormat.height = readInt16(data + 6);
    recordingFormat.flags = readInt16(data + 8);
    valuesDirty = true;
}

static void applyIris(const uint8_t* data, size_t) { cameraIris = readInt16(data); valuesDirty = true; }

static void applyFocus(const uint8_t* data, size_t) {
    cameraFocus = constrain(readInt16(data), 0, FOCUS_MAX);
    focusDirty = true;
}

static void applyTransport(const uint8_t* data, size_t valueLength) {
    const uint8_t previousMode = transportMode;
    transportMode = data[0];
    // The clip counter is only valid once a timecode packet arrives after the change.
    if (transportMode != previousMode) clipLive = false;
    if (valueLength >= 5) {
        transportFlags = data[2];
        slotMedium[0] = data[3];
        slotMedium[1] = data[4];
    }
    transportDirty = true;
}

static void applyVoltage(const uint8_t* data, size_t) { cameraVoltage = readInt16(data); statusDirty = true; }

static void applySlotRemaining(const uint8_t* data, size_t valueLength) {
    for (uint8_t i = 0; i < 3 && (size_t)(i + 1) * 2 <= valueLength; ++i) slotRemaining[i] = readInt16(data + i * 2);
    statusDirty = true;
}

static void applyTimecodeSource(const uint8_t* data, size_t) {
    timecodeSource = data[0];
    timecodeDirty = true;
}

static void applyTimecode(const uint8_t* data, size_t) { setTimecode(data); }

static void applyClock(const uint8_t* data, size_t) {
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

// One entry per camera report we understand, in no particular order; the first match wins.
static const PacketHandler PACKET_HANDLERS[] = {
    {1, 14, 3, 4, applyIso},
    {1, 11, 3, 4, applyShutter},
    {1, 2, 2, 4, applyWhiteBalance},
    {1, 9, 2, 10, applyRecordingFormat},
    {0, 2, 128, 2, applyIris},
    {0, 0, 128, 2, applyFocus},
    {10, 1, TYPE_ANY, 1, applyTransport},
    {9, 0, 2, 2, applyVoltage},
    {9, 2, 2, 0, applySlotRemaining},
    {4, 7, TYPE_ANY, 1, applyTimecodeSource},
    {9, 4, TYPE_ANY, 4, applyTimecode},
    {7, 0, TYPE_ANY, 8, applyClock},
};

// Decodes camera notifications and updates the cached camera state.
static void parseControlPackets(const uint8_t* bytes, size_t length) {
    size_t offset = 0;
    while (offset + 4 <= length) {
        const uint8_t* packet = bytes + offset;
        const uint8_t payloadLength = packet[1];
        const size_t rawPacketLength = 4 + payloadLength;
        const size_t paddedPacketLength = 4 + ((payloadLength + 3) & ~((size_t)3));
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
            for (const PacketHandler& handler : PACKET_HANDLERS) {
                if (handler.category == category && handler.parameter == parameter &&
                    (handler.type == TYPE_ANY || handler.type == type) && valueLength >= handler.minLength) {
                    handler.apply(data, valueLength);
                    break;
                }
            }
        }
        offset += packetLength;
    }
}
