#pragma once

#include <Arduino.h>
#include <vector>
#include "AppTypes.h"
#include "ChatManager.h"

struct PhotoAttachment {
    String label;
    String path;
};

// Talks to either an OpenAI-compatible /chat/completions endpoint or, when the
// base URL points at api.anthropic.com, the native Claude Messages API.
class AiClient {
public:
    static bool isAnthropicBaseUrl(const String& baseUrl);

    bool sendChat(const AppConfig& config, const std::vector<ChatMessage>& messages, String& assistantReply);
    bool sendChatWithImages(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply);
    const char* lastError() const;

private:
    String lastError_;

    bool checkPhotos(const std::vector<PhotoAttachment>& photos, size_t& totalBytes);
    bool sendOpenAi(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply);
    bool sendAnthropic(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply);
    String openAiUrl(String baseUrl) const;
    String anthropicUrl(String baseUrl) const;
    bool parseOpenAiReply(const String& payload, String& assistantReply);
    bool parseAnthropicReply(const String& payload, String& assistantReply);
    void setError(const __FlashStringHelper* error);
    void setError(const String& error);
};
