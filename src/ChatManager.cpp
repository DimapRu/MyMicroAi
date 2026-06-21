#include "ChatManager.h"
#include <ArduinoJson.h>

namespace {
static constexpr const char* CHAT_DIR = "/chats";
static constexpr const char* CHAT_INDEX_PATH = "/chats/index.json";
static constexpr size_t CHAT_INDEX_MAX_BYTES = 8192;
static constexpr size_t CHAT_FILE_MAX_BYTES = 32768;

String jsonLineEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t index = 0; index < value.length(); ++index) {
        const char current = value[index];
        switch (current) {
            case '\\': escaped += F("\\\\"); break;
            case '"': escaped += F("\\\""); break;
            case '\n': escaped += F("\\n"); break;
            case '\r': escaped += F("\\r"); break;
            case '\t': escaped += F("\\t"); break;
            default: escaped += current; break;
        }
    }
    return escaped;
}
}

bool ChatManager::begin(StorageManager& storage) {
    storage_ = &storage;
    sessions_.clear();
    messages_.clear();
    activeId_ = "";
    nextId_ = 1;
    lastError_ = "";

    if (!storage_->isReady()) {
        setError(F("Storage is not ready"));
        return false;
    }

    if (!storage_->ensureDirectory(CHAT_DIR)) {
        setError(F("Failed to create chat directory"));
        return false;
    }

    if (!loadIndex()) {
        return false;
    }

    if (!ensureDefaultChat()) {
        return false;
    }

    return loadMessages();
}

bool ChatManager::createChat() {
    if (!storage_) {
        setError(F("Chat storage is not initialized"));
        return false;
    }

    if (sessions_.size() >= CHAT_MAX_SESSIONS) {
        setError(F("Chat session limit reached"));
        return false;
    }

    ChatSessionInfo session;
    session.id = makeChatId(nextId_++);
    session.title = F("New chat");
    session.updatedAt = millis();
    sessions_.insert(sessions_.begin(), session);
    activeId_ = session.id;
    messages_.clear();

    const String path = chatPath(activeId_);
    if (!storage_->writeTextFileAtomic(path.c_str(), "")) {
        setError(F("Failed to create chat file"));
        return false;
    }

    return saveIndex();
}

bool ChatManager::clearAllChats() {
    if (!storage_) {
        setError(F("Chat storage is not initialized"));
        return false;
    }

    for (const ChatSessionInfo& session : sessions_) {
        const String path = chatPath(session.id);
        if (storage_->exists(path.c_str()) && !storage_->remove(path.c_str())) {
            setError(String(F("Failed to remove ")) + path);
            return false;
        }
    }

    if (storage_->exists(CHAT_INDEX_PATH) && !storage_->remove(CHAT_INDEX_PATH)) {
        setError(F("Failed to remove chat index"));
        return false;
    }

    sessions_.clear();
    messages_.clear();
    activeId_ = "";
    nextId_ = 1;

    if (!ensureDefaultChat()) {
        return false;
    }

    return loadMessages();
}

bool ChatManager::switchChat(int index) {
    if (index < 0 || static_cast<size_t>(index) >= sessions_.size()) {
        setError(F("Invalid chat index"));
        return false;
    }

    activeId_ = sessions_[index].id;
    return loadMessages();
}

bool ChatManager::appendMessage(const char* role, const String& content) {
    if (!storage_ || activeId_.length() == 0) {
        setError(F("No active chat"));
        return false;
    }

    String clipped = content;
    if (clipped.length() > CHAT_MAX_MESSAGE_TEXT) {
        clipped = clipped.substring(0, CHAT_MAX_MESSAGE_TEXT);
    }

    String line;
    line.reserve(clipped.length() + 48);
    line += F("{\"role\":\"");
    line += jsonLineEscape(String(role));
    line += F("\",\"content\":\"");
    line += jsonLineEscape(clipped);
    line += F("\"}\n");

    const String path = chatPath(activeId_);
    if (!storage_->appendTextFile(path.c_str(), line)) {
        setError(F("Failed to append chat message"));
        return false;
    }

    ChatMessage message;
    message.role = role;
    message.content = clipped;
    messages_.push_back(message);
    trimLoadedMessages();

    const int sessionIndex = findSessionIndex(activeId_);
    if (sessionIndex >= 0) {
        sessions_[sessionIndex].updatedAt = millis();
        if (sessions_[sessionIndex].title == F("New chat") && String(role) == F("user")) {
            sessions_[sessionIndex].title = makeTitleFromText(clipped);
        }
    }

    return saveIndex();
}

bool ChatManager::loadMessages() {
    messages_.clear();
    if (!storage_ || activeId_.length() == 0) {
        setError(F("No active chat"));
        return false;
    }

    const String path = chatPath(activeId_);
    if (!storage_->exists(path.c_str())) {
        return storage_->writeTextFileAtomic(path.c_str(), "");
    }

    String content;
    if (!storage_->readTextFile(path.c_str(), content, CHAT_FILE_MAX_BYTES)) {
        setError(F("Failed to read chat file"));
        return false;
    }

    int start = 0;
    while (start < static_cast<int>(content.length())) {
        const int end = content.indexOf('\n', start);
        String line = end >= 0 ? content.substring(start, end) : content.substring(start);
        line.trim();
        if (line.length() > 0) {
            ChatMessage message;
            if (parseJsonLine(line, message)) {
                messages_.push_back(message);
            }
        }
        if (end < 0) {
            break;
        }
        start = end + 1;
    }

    trimLoadedMessages();
    return true;
}

const std::vector<ChatSessionInfo>& ChatManager::sessions() const {
    return sessions_;
}

const std::vector<ChatMessage>& ChatManager::messages() const {
    return messages_;
}

int ChatManager::activeIndex() const {
    return findSessionIndex(activeId_);
}

const String& ChatManager::activeChatId() const {
    return activeId_;
}

const char* ChatManager::lastError() const {
    return lastError_.c_str();
}

bool ChatManager::loadIndex() {
    sessions_.clear();
    activeId_ = "";
    nextId_ = 1;

    if (!storage_->exists(CHAT_INDEX_PATH)) {
        return true;
    }

    String content;
    if (!storage_->readTextFile(CHAT_INDEX_PATH, content, CHAT_INDEX_MAX_BYTES)) {
        setError(F("Failed to read chat index"));
        return false;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, content);
    if (error) {
        setError(String(F("Invalid chat index: ")) + error.c_str());
        return false;
    }

    activeId_ = document["active_id"] | "";
    nextId_ = document["next_id"] | 1;

    JsonArray chats = document["chats"].as<JsonArray>();
    for (JsonObject item : chats) {
        if (sessions_.size() >= CHAT_MAX_SESSIONS) {
            break;
        }
        ChatSessionInfo session;
        session.id = item["id"] | "";
        session.title = item["title"] | "New chat";
        session.updatedAt = item["updated"] | 0;
        if (session.id.length() > 0) {
            sessions_.push_back(session);
        }
    }

    return true;
}

bool ChatManager::saveIndex() {
    JsonDocument document;
    document["active_id"] = activeId_;
    document["next_id"] = nextId_;
    JsonArray chats = document["chats"].to<JsonArray>();

    for (const ChatSessionInfo& session : sessions_) {
        JsonObject item = chats.add<JsonObject>();
        item["id"] = session.id;
        item["title"] = session.title;
        item["updated"] = session.updatedAt;
    }

    String content;
    serializeJson(document, content);
    if (!storage_->writeTextFileAtomic(CHAT_INDEX_PATH, content)) {
        setError(F("Failed to save chat index"));
        return false;
    }

    return true;
}

bool ChatManager::ensureDefaultChat() {
    if (sessions_.empty()) {
        return createChat();
    }

    if (findSessionIndex(activeId_) < 0) {
        activeId_ = sessions_[0].id;
        return saveIndex();
    }

    return true;
}

bool ChatManager::parseJsonLine(const String& line, ChatMessage& message) const {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, line);
    if (error) {
        return false;
    }

    message.role = document["role"] | "assistant";
    message.content = document["content"] | "";
    return message.content.length() > 0;
}

String ChatManager::chatPath(const String& id) const {
    return String(CHAT_DIR) + "/" + id + ".jsonl";
}

String ChatManager::makeChatId(uint32_t number) const {
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "chat_%04lu", static_cast<unsigned long>(number));
    return String(buffer);
}

String ChatManager::makeTitleFromText(const String& text) const {
    String title = text;
    title.replace('\n', ' ');
    title.trim();
    if (title.length() == 0) {
        return F("New chat");
    }
    if (title.length() > 22) {
        title = title.substring(0, 22) + "...";
    }
    return title;
}

int ChatManager::findSessionIndex(const String& id) const {
    for (size_t index = 0; index < sessions_.size(); ++index) {
        if (sessions_[index].id == id) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ChatManager::trimLoadedMessages() {
    while (messages_.size() > CHAT_MAX_MESSAGES) {
        messages_.erase(messages_.begin());
    }
}

void ChatManager::setError(const __FlashStringHelper* error) {
    lastError_ = String(error);
}

void ChatManager::setError(const String& error) {
    lastError_ = error;
}
