#pragma once

// Value stepping — translates the UI's selected cell into camera commands.

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"

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

// Step lists for the three stepped values, in CameraValue order.
static const struct { const int32_t* steps; uint8_t count; } STEP_LISTS[3] = {
    {ISO_STEPS, sizeof(ISO_STEPS) / sizeof(ISO_STEPS[0])},
    {SHUTTER_STEPS, sizeof(SHUTTER_STEPS) / sizeof(SHUTTER_STEPS[0])},
    {FPS_STEPS, sizeof(FPS_STEPS) / sizeof(FPS_STEPS[0])},
};

// Moves the selected value one step up or down on the camera.
static void adjustSelected(int direction) {
    if (!cameraConnected) return;
    bool wrote = false;
    if (selectedValue <= VALUE_FPS) {
        const int32_t current = selectedValue == VALUE_ISO ? cameraIso
                              : selectedValue == VALUE_SHUTTER ? cameraShutter
                              : recordingFormat.fileFps;
        const auto& list = STEP_LISTS[selectedValue];
        const int index = constrain(findStepIndex(list.steps, list.count, current) + direction, 0, list.count - 1);
        const int32_t next = list.steps[index];
        // Show the new value right away; the camera's report corrects it if the change is refused.
        if (selectedValue == VALUE_FPS) {
            if ((wrote = sendRecordingFormat((int16_t)next))) recordingFormat.fileFps = (int16_t)next;
        } else if (selectedValue == VALUE_ISO) {
            if ((wrote = sendPacket(ISO_PACKET, next, 4))) cameraIso = next;
        } else {
            if ((wrote = sendPacket(SHUTTER_PACKET, next, 4))) cameraShutter = next;
        }
        if (wrote) valuesDirty = true;
    } else if (selectedValue == VALUE_WB) {
        // Move to the next multiple of WB_STEP, even if the camera is currently between steps.
        const int kelvin = cameraWhiteBalance;
        const int next = direction > 0 ? (kelvin / WB_STEP + 1) * WB_STEP : ((kelvin + WB_STEP - 1) / WB_STEP - 1) * WB_STEP;
        wrote = sendWhiteBalance(constrain(next, WB_MIN, WB_MAX), cameraTint);
    } else if (selectedValue == VALUE_TINT) {
        wrote = sendWhiteBalance(cameraWhiteBalance, constrain(cameraTint + direction, -50, 50));
    } else if (cameraIris != INT16_MIN) {
        wrote = sendPacket(IRIS_STEP_PACKET, direction, 2);  // one stop wider (-1) or narrower (+1)
    }
    if (wrote) requestScreenshot();
}
