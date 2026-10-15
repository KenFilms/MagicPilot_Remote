#pragma once

#include "config.h"

#if defined(ARDUINO_M5STACK_CORES3)
#include <LTR5XX.h>

static LTR5XX ambientLightSensor;
static bool ambientLightReady = false;
static bool ambientBrightnessInitialized = false;
static uint16_t filteredAmbientLight = 0;
static uint8_t appliedAmbientBrightness = 0;
static uint32_t lastAmbientBrightnessUpdateMs = 0;

static uint8_t brightnessForAmbientLight(uint16_t light) {
    static const uint16_t lightPoints[] = {0, 50, 150, 400, 1000, 2500, 6000, 14000, 32000, 65535};
    static const uint8_t brightnessPoints[] = {50, 56, 72, 96, 128, 164, 198, 228, 248, 255};
    const size_t pointCount = sizeof(lightPoints) / sizeof(lightPoints[0]);

    for (size_t index = 1; index < pointCount; ++index) {
        if (light <= lightPoints[index]) {
            const uint32_t range = lightPoints[index] - lightPoints[index - 1];
            const uint32_t offset = light - lightPoints[index - 1];
            const int brightnessRange = brightnessPoints[index] - brightnessPoints[index - 1];
            return brightnessPoints[index - 1] + (brightnessRange * offset + range / 2) / range;
        }
    }
    return brightnessPoints[pointCount - 1];
}

static void updateAmbientBrightness(bool force = false) {
    if (!ambientLightReady) return;
    const uint32_t now = millis();
    if (!force && now - lastAmbientBrightnessUpdateMs < 500) return;
    lastAmbientBrightnessUpdateMs = now;

    const uint16_t reading = ambientLightSensor.getAlsValue();
    filteredAmbientLight = ambientBrightnessInitialized
        ? (uint16_t)(((uint32_t)filteredAmbientLight * 3 + reading) / 4)
        : reading;
    ambientBrightnessInitialized = true;

    const uint8_t brightness = brightnessForAmbientLight(filteredAmbientLight);
    const int brightnessDelta = (int)brightness - appliedAmbientBrightness;
    if (appliedAmbientBrightness == 0 || abs(brightnessDelta) >= 2) {
        M5.Display.setBrightness(brightness);
        appliedAmbientBrightness = brightness;
    }
}

static void initializeAmbientBrightness() {
    Ltr5xx_Init_Basic_Para sensorConfig = LTR5XX_BASE_PARA_CONFIG_DEFAULT;
    sensorConfig.als_gain = LTR5XX_ALS_GAIN_48X;
    sensorConfig.als_integration_time = LTR5XX_ALS_INTEGRATION_TIME_100MS;
    sensorConfig.als_measurement_rate = LTR5XX_ALS_MEASUREMENT_RATE_200MS;

    if (!ambientLightSensor.begin(&sensorConfig) || !ambientLightSensor.setAlsMode(LTR5XX_ALS_ACTIVE_MODE)) {
        Serial.println("[ALS] LTR553 initialization failed; using display default brightness");
        return;
    }

    ambientLightReady = true;
    updateAmbientBrightness(true);
    Serial.println("[ALS] automatic display brightness enabled");
}
#else
static void initializeAmbientBrightness() {}
static void updateAmbientBrightness() {}
#endif