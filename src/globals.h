#pragma once

// Mutable global state shared between the BLE task and the main loop, plus the fixed
// value-step tables and outgoing packet templates used throughout the firmware.

#include "config.h"

// --- BLE connection handles and state, owned by the Bluedroid callback task ---
static esp_gatt_if_t gattInterface = ESP_GATT_IF_NONE;  // our registered GATT client
static uint16_t gattConnectionId = 0;                   // identifies the open link in every GATT call
static uint16_t serviceStartHandle = 0;                 // handle range of the camera service, used to look up characteristics
static uint16_t serviceEndHandle = 0;
static uint16_t outgoingHandle = 0;                     // characteristic every control packet is written to
static uint16_t deviceNameHandle = 0;                   // written once so the camera shows this remote by name
static uint16_t pendingDescriptorHandle = 0;            // CCCD write we are waiting for a response on
// The three characteristics that notify: control in, timecode, status.
static uint16_t notificationHandles[3] = {0, 0, 0};
static esp_gatt_char_prop_t notificationProperties[3] = {0, 0, 0};  // decides notify versus indicate
static uint8_t notificationIndex = 0;                               // which of the three is being subscribed to right now
static esp_bd_addr_t cameraBda = {};                                // address of the camera we are talking to
static esp_bd_addr_t pairingBda = {};                               // address the passkey request came from
static volatile bool cameraConnected = false;                       // service found and the control characteristic is writable
static volatile bool connecting = false;                            // a scan or connection attempt is in flight
static volatile bool bleStackReady = false;                         // controller, host and callbacks all came up
static volatile bool scanParametersReady = false;                   // the controller accepted the scan parameters
static volatile bool cameraFound = false;                           // a matching advertisement was seen during this scan
static volatile bool gattConnectionOpen = false;                    // the service search needs this and securityReady
static volatile bool securityReady = false;                         // pairing finished and the link is encrypted
static volatile bool serviceSearchStarted = false;                  // keeps the search from being issued twice
static volatile bool passkeyReplyPending = false;                   // the camera is waiting for a PIN
static volatile bool pinRequested = false;                          // the keypad is on screen
static volatile bool pinSubmitted = false;                          // all six digits entered and confirmed

// Set by the BLE task, cleared once the main loop has redrawn that part of the screen.
static volatile bool valuesDirty = false;     // the value grid
static volatile bool timecodeDirty = false;   // the timecode row and its TC badge
static volatile bool transportDirty = false;  // record state, which also repaints the header and slots
static volatile bool statusDirty = false;     // the footer message
static volatile bool focusDirty = false;      // the focus page slider and percentage
static volatile bool connectionDirty = true;  // forces a full redraw; set on connect and disconnect

// Camera state. Each sentinel below means "not reported yet", so no extra "have" flags are needed.
static volatile int32_t cameraIso = 0;
static volatile int32_t cameraShutter = 0;                          // degrees * 100
static volatile int16_t cameraWhiteBalance = 0;                     // kelvin; also gates the tint readout
static volatile int16_t cameraTint = 0;                             // -50 to +50, reported with the white balance
static volatile int16_t cameraIris = INT16_MIN;                     // fixed16 aperture value (raw / 2048)
static volatile int16_t cameraFocus = -1;                           // fixed16, 0 (near) to FOCUS_MAX (far)
static volatile int32_t cameraVoltage = 0;                          // millivolts
static volatile RecordingFormat recordingFormat = {0, 0, 0, 0, 0};  // fileFps of 0 means nothing reported yet
static volatile uint8_t transportMode = 0;                          // 0 = preview, 1 = playback, 2 = recording
static volatile uint8_t transportFlags = 0;                         // bit 5 = slot 1 active, bit 6 = slot 2 active
static volatile uint8_t slotMedium[2] = {0, 0};                     // 0 = CFast, 1 = SD, 2 = SSD
static volatile int32_t slotRemaining[3] = {-1, -1, -1};            // seconds left per media slot
static volatile uint8_t timecodeBytes[4] = {0, 0, 0, 0};            // BCD frames, seconds, minutes, hours
static volatile uint8_t timecodeSource = 0;                         // 0 = timecode, 1 = clip counter (observed on the camera)
static volatile bool haveTimecode = false;                          // 00:00:00:00 is a valid value, so this needs its own flag

// --- Remote state, only touched from the main loop ---
static bool sdReady = false;                                         // the card mounted, so presets and captures can be written
static bool screenshotQueued = false;                                // a capture is waiting to be written out
static bool pinScreenDrawn = false;                                  // keeps the keypad from being repainted every pass
static bool headerExpanded = false;                                  // main header shows battery details only after a tap
static bool footerExpanded = false;                                  // footer shows the card slots only after a tap
static uint8_t screen = SCREEN_MAIN;                                 // the page currently drawn, one of the Screen values
static bool touchWasDown = false;                                    // previous pass, so a press is only acted on once
static uint32_t nextScreenshotIndex = 0;                             // counts up so captures keep their order across reboots
static uint32_t lastScreenshotMs = 0;                                // rate limits captures
static uint32_t lastStepMs = 0;                                      // last repeat of a held stepper or focus key
static uint32_t pendingTimecodeCount = UINT32_MAX;                   // candidate after a timecode jump; UINT32_MAX = none
static uint8_t clipHeldBytes[4] = {};                                // last clip counter (BCD), kept after recording stops
static bool clipLive = false;                                        // a timecode packet has arrived since the last transport change
static uint8_t selectedValue = VALUE_ISO;                            // which grid cell the - / + buttons act on
static uint8_t pinDigits = 0;                                        // digits entered so far
static char pinEntry[6] = {};                                        // the six PIN digits as characters, not null terminated
static char statusMessage[44] = "Camera not found (pairing mode?)";  // footer text

// Values the - / + buttons step through. ISO and FPS are the rates the camera accepts.
static const int32_t ISO_STEPS[] = {100, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800, 16000, 20000, 25600};
// Shutter angles in camera units (degrees * 100), so 11.2 degrees is 1120.
static const int32_t SHUTTER_STEPS[] = {1120, 1500, 2250, 3000, 3750, 4500, 6000, 7200, 7500, 9000, 10800, 12000, 14400, 15000, 17280, 18000, 21600, 27000, 32400, 36000};
static const int WB_MIN = 2500, WB_MAX = 10000, WB_STEP = 50;  // kelvin
static const int32_t FPS_STEPS[] = {5, 6, 8, 10, 12, 15, 18, 20, 23, 24, 25, 30, 48, 50, 60, 72, 90, 100, 120};  // 23 and 24 are 23.98 and 24 on the camera

// Packet layout: dest, length, command, reserved, category, parameter, type, operation, then the value padded to 4 bytes.
static const uint8_t ISO_PACKET[] = {0xFF, 0x08, 0x00, 0x00, 0x01, 0x0E, 0x03, 0x00, 0, 0, 0, 0};
static const uint8_t SHUTTER_PACKET[] = {0xFF, 0x08, 0x00, 0x00, 0x01, 0x0B, 0x03, 0x00, 0, 0, 0, 0};
// Carries the whole recording format, so the other fields have to be filled in from the last report.
static const uint8_t FRAME_RATE_PACKET[] = {0xFF, 0x0E, 0x00, 0x00, 0x01, 0x09, 0x02, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// Kelvin and tint sit side by side as two int16 in the value.
static const uint8_t WHITE_BALANCE_PACKET[] = {0xFF, 0x08, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0, 0, 0, 0};
// Aperture ordinal with the offset operation: steps one stop in the lens's list. Setting the f-stop directly is ignored until the iris has been changed on the camera.
static const uint8_t IRIS_STEP_PACKET[] = {0xFF, 0x06, 0x00, 0x00, 0x00, 0x04, 0x02, 0x01, 0, 0, 0, 0};
static const uint8_t TRANSPORT_PACKET[] = {0xFF, 0x05, 0x00, 0x00, 0x0A, 0x01, 0x01, 0x00, 0, 0, 0, 0};        // 0 = stop, 2 = record
static const uint8_t TIMECODE_SOURCE_PACKET[] = {0xFF, 0x05, 0x00, 0x00, 0x04, 0x07, 0x01, 0x00, 0, 0, 0, 0};  // 0 = timecode, 1 = clip counter
static const uint8_t FOCUS_PACKET[] = {0xFF, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0, 0, 0, 0};            // absolute fixed16 position
static const uint8_t AUTOFOCUS_PACKET[] = {0xFF, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};                    // no value, triggers one shot
