#include "AiClient.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include "LogManager.h"
#include "TlsConfig.h"
#include "mbedtls/base64.h"

namespace {
static constexpr uint16_t AI_TIMEOUT_MS = 60000;
static constexpr size_t AI_RESPONSE_MAX_BYTES = 24576;
// The camera's JPEG frame buffer for 1600x1200 is 384000 bytes.
static constexpr size_t AI_MAX_IMAGE_BYTES = 400000;
static constexpr size_t AI_MAX_CONTEXT_MESSAGES = 10;
static constexpr uint16_t OPENAI_MAX_TOKENS = 1024;
// Claude counts its thinking toward max_tokens, so leave room beyond the reply.
static constexpr uint16_t ANTHROPIC_MAX_TOKENS = 4096;
static constexpr const char* ANTHROPIC_VERSION = "2023-06-01";
static constexpr const char* ANTHROPIC_FALLBACK_BETA = "server-side-fallback-2026-07-01";
static constexpr const char* IMAGE_PLACEHOLDER = "@@MMA_IMAGE_";

static const char SYSTEM_PROMPT[] PROGMEM =
    "You are MyMicroAI, a concise helpful assistant running on a tiny ESP32 handheld. "
    "Reply in the same language the user uses. If the user writes in Russian, answer in natural Russian. "
    "Keep replies short because the display is very small.";
static const char SYSTEM_PROMPT_IMAGES[] PROGMEM =
    " Photos from the device camera are attached and labeled photo1, photo2 and so on, matching the "
    "labels in the user's text. Inspect them carefully and read visible text or numbers.";

String trimSlash(String value) {
    value.trim();
    while (value.endsWith("/")) {
        value.remove(value.length() - 1);
    }
    return value;
}

String systemPrompt(bool withImages) {
    String prompt = FPSTR(SYSTEM_PROMPT);
    if (withImages) {
        prompt += FPSTR(SYSTEM_PROMPT_IMAGES);
    }
    return prompt;
}

// serializeJson() replaces a String's contents (ArduinoJson 7), so encode
// into a scratch string and append that.
void appendJsonString(String& out, const String& value) {
    JsonDocument scalar;
    scalar.set(value);
    String encoded;
    serializeJson(scalar, encoded);
    out += encoded;
}

size_t contextStart(const std::vector<ChatMessage>& messages) {
    return messages.size() > AI_MAX_CONTEXT_MESSAGES ? messages.size() - AI_MAX_CONTEXT_MESSAGES : 0;
}

bool startsWithAny(const String& value, const char* const* prefixes, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (value.startsWith(prefixes[index])) {
            return true;
        }
    }
    return false;
}

// output_config.effort errors on older models (Sonnet 4.5, Haiku 4.5, Claude 3).
bool claudeSupportsEffort(const String& model) {
    static const char* const prefixes[] = {
        "claude-opus-4-5", "claude-opus-4-6", "claude-opus-4-7", "claude-opus-4-8", "claude-opus-5",
        "claude-sonnet-4-6", "claude-sonnet-5", "claude-haiku-5", "claude-fable", "claude-mythos"};
    return startsWithAny(model, prefixes, sizeof(prefixes) / sizeof(prefixes[0]));
}

// Server-side refusal fallback ("fallbacks": "default") for models that support it.
bool claudeSupportsDefaultFallback(const String& model) {
    return model == F("claude-fable-5-1") || model == F("claude-opus-5-5") || model == F("claude-opus-5") || model == F("claude-sonnet-5-5");
}

bool appendFileBase64(const String& path, String& out) {
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) {
        return false;
    }

    const size_t fileSize = file.size();
    if (fileSize == 0 || fileSize > AI_MAX_IMAGE_BYTES) {
        file.close();
        return false;
    }

    uint8_t* raw = static_cast<uint8_t*>(ps_malloc(fileSize));
    if (!raw) {
        file.close();
        return false;
    }

    const size_t readBytes = file.read(raw, fileSize);
    file.close();
    if (readBytes != fileSize) {
        free(raw);
        return false;
    }

    const size_t encodedSize = 4 * ((fileSize + 2) / 3) + 1;
    char* encoded = static_cast<char*>(ps_malloc(encodedSize));
    if (!encoded) {
        free(raw);
        return false;
    }

    size_t written = 0;
    const int err = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded), encodedSize, &written, raw, fileSize);
    free(raw);
    if (err != 0) {
        free(encoded);
        return false;
    }

    out.concat(encoded, written);
    free(encoded);
    return true;
}

String summarizePayload(const String& payload) {
    String summary;
    summary.reserve(160);
    summary += F("payload_bytes=");
    summary += payload.length();
    summary += F(" image_parts=");

    int imageParts = 0;
    int searchFrom = 0;
    while (true) {
        const int foundOpenAi = payload.indexOf(F("data:image/jpeg;base64,"), searchFrom);
        const int foundClaude = payload.indexOf(F("\"media_type\":\"image/jpeg\""), searchFrom);
        int found = foundOpenAi;
        if (found < 0 || (foundClaude >= 0 && foundClaude < found)) {
            found = foundClaude;
        }
        if (found < 0) {
            break;
        }
        ++imageParts;
        searchFrom = found + 1;
    }

    summary += imageParts;
    return summary;
}

// Pulls a readable message out of an error body: {"error":{"message":...}} is
// used by both OpenAI-compatible servers and the Anthropic API.
String describeHttpError(int httpCode, const String& response) {
    String message = F("AI HTTP ");
    message += httpCode;

    JsonDocument filter;
    filter["error"]["message"] = true;
    JsonDocument document;
    if (!deserializeJson(document, response, DeserializationOption::Filter(filter))) {
        const char* detail = document["error"]["message"] | "";
        if (strlen(detail) > 0) {
            message += F(": ");
            message += String(detail).substring(0, 180);
            return message;
        }
    }

    if (response.length() > 0) {
        message += F(": ");
        message += response.substring(0, 180);
    }
    return message;
}

enum class ApiKind : uint8_t {
    OpenAi,
    Anthropic
};

bool postPayload(const AppConfig& config, ApiKind kind, bool fallbackBeta, const String& url, const String& payload, String& response, String& errorOut) {
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool httpStarted = false;

    if (url.startsWith("https://")) {
        configureTls(secureClient);
        httpStarted = http.begin(secureClient, url);
    } else {
        httpStarted = http.begin(plainClient, url);
    }

    if (!httpStarted) {
        errorOut = F("Failed to start AI HTTP request");
        LogManager::append(F("AI HTTP START FAILED"), String(F("url=")) + url + '\n' + summarizePayload(payload));
        return false;
    }

    http.setTimeout(AI_TIMEOUT_MS);
    if (kind == ApiKind::Anthropic) {
        http.addHeader("x-api-key", config.apiKey);
        http.addHeader("anthropic-version", ANTHROPIC_VERSION);
        if (fallbackBeta) {
            http.addHeader("anthropic-beta", ANTHROPIC_FALLBACK_BETA);
        }
    } else {
        http.addHeader("Authorization", String("Bearer ") + config.apiKey);
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    const int httpCode = http.POST(payload);
    response = http.getString();
    http.end();

    if (httpCode < 200 || httpCode >= 300) {
        String logMessage;
        logMessage.reserve(response.length() + 256);
        logMessage += F("url=");
        logMessage += url;
        logMessage += '\n';
        logMessage += F("http_code=");
        logMessage += httpCode;
        logMessage += '\n';
        logMessage += summarizePayload(payload);
        logMessage += '\n';
        logMessage += F("response=\n");
        logMessage += response;
        LogManager::append(F("AI HTTP ERROR"), logMessage);

        if (httpCode < 0) {
            // Negative codes are transport failures: DNS, TCP or a rejected TLS certificate.
            errorOut = F("Connection failed: ");
            errorOut += HTTPClient::errorToString(httpCode);
            return false;
        }

        errorOut = describeHttpError(httpCode, response);
        return false;
    }

    if (response.length() > AI_RESPONSE_MAX_BYTES) {
        LogManager::append(F("AI RESPONSE TOO LARGE"), String(F("bytes=")) + response.length() + '\n' + response.substring(0, 1024));
        errorOut = F("AI response is too large");
        return false;
    }

    return true;
}
}

bool AiClient::isAnthropicBaseUrl(const String& baseUrl) {
    String lower = baseUrl;
    lower.toLowerCase();
    return lower.indexOf(F("api.anthropic.com")) >= 0;
}

bool AiClient::sendChat(const AppConfig& config, const std::vector<ChatMessage>& messages, String& assistantReply) {
    return sendChatWithImages(config, messages, std::vector<PhotoAttachment>(), assistantReply);
}

bool AiClient::sendChatWithImages(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply) {
    assistantReply = "";
    lastError_ = "";

    if (!config.isComplete()) {
        setError(F("AI config is incomplete"));
        return false;
    }

    if (messages.empty()) {
        setError(F("No messages to send"));
        return false;
    }

    if (isAnthropicBaseUrl(config.apiBaseUrl)) {
        return sendAnthropic(config, messages, photos, assistantReply);
    }
    return sendOpenAi(config, messages, photos, assistantReply);
}

const char* AiClient::lastError() const {
    return lastError_.c_str();
}

bool AiClient::checkPhotos(const std::vector<PhotoAttachment>& photos, size_t& totalBytes) {
    totalBytes = 0;
    for (const PhotoAttachment& photo : photos) {
        File file = SD.open(photo.path.c_str(), FILE_READ);
        if (!file) {
            setError(String(F("Photo read failed: ")) + photo.label);
            return false;
        }
        const size_t fileSize = file.size();
        file.close();
        if (fileSize == 0 || fileSize > AI_MAX_IMAGE_BYTES) {
            setError(String(F("Photo too large: ")) + photo.label);
            return false;
        }
        totalBytes += fileSize;
    }
    return true;
}

bool AiClient::sendOpenAi(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply) {
    size_t imageBytes = 0;
    if (!checkPhotos(photos, imageBytes)) {
        return false;
    }

    // Built by hand so base64 photos stream straight into one PSRAM buffer
    // instead of being copied through a JsonDocument.
    String payload;
    payload.reserve(4096 + (4 * ((imageBytes + 2) / 3)) + photos.size() * 128);
    payload += F("{\"model\":");
    appendJsonString(payload, config.modelName);
    payload += F(",\"temperature\":0.7,\"max_tokens\":");
    payload += OPENAI_MAX_TOKENS;
    payload += F(",\"messages\":[{\"role\":\"system\",\"content\":");
    appendJsonString(payload, systemPrompt(!photos.empty()));
    payload += '}';

    const size_t lastIndex = messages.size() - 1;
    for (size_t index = contextStart(messages); index < messages.size(); ++index) {
        const ChatMessage& message = messages[index];
        payload += F(",{\"role\":");
        appendJsonString(payload, message.role);
        payload += F(",\"content\":");

        if (index == lastIndex && message.role == F("user") && !photos.empty()) {
            payload += '[';
            for (const PhotoAttachment& photo : photos) {
                payload += F("{\"type\":\"text\",\"text\":");
                appendJsonString(payload, photo.label + ':');
                payload += F("},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,");
                if (!appendFileBase64(photo.path, payload)) {
                    setError(String(F("Photo read failed: ")) + photo.label);
                    return false;
                }
                payload += F("\"}},");
            }
            payload += F("{\"type\":\"text\",\"text\":");
            appendJsonString(payload, message.content.length() > 0 ? message.content : String(F("Please analyze the attached photo.")));
            payload += F("}]");
        } else {
            appendJsonString(payload, message.content);
        }
        payload += '}';
    }
    payload += F("]}");

    String response;
    String error;
    if (!postPayload(config, ApiKind::OpenAi, false, openAiUrl(config.apiBaseUrl), payload, response, error)) {
        setError(error);
        return false;
    }
    return parseOpenAiReply(response, assistantReply);
}

bool AiClient::sendAnthropic(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply) {
    size_t imageBytes = 0;
    if (!checkPhotos(photos, imageBytes)) {
        return false;
    }

    // The request is laid out with ArduinoJson, with a placeholder where each
    // photo goes; the base64 data is spliced in afterwards so the large image
    // bytes never pass through the JSON document.
    JsonDocument request;
    request["model"] = config.modelName;
    request["max_tokens"] = ANTHROPIC_MAX_TOKENS;
    request["system"] = systemPrompt(!photos.empty());
    if (claudeSupportsEffort(config.modelName)) {
        // Short answers on a small screen; low effort keeps replies fast.
        request["output_config"]["effort"] = "low";
    }
    const bool fallback = claudeSupportsDefaultFallback(config.modelName);
    if (fallback) {
        request["fallbacks"] = "default";
    }

    JsonArray requestMessages = request["messages"].to<JsonArray>();
    const size_t lastIndex = messages.size() - 1;
    bool started = false;
    for (size_t index = contextStart(messages); index < messages.size(); ++index) {
        const ChatMessage& message = messages[index];
        // The Messages API needs the conversation to open with a user turn.
        if (!started && message.role != F("user")) {
            continue;
        }
        started = true;

        JsonObject item = requestMessages.add<JsonObject>();
        item["role"] = message.role == F("assistant") ? "assistant" : "user";
        if (index == lastIndex && message.role == F("user") && !photos.empty()) {
            JsonArray content = item["content"].to<JsonArray>();
            for (size_t photoIndex = 0; photoIndex < photos.size(); ++photoIndex) {
                JsonObject label = content.add<JsonObject>();
                label["type"] = "text";
                label["text"] = photos[photoIndex].label + ':';
                JsonObject image = content.add<JsonObject>();
                image["type"] = "image";
                JsonObject source = image["source"].to<JsonObject>();
                source["type"] = "base64";
                source["media_type"] = "image/jpeg";
                source["data"] = String(IMAGE_PLACEHOLDER) + photoIndex + F("@@");
            }
            JsonObject text = content.add<JsonObject>();
            text["type"] = "text";
            text["text"] = message.content.length() > 0 ? message.content : String(F("Please analyze the attached photo."));
        } else {
            item["content"] = message.content;
        }
    }

    if (requestMessages.size() == 0) {
        setError(F("No user message to send"));
        return false;
    }

    String skeleton;
    serializeJson(request, skeleton);

    String payload;
    payload.reserve(skeleton.length() + (4 * ((imageBytes + 2) / 3)) + 16);
    int cursor = 0;
    for (size_t photoIndex = 0; photoIndex < photos.size(); ++photoIndex) {
        const String placeholder = String(IMAGE_PLACEHOLDER) + photoIndex + F("@@");
        const int position = skeleton.indexOf(placeholder, cursor);
        if (position < 0) {
            setError(F("Photo request build failed"));
            return false;
        }
        payload += skeleton.substring(cursor, position);
        if (!appendFileBase64(photos[photoIndex].path, payload)) {
            setError(String(F("Photo read failed: ")) + photos[photoIndex].label);
            return false;
        }
        cursor = position + placeholder.length();
    }
    payload += skeleton.substring(cursor);

    String response;
    String error;
    if (!postPayload(config, ApiKind::Anthropic, fallback, anthropicUrl(config.apiBaseUrl), payload, response, error)) {
        setError(error);
        return false;
    }
    return parseAnthropicReply(response, assistantReply);
}

String AiClient::openAiUrl(String baseUrl) const {
    baseUrl = trimSlash(baseUrl);
    if (baseUrl.endsWith("/chat/completions")) {
        return baseUrl;
    }
    return baseUrl + F("/chat/completions");
}

String AiClient::anthropicUrl(String baseUrl) const {
    baseUrl = trimSlash(baseUrl);
    if (baseUrl.endsWith("/messages")) {
        return baseUrl;
    }
    if (!baseUrl.endsWith("/v1")) {
        baseUrl += F("/v1");
    }
    return baseUrl + F("/messages");
}

bool AiClient::parseOpenAiReply(const String& payload, String& assistantReply) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, payload);
    if (error) {
        setError(String(F("Invalid AI JSON: ")) + error.c_str());
        return false;
    }

    assistantReply = document["choices"][0]["message"]["content"] | "";
    assistantReply.trim();
    if (assistantReply.length() == 0) {
        setError(F("AI response has no assistant text"));
        return false;
    }

    return true;
}

bool AiClient::parseAnthropicReply(const String& payload, String& assistantReply) {
    JsonDocument document;
    const DeserializationError error = deserializeJson(document, payload);
    if (error) {
        setError(String(F("Invalid Claude JSON: ")) + error.c_str());
        return false;
    }

    const String stopReason = document["stop_reason"] | "";
    if (stopReason == F("refusal")) {
        String message = F("Claude declined this request");
        const char* category = document["stop_details"]["category"] | "";
        if (strlen(category) > 0) {
            message += F(" (");
            message += category;
            message += ')';
        }
        setError(message);
        return false;
    }

    // Read text blocks by type: thinking blocks can come first.
    for (JsonObject block : document["content"].as<JsonArray>()) {
        if (strcmp(block["type"] | "", "text") == 0) {
            assistantReply += block["text"] | "";
        }
    }
    assistantReply.trim();

    if (assistantReply.length() == 0) {
        setError(stopReason == F("max_tokens") ? F("Claude ran out of tokens before replying") : F("Claude response has no text"));
        return false;
    }
    if (stopReason == F("max_tokens")) {
        assistantReply += F(" [...]");
    }
    return true;
}

void AiClient::setError(const __FlashStringHelper* error) {
    lastError_ = String(error);
}

void AiClient::setError(const String& error) {
    lastError_ = error;
}
