#pragma once

#include <Arduino.h>
#include <vector>
#include "StorageManager.h"

static constexpr size_t CHAT_MAX_MESSAGES = 16;
static constexpr size_t CHAT_MAX_SESSIONS = 12;
static constexpr size_t CHAT_MAX_MESSAGE_TEXT = 900;

struct ChatMessage {
    String role;
    String content;
};

struct ChatSessionInfo {
    String id;
    String title;
    uint32_t updatedAt = 0;
};

class ChatManager {
public:
    bool begin(StorageManager& storage);
    bool createChat();
    bool clearAllChats();
    bool switchChat(int index);
    bool appendMessage(const char* role, const String& content);
    bool loadMessages();

    const std::vector<ChatSessionInfo>& sessions() const;
    const std::vector<ChatMessage>& messages() const;
    int activeIndex() const;
    const String& activeChatId() const;
    const char* lastError() const;

private:
    StorageManager* storage_ = nullptr;
    std::vector<ChatSessionInfo> sessions_;
    std::vector<ChatMessage> messages_;
    String activeId_;
    uint32_t nextId_ = 1;
    String lastError_;

    bool loadIndex();
    bool saveIndex();
    bool ensureDefaultChat();
    bool parseJsonLine(const String& line, ChatMessage& message) const;
    String chatPath(const String& id) const;
    String makeChatId(uint32_t number) const;
    String makeTitleFromText(const String& text) const;
    int findSessionIndex(const String& id) const;
    void trimLoadedMessages();
    void setError(const __FlashStringHelper* error);
    void setError(const String& error);
};
