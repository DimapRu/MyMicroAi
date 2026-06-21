#pragma once

#include <Arduino.h>
#include "esp_camera.h"

class CameraManager {
public:
    bool begin();
    void end();
    bool isReady() const;
    camera_fb_t* capturePreviewFrame();
    camera_fb_t* capturePhotoFrame();
    void returnFrame(camera_fb_t* frame);
    bool saveFrameToFile(camera_fb_t* frame, const char* path);
    const char* lastError() const;

private:
    bool ready_ = false;
    bool captureMode_ = false;
    String lastError_;

    bool setPreviewMode();
    bool setCaptureMode();
    camera_fb_t* captureFrame();
    void configureSensor();
    void setError(const __FlashStringHelper* error);
    void setError(const String& error);
};
