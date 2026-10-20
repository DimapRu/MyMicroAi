#include "CameraManager.h"
#include "BoardPins.h"
#include <SD.h>
#include <esp_err.h>

namespace {
static constexpr int PREVIEW_QUALITY = 14;
// Lower number = better JPEG. 12 keeps a detailed 1600x1200 shot (typically
// 150-300 KB) inside the driver's frame buffer, which is sized width*height/5.
static constexpr int CAPTURE_QUALITY = 12;
static constexpr uint32_t FOCUS_TIMEOUT_MS = 1800;
// A lock from a tap-to-focus is reused for a capture shortly after it.
static constexpr uint32_t FOCUS_LOCK_REUSE_MS = 8000;
}

bool CameraManager::begin() {
    if (ready_) {
        return true;
    }

    if (!psramFound()) {
        setError(F("Camera needs PSRAM"));
        return false;
    }

    pinMode(BoardPins::CAM_PWDN, OUTPUT);
    digitalWrite(BoardPins::CAM_PWDN, LOW);
    delay(20);

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_1;
    config.pin_d0 = BoardPins::CAM_Y2;
    config.pin_d1 = BoardPins::CAM_Y3;
    config.pin_d2 = BoardPins::CAM_Y4;
    config.pin_d3 = BoardPins::CAM_Y5;
    config.pin_d4 = BoardPins::CAM_Y6;
    config.pin_d5 = BoardPins::CAM_Y7;
    config.pin_d6 = BoardPins::CAM_Y8;
    config.pin_d7 = BoardPins::CAM_Y9;
    config.pin_xclk = BoardPins::CAM_XCLK;
    config.pin_pclk = BoardPins::CAM_PCLK;
    config.pin_vsync = BoardPins::CAM_VSYNC;
    config.pin_href = BoardPins::CAM_HREF;
    config.pin_sccb_sda = BoardPins::CAM_SIOD;
    config.pin_sccb_scl = BoardPins::CAM_SIOC;
    config.pin_pwdn = BoardPins::CAM_PWDN;
    config.pin_reset = BoardPins::CAM_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    // Frame buffers are allocated for the init size, so init at the largest
    // size we capture and switch down to the preview size afterwards.
    config.frame_size = CAPTURE_SIZE;
    config.jpeg_quality = CAPTURE_QUALITY;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        setError(String(F("Camera init failed 0x")) + String(static_cast<uint32_t>(err), HEX));
        return false;
    }

    ready_ = true;
    configureSensor();

    focusState_ = FocusState::Unavailable;
    focusLockedAtMs_ = 0;
    if (af_.begin(esp_camera_sensor_get())) {
        Serial.printf("OV5640 AF firmware loaded in %lu ms\n", static_cast<unsigned long>(af_.lastLoadMs()));
        if (af_.startContinuous()) {
            focusState_ = FocusState::Continuous;
        }
    } else {
        Serial.printf("Autofocus unavailable: %s\n", af_.lastError());
    }

    lastError_ = "";
    return true;
}

void CameraManager::end() {
    if (!ready_) {
        return;
    }

    esp_camera_deinit();
    ready_ = false;
    captureMode_ = false;
    focusState_ = FocusState::Unavailable;
    pinMode(BoardPins::CAM_PWDN, OUTPUT);
    digitalWrite(BoardPins::CAM_PWDN, HIGH);
    lastError_ = "";
}

bool CameraManager::isReady() const {
    return ready_;
}

camera_fb_t* CameraManager::capturePreviewFrame() {
    if (!setPreviewMode()) {
        return nullptr;
    }

    return captureFrame();
}

camera_fb_t* CameraManager::capturePhotoFrame() {
    if (!ready_ && !begin()) {
        return nullptr;
    }

    // Focus on the preview stream first: AF works on live frames and is
    // faster at the small size. A recent tap-to-focus lock is kept as is.
    const bool recentLock = focusState_ == FocusState::Locked && millis() - focusLockedAtMs_ < FOCUS_LOCK_REUSE_MS;
    if (af_.isAvailable() && !recentLock) {
        focusState_ = FocusState::Focusing;
        if (af_.focusOnce(FOCUS_TIMEOUT_MS)) {
            focusState_ = FocusState::Locked;
            focusLockedAtMs_ = millis();
        } else {
            // Still take the picture; a slightly soft photo beats none.
            focusState_ = FocusState::Failed;
        }
    }

    if (!setCaptureMode()) {
        return nullptr;
    }

    // Drop frames still in flight at the old size, then one more so exposure
    // settles after the mode switch.
    camera_fb_t* settle = captureFrameOfSize(CAPTURE_SIZE, 4);
    returnFrame(settle);
    return captureFrameOfSize(CAPTURE_SIZE, 3);
}

bool CameraManager::focusAt(uint8_t gridX, uint8_t gridY) {
    if (!ready_ || !af_.isAvailable()) {
        setError(F("Autofocus not available"));
        return false;
    }
    if (!setPreviewMode()) {
        return false;
    }

    focusState_ = FocusState::Focusing;
    if (!af_.focusAt(gridX, gridY, FOCUS_TIMEOUT_MS)) {
        focusState_ = FocusState::Failed;
        setError(af_.lastError());
        return false;
    }

    focusState_ = FocusState::Locked;
    focusLockedAtMs_ = millis();
    return true;
}

camera_fb_t* CameraManager::captureFrame() {
    if (!ready_ && !begin()) {
        return nullptr;
    }

    camera_fb_t* frame = esp_camera_fb_get();
    if (!frame) {
        setError(F("Camera frame failed"));
        return nullptr;
    }

    if (frame->format != PIXFORMAT_JPEG || frame->len == 0) {
        esp_camera_fb_return(frame);
        setError(F("Invalid JPEG frame"));
        return nullptr;
    }

    return frame;
}

camera_fb_t* CameraManager::captureFrameOfSize(framesize_t size, uint8_t maxAttempts) {
    const uint16_t expectedWidth = resolution[size].width;
    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
        camera_fb_t* frame = captureFrame();
        if (!frame) {
            return nullptr;
        }
        if (frame->width == expectedWidth) {
            return frame;
        }
        returnFrame(frame);
    }

    setError(F("Camera did not switch resolution"));
    return nullptr;
}

void CameraManager::returnFrame(camera_fb_t* frame) {
    if (frame) {
        esp_camera_fb_return(frame);
    }
}

bool CameraManager::saveFrameToFile(camera_fb_t* frame, const char* path) {
    if (!frame || !path) {
        setError(F("No frame to save"));
        return false;
    }

    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        setError(String(F("Open failed: ")) + path);
        return false;
    }

    const size_t written = file.write(frame->buf, frame->len);
    file.close();
    if (written != frame->len) {
        SD.remove(path);
        setError(F("Photo write failed"));
        return false;
    }

    return true;
}

FocusState CameraManager::focusState() const {
    return focusState_;
}

const char* CameraManager::focusError() const {
    return af_.lastError();
}

const char* CameraManager::lastError() const {
    return lastError_.c_str();
}

bool CameraManager::setPreviewMode() {
    if (!ready_ && !begin()) {
        return false;
    }

    if (!captureMode_) {
        return true;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) {
        setError(F("Camera sensor unavailable"));
        return false;
    }

    sensor->set_framesize(sensor, PREVIEW_SIZE);
    sensor->set_quality(sensor, PREVIEW_QUALITY);
    captureMode_ = false;
    return true;
}

bool CameraManager::setCaptureMode() {
    if (!ready_ && !begin()) {
        return false;
    }

    if (captureMode_) {
        return true;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) {
        setError(F("Camera sensor unavailable"));
        return false;
    }

    sensor->set_quality(sensor, CAPTURE_QUALITY);
    if (sensor->set_framesize(sensor, CAPTURE_SIZE) != 0) {
        setError(F("Capture resolution failed"));
        return false;
    }
    captureMode_ = true;
    return true;
}

void CameraManager::configureSensor() {
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor) {
        return;
    }

    sensor->set_vflip(sensor, 1);
    sensor->set_hmirror(sensor, 0);
    sensor->set_brightness(sensor, 1);
    sensor->set_contrast(sensor, 2);
    sensor->set_saturation(sensor, -2);
    sensor->set_sharpness(sensor, 2);
    sensor->set_denoise(sensor, 1);
    sensor->set_gainceiling(sensor, GAINCEILING_8X);
    sensor->set_whitebal(sensor, 1);
    sensor->set_awb_gain(sensor, 1);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_aec2(sensor, 1);
    sensor->set_quality(sensor, PREVIEW_QUALITY);
    sensor->set_framesize(sensor, PREVIEW_SIZE);
    captureMode_ = false;
}

void CameraManager::setError(const __FlashStringHelper* error) {
    lastError_ = String(error);
}

void CameraManager::setError(const String& error) {
    lastError_ = error;
}
