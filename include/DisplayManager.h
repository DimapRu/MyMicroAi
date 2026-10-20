#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <vector>
#include "esp_camera.h"
#include "AppTypes.h"
#include "CameraManager.h"
#include "ChatManager.h"

class MyMicroAIDisplay : public lgfx::LGFX_Device {
public:
    MyMicroAIDisplay();

private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

enum class ChatUiActionType : uint8_t {
    None,
    Settings,
    NewChat,
    OpenChats,
    CloseChats,
    ChatsUp,
    ChatsDown,
    SelectChat,
    OpenClearChatsConfirm,
    CancelClearChats,
    ConfirmClearChats,
    OpenKeyboard,
    CloseKeyboard,
    Send,
    OpenCamera,
    CloseCamera,
    CapturePhoto,
    FocusAt,
    Backspace,
    Character,
    LayoutSwitch,
    ScrollUp,
    ScrollDown
};

struct ChatUiAction {
    ChatUiActionType type = ChatUiActionType::None;
    String text;
    int index = -1;
    int16_t x = -1;
    int16_t y = -1;
};

enum class ErrorUiAction : uint8_t {
    None,
    Retry,
    Setup
};

enum class KeyboardLayout : uint8_t {
    English,
    Russian,
    Numbers
};

class DisplayManager {
public:
    bool begin();
    bool isReady() const;
    void showBoot(const char* message);
    void showConfigurationMode(const String& address);
    void showWorkMode(const String& address);
    void showChatWorkMode(const std::vector<ChatSessionInfo>& sessions,
                          int activeIndex,
                          const std::vector<ChatMessage>& messages,
                          const String& inputText,
                          bool keyboardVisible,
                          const String& statusText,
                          int32_t scrollOffset,
                          bool chatsMenuVisible,
                          int chatsMenuOffset,
                          bool clearChatsConfirmVisible);
    void showCameraPreview(camera_fb_t* frame, size_t queuedPhotos, const String& statusText, FocusState focus, bool fullRedraw);
    void setFocusMarker(int16_t x, int16_t y);
    void clearFocusMarker();
    // Maps a screen point inside the camera preview to the sensor's AF grid.
    bool previewPointToFocusGrid(int16_t x, int16_t y, uint8_t& gridX, uint8_t& gridY) const;
    void showError(AppError error, const char* message);
    void showStatus(const char* title, const char* message);
    void refreshKeyboardPreview(const String& inputText);
    bool pollChatAction(ChatUiAction& action);
    ErrorUiAction pollErrorAction();
    int32_t chatMaxScroll() const;
    bool wasWorkSettingsButtonPressed();

private:
    MyMicroAIDisplay display_;
    bool ready_ = false;
    bool touchReady_ = false;
    bool workButtonVisible_ = false;
    bool chatKeyboardVisible_ = false;
    bool chatsMenuVisible_ = false;
    bool clearChatsConfirmVisible_ = false;
    bool cameraPreviewVisible_ = false;
    bool errorButtonsVisible_ = false;
    int16_t focusMarkerX_ = -1;
    int16_t focusMarkerY_ = -1;
    int32_t chatMaxScroll_ = 0;
    int chatsMenuOffset_ = 0;
    KeyboardLayout keyboardLayout_ = KeyboardLayout::English;
    bool touchHeld_ = false;
    uint32_t lastWorkButtonPressMs_ = 0;

    bool readTouchPoint(uint16_t& x, uint16_t& y);
    bool consumeTouch(uint16_t& x, uint16_t& y, uint32_t debounceMs);
    void drawHeader(const char* title, uint16_t color);
    void drawWorkSettingsButton();
    void drawChatsMenu(const std::vector<ChatSessionInfo>& sessions, int activeIndex, int menuOffset);
    void drawClearChatsConfirm();
    void drawChatKeyboard(const String& inputText);
    void drawKeyboardPreview(const String& inputText);
    void drawWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight);
    void drawWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight, int32_t clipTop, int32_t clipBottom);
};
