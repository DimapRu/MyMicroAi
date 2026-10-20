#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "ChatManager.h"

// Line-based JSON protocol over the USB serial port, used by tools/pc-chat.
// PC -> device: one JSON object per line, e.g. {"cmd":"send","text":"hi"}.
// Device -> PC: lines prefixed with "@mma " so they stand out from debug logs.
// It listens on Serial and, when Serial is not already the ESP32-S3 native
// USB port, on USBSerial too, and answers on whichever one the PC used.
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT && (CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3)
#define USB_BRIDGE_NATIVE_LINK 1
#else
#define USB_BRIDGE_NATIVE_LINK 0
#endif

enum class UsbCommandType : uint8_t {
    None,
    Hello,
    History,
    Send,
    NewChat
};

struct UsbCommand {
    UsbCommandType type = UsbCommandType::None;
    String text;
};

class UsbBridge {
public:
    static constexpr size_t LINE_MAX_BYTES = 6144;

    void begin();
    bool poll(UsbCommand& command);

    void sendState(const char* state, const String& chatTitle, const String& model);
    void sendHistory(const std::vector<ChatMessage>& messages, const String& chatTitle);
    void sendMessage(const String& role, const String& content);
    void sendStatus(const String& text);
    void sendError(const String& text);

private:
    struct Link {
        Stream* stream = nullptr;
        String line;
        bool overflow = false;
    };

    Link links_[2];
    size_t linkCount_ = 0;
    Stream* active_ = nullptr;

    bool readCommand(Link& link, UsbCommand& command);
    bool parseCommand(const String& line, UsbCommand& command);
    void emit(const JsonDocument& document);
};
