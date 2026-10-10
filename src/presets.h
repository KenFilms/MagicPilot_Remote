#pragma once

// PRESETS — SD card storage: reading, writing and sending preset values to the camera.

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"
#include "ble_connection.h"

// Settings stored in one preset slot.
struct Preset {
    bool used;             // false for an empty slot, which has no file on the card
    int32_t iso;
    int32_t shutter;       // degrees * 100
    int16_t fps;
    int16_t whiteBalance;  // kelvin
    int16_t tint;
    int16_t iris;          // fixed16 aperture, INT16_MIN when the lens didn't report one
};

static const uint8_t PRESET_COUNT = 2;  // rows on the preset page, and files on the card
// One JSON file per preset, named like 2026-10-05_MagicPilot_Remote_Preset1.json (the date comes from the camera).
static const char* PRESET_DIR = "/presets";
// Each write waits for a GATT response, so space them out instead of queueing them back to back.
static const uint32_t PRESET_WRITE_GAP_MS = 60;
static const uint32_t IRIS_REPORT_TIMEOUT_MS = 800;  // how long to wait for the camera to confirm an iris step

// Loading a preset walks these steps, one write per pass of the main loop.
enum LoadStep : uint8_t { LOAD_IDLE, LOAD_SHUTTER, LOAD_FPS, LOAD_WB, LOAD_IRIS, LOAD_IRIS_WAIT };
static LoadStep loadStep = LOAD_IDLE;
static Preset loadingPreset;      // copy of the slot being loaded, so a later card read can't change it
static uint8_t loadingSlot;       // slot number, only used for the status message
static bool loadOk;               // cleared as soon as one write fails
static uint32_t loadReadyMs;      // the next write waits until this time
static int16_t irisBefore;        // aperture before the last step, to detect that the camera moved
static int irisLastDirection;     // a reversal means the target stop was passed
static uint8_t irisAttempts;      // capped so a lens that never reaches the target can't loop forever
static Preset presets[PRESET_COUNT] = {};  // slots as last read from the card

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
    const struct { const char* key; double* value; } requiredFields[] = {
        {"iso", &iso}, {"shutter_angle", &shutter}, {"fps", &fps}, {"white_balance", &whiteBalance}, {"tint", &tint},
    };
    for (const auto& field : requiredFields) {
        if (!jsonNumber(json, field.key, *field.value, notNull)) return true;
    }
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

    // date, time and iris are written as JSON null when the camera or lens didn't report them.
    char dateJson[14] = "null", timeJson[12] = "null", irisJson[8] = "null";
    if (haveDate) snprintf(dateJson, sizeof(dateJson), "\"%s\"", clockDate);
    if (clockTime[0] != '\0') snprintf(timeJson, sizeof(timeJson), "\"%s\"", clockTime);
    if (preset.iris != INT16_MIN) snprintf(irisJson, sizeof(irisJson), "%d", (int)preset.iris);

    file.printf(
        "{\n"
        "  \"description\": \"MagicPilot Preset\",\n"
        "  \"slot\": %d,\n"
        "  \"date\": %s,\n"
        "  \"time\": %s,\n"
        "  \"iso\": %ld,\n"
        "  \"shutter_angle\": %ld.%02ld,\n"
        "  \"fps\": %d,\n"
        "  \"white_balance\": %d,\n"
        "  \"tint\": %d,\n"
        "  \"iris\": %s\n"
        "}\n",
        (int)slot + 1, dateJson, timeJson,
        (long)preset.iso, (long)(preset.shutter / 100), (long)(preset.shutter % 100),
        (int)preset.fps, (int)preset.whiteBalance, (int)preset.tint, irisJson);
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
    if (!cameraConnected || cameraIso <= 0 || cameraShutter <= 0 || recordingFormat.fileFps <= 0 || cameraWhiteBalance <= 0) {
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
    preset.iris = cameraIris;
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
    loadOk = sendPacket(ISO_PACKET, preset.iso, 4);
    loadStep = LOAD_SHUTTER;
    loadReadyMs = millis() + PRESET_WRITE_GAP_MS;
    setPresetStatus("Loading preset %d...", slot);
}

// Ends the load sequence and reports whether every write got through.
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

    switch (loadStep) {
        case LOAD_SHUTTER:
            loadOk = sendPacket(SHUTTER_PACKET, loadingPreset.shutter, 4) && loadOk;
            loadStep = LOAD_FPS;
            break;
        case LOAD_FPS:
            loadOk = sendRecordingFormat(loadingPreset.fps) && loadOk;
            loadStep = LOAD_WB;
            break;
        case LOAD_WB:
            loadOk = sendWhiteBalance(loadingPreset.whiteBalance, loadingPreset.tint) && loadOk;
            if (loadingPreset.iris == INT16_MIN || cameraIris == INT16_MIN) {
                finishPresetLoad();
                return;
            }
            loadStep = LOAD_IRIS;
            break;
        case LOAD_IRIS: {
            const int16_t before = cameraIris;
            const int direction = loadingPreset.iris > before ? 1 : -1;
            // Reversing direction means the target was passed, so this is the nearest stop.
            if (abs(before - loadingPreset.iris) < 100 || (irisLastDirection != 0 && direction != irisLastDirection)) {
                finishPresetLoad();
                return;
            }
            if (irisAttempts++ >= 30 || !sendPacket(IRIS_STEP_PACKET, direction, 2)) {
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
        default:
            break;
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
