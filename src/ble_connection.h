#pragma once

// BLE connection lifecycle — advertisement matching, scan/connect, pairing, GATT discovery
// and notifications, and Bluetooth stack startup.

#include "config.h"
#include "globals.h"
#include "ble_protocol.h"

// ----------------------------------------------------------------------------
// BLE advertisement & UUID helpers
// ----------------------------------------------------------------------------

// Value of one hex digit, or -1 if the character is not hex.
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

// True if a discovered 128-bit UUID is the one named by text.
static bool uuidMatches(const esp_bt_uuid_t& candidate, const char* text) {
    const esp_bt_uuid_t expected = uuid128FromString(text);
    return candidate.len == ESP_UUID_LEN_128 && expected.len == ESP_UUID_LEN_128 &&
           memcmp(candidate.uuid.uuid128, expected.uuid.uuid128, ESP_UUID_LEN_128) == 0;
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
    if (strstr(name, "Blackmagic") != nullptr || strstr(name, "BMPCC") != nullptr) return true;

    uint8_t length = 0;
    uint8_t* uuids = esp_ble_resolve_adv_data(advertisement, ESP_BLE_AD_TYPE_128SRV_CMPL, &length);
    if (uuids == nullptr) return false;
    const esp_bt_uuid_t service = uuid128FromString(CAMERA_SERVICE_UUID);
    for (uint8_t offset = 0; offset + ESP_UUID_LEN_128 <= length; offset += ESP_UUID_LEN_128) {
        if (memcmp(uuids + offset, service.uuid.uuid128, ESP_UUID_LEN_128) == 0) return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
// BLE connection lifecycle — scan, open, discover characteristics, subscribe
// ----------------------------------------------------------------------------

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

// Defined below; the subscription loop and its GATT event call each other.
static void registerNextNotification();

// Stores the characteristic handles, marks the camera connected, and starts enabling notifications.
static void discoverCameraCharacteristics() {
    esp_gatt_char_prop_t outgoingProperties = 0;
    notificationHandles[0] = findCharacteristicHandle(INCOMING_UUID, &notificationProperties[0]);
    notificationHandles[1] = findCharacteristicHandle(TIMECODE_UUID, &notificationProperties[1]);
    notificationHandles[2] = findCharacteristicHandle(STATUS_UUID, &notificationProperties[2]);
    outgoingHandle = findCharacteristicHandle(OUTGOING_UUID, &outgoingProperties);
    deviceNameHandle = findCharacteristicHandle(DEVICE_NAME_UUID, nullptr);

    Serial.printf("[BLE] characteristics: control-out=%d control-in=%d timecode=%d status=%d name=%d\n",
                  outgoingHandle != 0, notificationHandles[0] != 0, notificationHandles[1] != 0,
                  notificationHandles[2] != 0, deviceNameHandle != 0);
    Serial.printf("[BLE] properties: control-in=%02X timecode=%02X status=%02X\n",
                  notificationProperties[0], notificationProperties[1], notificationProperties[2]);
    if (deviceNameHandle != 0) {
        uint8_t deviceName[] = "MagicPilot Remote";
        esp_ble_gattc_write_char(gattInterface, gattConnectionId, deviceNameHandle, sizeof(deviceName) - 1,
                                 deviceName, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_MITM);
    }
    cameraConnected = outgoingHandle != 0 && (outgoingProperties & ESP_GATT_CHAR_PROP_BIT_WRITE) != 0;
    connecting = false;
    connectionDirty = true;
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

// ----------------------------------------------------------------------------
// BLE pairing & GAP events
// ----------------------------------------------------------------------------

// Aborts the in-progress connection attempt and shows why.
static void failConnection(const char* message) {
    connecting = false;
    setStatus(message);
}

// A camera advertisement was seen during the scan; stop scanning and open a GATT connection to it.
static void connectToFoundCamera(esp_ble_gap_cb_param_t* parameter) {
    cameraFound = true;
    memcpy(cameraBda, parameter->scan_rst.bda, sizeof(cameraBda));
    char name[32];
    advertisedName(parameter->scan_rst.ble_adv, name, sizeof(name));
    Serial.printf("[BLE] selected camera %02X:%02X:%02X:%02X:%02X:%02X \"%s\"\n",
                  cameraBda[0], cameraBda[1], cameraBda[2], cameraBda[3], cameraBda[4], cameraBda[5], name);
    setStatus("Camera found, connecting...");
    esp_ble_gap_stop_scanning();
    if (esp_ble_gattc_open(gattInterface, cameraBda, parameter->scan_rst.ble_addr_type, true) != ESP_OK) {
        failConnection("Connection failed");
    }
}

// One scan result or the end of the scan; ignored once a camera has already been chosen.
static void handleScanEvent(esp_ble_gap_cb_param_t* parameter) {
    if (!connecting || cameraFound) return;
    if (parameter->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
        if (isCameraAdvertisement(parameter->scan_rst.ble_adv)) connectToFoundCamera(parameter);
    } else if (parameter->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
        Serial.println("[BLE] scan ended without a camera; check camera pairing mode and advertising");
        failConnection("Camera not found (pairing mode?)");
    }
}

// Starts PIN entry; the camera's passkey request lands here.
static void beginPinEntry(esp_ble_gap_cb_param_t* parameter) {
    Serial.println("[BLE] passkey requested; enter the camera PIN on the keypad");
    memcpy(pairingBda, parameter->ble_security.ble_req.bd_addr, sizeof(pairingBda));
    pinDigits = 0;
    memset(pinEntry, 0, sizeof(pinEntry));
    pinSubmitted = false;
    passkeyReplyPending = true;
    pinRequested = true;
    setStatus("Enter camera PIN");
}

// Pairing finished; on success look for the service, on failure drop the link.
static void handleAuthComplete(esp_ble_gap_cb_param_t* parameter) {
    if (parameter->ble_security.auth_cmpl.success) {
        Serial.println("[BLE] encrypted pairing complete");
        securityReady = true;
        startServiceSearchIfReady();
        return;
    }
    Serial.printf("[BLE] authentication failed: %u\n", parameter->ble_security.auth_cmpl.fail_reason);
    pinRequested = false;
    passkeyReplyPending = false;
    failConnection("Pairing failed, retry");
    esp_ble_gap_disconnect(parameter->ble_security.auth_cmpl.bd_addr);
}

// Handles scan results and pairing (security request, PIN request, authentication result).
static void gapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* parameter) {
    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            scanParametersReady = parameter->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS;
            if (scanParametersReady) startCameraScan();
            else failConnection("Scan setup failed");
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT:
            handleScanEvent(parameter);
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            Serial.println("[BLE] security request from camera");
            esp_ble_gap_security_rsp(parameter->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_PASSKEY_REQ_EVT:
            beginPinEntry(parameter);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            handleAuthComplete(parameter);
            break;
        default:
            break;
    }
}

// ----------------------------------------------------------------------------
// GATT client events — service discovery, notifications, incoming data
// ----------------------------------------------------------------------------

// Subscribes to the characteristic by writing its client config descriptor.
// Returns false when there is nothing to wait for, so the caller moves on to the next one.
static bool writeClientConfig(uint16_t characteristicHandle) {
    esp_bt_uuid_t cccdUuid = {};
    cccdUuid.len = ESP_UUID_LEN_16;
    cccdUuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
    esp_gattc_descr_elem_t descriptor = {};
    uint16_t count = 1;
    if (esp_ble_gattc_get_descr_by_char_handle(gattInterface, gattConnectionId, characteristicHandle,
                                               cccdUuid, &descriptor, &count) != ESP_GATT_OK || count == 0) {
        return false;
    }
    // CCCD value: 1 = notifications, 2 = indications.
    const uint8_t properties = notificationProperties[notificationIndex];
    uint8_t config[2] = {(uint8_t)((properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ? 1 : 2), 0};
    pendingDescriptorHandle = descriptor.handle;
    if (esp_ble_gattc_write_char_descr(gattInterface, gattConnectionId, descriptor.handle, sizeof(config),
                                       config, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_MITM) == ESP_OK) {
        return true;
    }
    pendingDescriptorHandle = 0;
    return false;
}

// Drives the connection: open, pair, find the service, enable notifications, then receive data.
static void gattClientEventHandler(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t* parameter) {
    if (event == ESP_GATTC_REG_EVT) {
        if (parameter->reg.status != ESP_GATT_OK) {
            setStatus("GATT client registration failed");
            return;
        }
        gattInterface = interface;
        static esp_ble_scan_params_t scanParameters = {};
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
            const bool expected = parameter->reg_for_notify.status == ESP_GATT_OK && notificationIndex < 3 &&
                                  parameter->reg_for_notify.handle == notificationHandles[notificationIndex];
            if (!expected || !writeClientConfig(parameter->reg_for_notify.handle)) {
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

// ----------------------------------------------------------------------------
// Bluetooth stack startup
// ----------------------------------------------------------------------------

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
    const struct { esp_ble_sm_param_t type; void* value; uint8_t size; const char* stage; } securityParams[] = {
        {ESP_BLE_SM_AUTHEN_REQ_MODE, &authRequest, sizeof(authRequest), "auth config"},
        {ESP_BLE_SM_IOCAP_MODE, &ioCapability, sizeof(ioCapability), "I/O config"},
        {ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize), "key size config"},
        {ESP_BLE_SM_SET_INIT_KEY, &keyMask, sizeof(keyMask), "initiator key config"},
        {ESP_BLE_SM_SET_RSP_KEY, &keyMask, sizeof(keyMask), "responder key config"},
    };
    for (const auto& parameter : securityParams) {
        result = esp_ble_gap_set_security_param(parameter.type, parameter.value, parameter.size);
        if (result != ESP_OK) return bluetoothInitFailure(parameter.stage, result);
    }
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
    gattConnectionOpen = false;
    securityReady = false;
    serviceSearchStarted = false;
    serviceStartHandle = serviceEndHandle = 0;
    setStatus("Searching for camera...");
    startCameraScan();
}
