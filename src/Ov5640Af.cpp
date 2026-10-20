#include "Ov5640Af.h"
#include <driver/i2c.h>
#include "Ov5640AfFirmware.h"

namespace {
// esp32-camera talks to the sensor (SCCB) through the legacy I2C driver on
// this port; the firmware upload reuses it for fast multi-byte writes.
#if CONFIG_SCCB_HARDWARE_I2C_PORT1
static constexpr i2c_port_t SCCB_PORT = I2C_NUM_1;
#else
static constexpr i2c_port_t SCCB_PORT = I2C_NUM_0;
#endif

static constexpr uint16_t REG_SYSTEM_RESET00 = 0x3000;
static constexpr uint16_t REG_CHIP_ID_HIGH = 0x300A;
static constexpr uint16_t REG_CHIP_ID_LOW = 0x300B;
static constexpr uint16_t REG_CMD_MAIN = 0x3022;
static constexpr uint16_t REG_CMD_ACK = 0x3023;
static constexpr uint16_t REG_CMD_PARA0 = 0x3024;
static constexpr uint16_t REG_CMD_PARA1 = 0x3025;
static constexpr uint16_t REG_CMD_PARA2 = 0x3026;
static constexpr uint16_t REG_CMD_PARA3 = 0x3027;
static constexpr uint16_t REG_CMD_PARA4 = 0x3028;
static constexpr uint16_t REG_FW_STATUS = 0x3029;
static constexpr uint16_t FIRMWARE_ADDR = 0x8000;

static constexpr uint8_t CMD_SINGLE_FOCUS = 0x03;
static constexpr uint8_t CMD_CONTINUOUS_FOCUS = 0x04;
static constexpr uint8_t CMD_RELEASE_FOCUS = 0x08;
static constexpr uint8_t CMD_TOUCH_ZONE = 0x81;

static constexpr uint8_t FW_STATUS_IDLE = 0x70;
static constexpr uint8_t FW_STATUS_FOCUSED = 0x10;

static constexpr size_t BURST_CHUNK = 256;
// Size of the touch focus zone on the 80x60 grid.
static constexpr uint8_t TOUCH_ZONE_W = 16;
static constexpr uint8_t TOUCH_ZONE_H = 12;
}

bool Ov5640Af::begin(sensor_t* sensor) {
    sensor_ = sensor;
    available_ = false;
    lastError_ = "";

    if (!sensor_) {
        setError(F("No camera sensor"));
        return false;
    }

    const int idHigh = readReg(REG_CHIP_ID_HIGH);
    const int idLow = readReg(REG_CHIP_ID_LOW);
    if (idHigh != 0x56 || idLow != 0x40) {
        setError(F("Sensor is not an OV5640"));
        return false;
    }

    const uint32_t startedAt = millis();
    if (!loadFirmware()) {
        return false;
    }
    lastLoadMs_ = millis() - startedAt;
    available_ = true;
    return true;
}

bool Ov5640Af::isAvailable() const {
    return available_;
}

bool Ov5640Af::startContinuous() {
    return available_ && sendCommand(CMD_CONTINUOUS_FOCUS, 500);
}

bool Ov5640Af::focusOnce(uint32_t timeoutMs) {
    if (!available_) {
        return false;
    }
    if (!sendCommand(CMD_SINGLE_FOCUS, 500)) {
        return false;
    }
    return waitFocused(timeoutMs);
}

bool Ov5640Af::focusAt(uint8_t gridX, uint8_t gridY, uint32_t timeoutMs) {
    if (!available_) {
        return false;
    }

    // The touch zone is anchored at its top-left corner; center it on the tap.
    const int x0 = constrain(static_cast<int>(gridX) - TOUCH_ZONE_W / 2, 0, GRID_W - TOUCH_ZONE_W);
    const int y0 = constrain(static_cast<int>(gridY) - TOUCH_ZONE_H / 2, 0, GRID_H - TOUCH_ZONE_H);
    if (!writeReg(REG_CMD_PARA0, static_cast<uint8_t>(x0)) || !writeReg(REG_CMD_PARA1, static_cast<uint8_t>(y0))) {
        setError(F("AF zone write failed"));
        return false;
    }
    if (!sendCommand(CMD_TOUCH_ZONE, 500)) {
        return false;
    }
    return focusOnce(timeoutMs);
}

bool Ov5640Af::release() {
    return available_ && sendCommand(CMD_RELEASE_FOCUS, 500);
}

bool Ov5640Af::isFocused() {
    return available_ && readReg(REG_FW_STATUS) == FW_STATUS_FOCUSED;
}

uint32_t Ov5640Af::lastLoadMs() const {
    return lastLoadMs_;
}

const char* Ov5640Af::lastError() const {
    return lastError_.c_str();
}

bool Ov5640Af::loadFirmware() {
    // Hold the sensor MCU in reset while its program memory is written.
    if (!writeReg(REG_SYSTEM_RESET00, 0x20)) {
        setError(F("AF reset failed"));
        return false;
    }

    // The sensor auto-increments the register address, so the microcode goes
    // out in a few large writes instead of ~4000 single-register ones.
    bool burstOk = true;
    for (size_t offset = 0; offset < OV5640_AF_FIRMWARE_SIZE; offset += BURST_CHUNK) {
        const size_t length = min(BURST_CHUNK, OV5640_AF_FIRMWARE_SIZE - offset);
        if (!burstWrite(FIRMWARE_ADDR + offset, OV5640_AF_FIRMWARE + offset, length)) {
            burstOk = false;
            break;
        }
    }

    // Spot-check the upload and fall back to single writes if the module
    // does not accept sequential writes.
    const size_t probes[] = {0, 1, 255, 256, 2047, OV5640_AF_FIRMWARE_SIZE - 1};
    for (size_t probe : probes) {
        if (!burstOk) {
            break;
        }
        if (readReg(FIRMWARE_ADDR + probe) != OV5640_AF_FIRMWARE[probe]) {
            burstOk = false;
        }
    }
    if (!burstOk) {
        log_w("AF burst upload not verified, writing byte by byte");
        for (size_t offset = 0; offset < OV5640_AF_FIRMWARE_SIZE; ++offset) {
            if (!writeReg(FIRMWARE_ADDR + offset, OV5640_AF_FIRMWARE[offset])) {
                setError(F("AF firmware upload failed"));
                return false;
            }
        }
    }

    const uint16_t commandRegs[] = {REG_CMD_MAIN, REG_CMD_ACK, REG_CMD_PARA0, REG_CMD_PARA1, REG_CMD_PARA2, REG_CMD_PARA3, REG_CMD_PARA4};
    for (uint16_t reg : commandRegs) {
        writeReg(reg, 0x00);
    }
    writeReg(REG_FW_STATUS, 0x7F);
    writeReg(REG_SYSTEM_RESET00, 0x00);

    const uint32_t startedAt = millis();
    while (millis() - startedAt < 1500) {
        if (readReg(REG_FW_STATUS) == FW_STATUS_IDLE) {
            return true;
        }
        delay(5);
    }

    setError(F("AF firmware did not start"));
    return false;
}

bool Ov5640Af::burstWrite(uint16_t reg, const uint8_t* data, size_t length) {
    uint8_t buffer[2 + BURST_CHUNK];
    if (length > BURST_CHUNK) {
        return false;
    }
    buffer[0] = static_cast<uint8_t>(reg >> 8);
    buffer[1] = static_cast<uint8_t>(reg & 0xFF);
    memcpy_P(buffer + 2, data, length);
    return i2c_master_write_to_device(SCCB_PORT, sensor_->slv_addr, buffer, length + 2, pdMS_TO_TICKS(200)) == ESP_OK;
}

bool Ov5640Af::writeReg(uint16_t reg, uint8_t value) {
    const uint8_t buffer[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), value};
    return i2c_master_write_to_device(SCCB_PORT, sensor_->slv_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(50)) == ESP_OK;
}

int Ov5640Af::readReg(uint16_t reg) {
    return sensor_->get_reg(sensor_, reg, 0xFF);
}

bool Ov5640Af::sendCommand(uint8_t command, uint32_t timeoutMs) {
    // Handshake: mark the command pending, issue it, wait for the MCU to clear ACK.
    if (!writeReg(REG_CMD_ACK, 0x01) || !writeReg(REG_CMD_MAIN, command)) {
        setError(F("AF command write failed"));
        return false;
    }

    const uint32_t startedAt = millis();
    while (millis() - startedAt < timeoutMs) {
        if (readReg(REG_CMD_ACK) == 0x00) {
            return true;
        }
        delay(5);
    }

    setError(String(F("AF command timeout 0x")) + String(command, HEX));
    return false;
}

bool Ov5640Af::waitFocused(uint32_t timeoutMs) {
    const uint32_t startedAt = millis();
    while (millis() - startedAt < timeoutMs) {
        if (readReg(REG_FW_STATUS) == FW_STATUS_FOCUSED) {
            return true;
        }
        delay(10);
    }

    setError(F("AF did not lock"));
    return false;
}

void Ov5640Af::setError(const String& error) {
    lastError_ = error;
}
