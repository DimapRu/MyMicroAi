#include "CameraManager.h"
#include "BoardPins.h"
#include <SD.h>
#include <esp_err.h>

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
    config.frame_size = FRAMESIZE_HVGA;
    config.jpeg_quality = 18;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        setError(String(F("Camera init failed 0x")) + String(static_cast<uint32_t>(err), HEX));
        return false;
    }

    ready_ = true;
    configureSensor();
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
    if (!setCaptureMode()) {
        return nullptr;
    }

    camera_fb_t* warmup = captureFrame();
    returnFrame(warmup);
    delay(80);
    return captureFrame();
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

    sensor->set_quality(sensor, 18);
    captureMode_ = false;
    delay(60);
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

    sensor->set_quality(sensor, 10);
    captureMode_ = true;
    delay(140);
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
    sensor->set_quality(sensor, 18);
    sensor->set_framesize(sensor, FRAMESIZE_HVGA);
    captureMode_ = false;
}

void CameraManager::setError(const __FlashStringHelper* error) {
    lastError_ = String(error);
}

void CameraManager::setError(const String& error) {
    lastError_ = error;
}
