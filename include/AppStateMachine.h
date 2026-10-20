#pragma once

#include <Arduino.h>
#include <vector>
#include "AiClient.h"
#include "AppTypes.h"
#include "CameraManager.h"
#include "ChatManager.h"
#include "ConfigManager.h"
#include "DisplayManager.h"
#include "NetworkManager.h"
#include "StorageManager.h"
#include "UsbBridge.h"

class AppStateMachine {
public:
    void begin();
    void update();

private:
    AppMode mode_ = AppMode::Boot;
    AppError error_ = AppError::None;
    DisplayManager display_;
    StorageManager storage_;
    ConfigManager config_;
    NetworkManager network_;
    ChatManager chat_;
    AiClient ai_;
    CameraManager camera_;
    UsbBridge usb_;
    String inputText_;
    String workStatus_;
    String pendingUserText_;
    bool keyboardVisible_ = false;
    bool chatsMenuVisible_ = false;
    bool clearChatsConfirmVisible_ = false;
    bool cameraModeVisible_ = false;
    int chatsMenuOffset_ = 0;
    int32_t chatScrollOffset_ = 0;
    bool workUiDirty_ = false;
    bool wifiLost_ = false;
    uint32_t lastWifiRetryMs_ = 0;
    uint32_t lastSettingsTouchMs_ = 0;
    uint32_t lastCameraFrameMs_ = 0;
    uint16_t nextPhotoId_ = 1;
    std::vector<PhotoAttachment> pendingPhotos_;
    camera_fb_t* previewFrame_ = nullptr;

    void enterConfigurationMode();
    void enterWorkMode();
    void updateWorkMode();
    void updateErrorMode();
    void watchWifi();
    void redrawWorkMode();
    void redrawCameraPreview(bool forceFrame);
    void enterCameraMode();
    void exitCameraMode();
    void capturePhotoToQueue();
    void cleanupPendingPhotos();
    void sendCurrentMessage();
    bool runChatTurn(const String& userText, const std::vector<PhotoAttachment>& photos);
    void handleUsb();
    void handleUsbSend(const String& text);
    void announceState();
    void sendChatToUsb();
    String activeChatTitle() const;
    void enterError(AppError error, const char* message);
};
