#pragma once

#include <Arduino.h>
#include <vector>
#include "AppTypes.h"
#include "ChatManager.h"

struct PhotoAttachment {
    String label;
    String path;
};

class AiClient {
public:
    bool sendChat(const AppConfig& config, const std::vector<ChatMessage>& messages, String& assistantReply);
    bool sendChatWithImages(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply);
    const char* lastError() const;

private:
    String lastError_;

    String completionsUrl(String baseUrl) const;
    bool parseReply(const String& payload, String& assistantReply);
    void setError(const __FlashStringHelper* error);
    void setError(const String& error);
};
