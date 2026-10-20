#include "UsbBridge.h"

namespace {
static constexpr const char* LINE_PREFIX = "@mma ";
static constexpr uint8_t PROTOCOL_VERSION = 1;
}

void UsbBridge::begin() {
    linkCount_ = 0;
    links_[linkCount_].stream = &Serial;
    links_[linkCount_++].line.reserve(256);
#if USB_BRIDGE_NATIVE_LINK
    USBSerial.setRxBufferSize(LINE_MAX_BYTES + 1024);
    USBSerial.begin();
    links_[linkCount_].stream = &USBSerial;
    links_[linkCount_++].line.reserve(256);
#endif
    active_ = &Serial;
}

bool UsbBridge::poll(UsbCommand& command) {
    command = UsbCommand{};
    for (size_t index = 0; index < linkCount_; ++index) {
        if (readCommand(links_[index], command)) {
            return true;
        }
    }
    return false;
}

bool UsbBridge::readCommand(Link& link, UsbCommand& command) {
    while (link.stream->available() > 0) {
        const int value = link.stream->read();
        if (value < 0) {
            break;
        }

        const char current = static_cast<char>(value);
        if (current == '\r') {
            continue;
        }
        if (current != '\n') {
            if (link.line.length() < LINE_MAX_BYTES) {
                link.line += current;
            } else {
                link.overflow = true;
            }
            continue;
        }

        String line = link.line;
        link.line = "";
        line.trim();
        // Anything that is not a JSON object is ignored, e.g. a terminal user typing.
        if (!line.startsWith("{") && !link.overflow) {
            continue;
        }

        active_ = link.stream;
        if (link.overflow) {
            link.overflow = false;
            sendError(F("Message is too long"));
            continue;
        }
        if (parseCommand(line, command)) {
            return true;
        }
    }

    return false;
}

void UsbBridge::sendState(const char* state, const String& chatTitle, const String& model) {
    JsonDocument document;
    document["type"] = "hello";
    document["proto"] = PROTOCOL_VERSION;
    document["state"] = state;
    document["chat"] = chatTitle;
    document["model"] = model;
    emit(document);
}

void UsbBridge::sendHistory(const std::vector<ChatMessage>& messages, const String& chatTitle) {
    JsonDocument document;
    document["type"] = "history_begin";
    document["chat"] = chatTitle;
    emit(document);

    // One line per message keeps peak RAM low for long chats.
    for (const ChatMessage& message : messages) {
        sendMessage(message.role, message.content);
    }

    document.clear();
    document["type"] = "history_end";
    emit(document);
}

void UsbBridge::sendMessage(const String& role, const String& content) {
    JsonDocument document;
    document["type"] = "message";
    document["role"] = role;
    document["content"] = content;
    emit(document);
}

void UsbBridge::sendStatus(const String& text) {
    JsonDocument document;
    document["type"] = "status";
    document["text"] = text;
    emit(document);
}

void UsbBridge::sendError(const String& text) {
    JsonDocument document;
    document["type"] = "error";
    document["text"] = text;
    emit(document);
}

bool UsbBridge::parseCommand(const String& line, UsbCommand& command) {
    JsonDocument document;
    if (deserializeJson(document, line)) {
        sendError(F("Invalid JSON command"));
        return false;
    }

    const String name = document["cmd"] | "";
    if (name == F("hello")) {
        command.type = UsbCommandType::Hello;
    } else if (name == F("history")) {
        command.type = UsbCommandType::History;
    } else if (name == F("send")) {
        command.type = UsbCommandType::Send;
        command.text = document["text"] | "";
    } else if (name == F("new_chat")) {
        command.type = UsbCommandType::NewChat;
    } else {
        sendError(String(F("Unknown command: ")) + name);
        return false;
    }
    return true;
}

void UsbBridge::emit(const JsonDocument& document) {
    // serializeJson() replaces a String's contents, so encode first, then prefix.
    String json;
    serializeJson(document, json);
    String line;
    line.reserve(json.length() + 8);
    line += LINE_PREFIX;
    line += json;
    line += '\n';
    // A single write keeps the line from being split by other log output.
    active_->print(line);
}
