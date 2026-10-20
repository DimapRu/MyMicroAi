#pragma once

#include <Arduino.h>
#include "esp_camera.h"
#include "Ov5640Af.h"

enum class FocusState : uint8_t {
    Unavailable,
    Continuous,
    Focusing,
    Locked,
    Failed
};

class CameraManager {
public:
    // Preview is small so it decodes fast; photos are sized for vision models
    // (Claude reads up to 2576 px on the long edge, so 1600x1200 keeps detail
    // without making uploads slow).
    static constexpr framesize_t PREVIEW_SIZE = FRAMESIZE_QVGA;
    static constexpr framesize_t CAPTURE_SIZE = FRAMESIZE_UXGA;

    bool begin();
    void end();
    bool isReady() const;
    camera_fb_t* capturePreviewFrame();
    camera_fb_t* capturePhotoFrame();
    bool focusAt(uint8_t gridX, uint8_t gridY);
    void returnFrame(camera_fb_t* frame);
    bool saveFrameToFile(camera_fb_t* frame, const char* path);
    FocusState focusState() const;
    const char* focusError() const;
    const char* lastError() const;

private:
    bool ready_ = false;
    bool captureMode_ = false;
    Ov5640Af af_;
    FocusState focusState_ = FocusState::Unavailable;
    uint32_t focusLockedAtMs_ = 0;
    String lastError_;

    bool setPreviewMode();
    bool setCaptureMode();
    camera_fb_t* captureFrame();
    camera_fb_t* captureFrameOfSize(framesize_t size, uint8_t maxAttempts);
    void configureSensor();
    void setError(const __FlashStringHelper* error);
    void setError(const String& error);
};
