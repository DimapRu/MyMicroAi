#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// Drives the autofocus MCU inside an OV5640 AF module (OV5640 + VCM lens motor).
// esp_camera_init() never loads the AF microcode, so focus stays fixed until
// begin() uploads it. A plain OV5640 without a motor, or a module whose motor
// supply is not wired, loads the firmware fine but the lens will not move.
class Ov5640Af {
public:
    // Grid used by the sensor for focus zones (the "virtual viewfinder").
    static constexpr uint8_t GRID_W = 80;
    static constexpr uint8_t GRID_H = 60;

    bool begin(sensor_t* sensor);
    bool isAvailable() const;
    bool startContinuous();
    bool focusOnce(uint32_t timeoutMs);
    bool focusAt(uint8_t gridX, uint8_t gridY, uint32_t timeoutMs);
    bool release();
    bool isFocused();
    uint32_t lastLoadMs() const;
    const char* lastError() const;

private:
    sensor_t* sensor_ = nullptr;
    bool available_ = false;
    uint32_t lastLoadMs_ = 0;
    String lastError_;

    bool loadFirmware();
    bool burstWrite(uint16_t reg, const uint8_t* data, size_t length);
    bool writeReg(uint16_t reg, uint8_t value);
    int readReg(uint16_t reg);
    bool sendCommand(uint8_t command, uint32_t timeoutMs);
    bool waitFocused(uint32_t timeoutMs);
    void setError(const String& error);
};
