#include "AiClient.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include "LogManager.h"
#include "mbedtls/base64.h"

namespace {
static constexpr uint16_t AI_TIMEOUT_MS = 60000;
static constexpr size_t AI_RESPONSE_MAX_BYTES = 24576;
static constexpr size_t AI_MAX_IMAGE_BYTES = 140000;
static constexpr size_t AI_MAX_CONTEXT_MESSAGES = 10;

String trimSlash(String value) {
    value.trim();
    while (value.endsWith("/")) {
        value.remove(value.length() - 1);
    }
    return value;
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

    encoded[written] = '\0';
    out += encoded;
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
        const int found = payload.indexOf(F("data:image/jpeg;base64,"), searchFrom);
        if (found < 0) {
            break;
        }
        ++imageParts;
        searchFrom = found + 1;
    }

    summary += imageParts;
    return summary;
}

bool postPayload(const AppConfig& config, const String& url, const String& payload, String& response, String& errorOut) {
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool httpStarted = false;

    if (url.startsWith("https://")) {
        secureClient.setInsecure();
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
    http.addHeader("Authorization", String("Bearer ") + config.apiKey);
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

        errorOut = F("AI HTTP ");
        errorOut += httpCode;
        if (response.length() > 0) {
            errorOut += F(": ");
            errorOut += response.substring(0, 180);
        }
        return false;
    }

    return true;
}
}

bool AiClient::sendChat(const AppConfig& config, const std::vector<ChatMessage>& messages, String& assistantReply) {
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

    JsonDocument request;
    request["model"] = config.modelName;
    request["temperature"] = 0.7;
    request["max_tokens"] = 420;
    JsonArray requestMessages = request["messages"].to<JsonArray>();

    JsonObject systemMessage = requestMessages.add<JsonObject>();
    systemMessage["role"] = "system";
    systemMessage["content"] = "You are MyMicroAI, a concise helpful assistant running on a tiny ESP32 handheld. Reply in the same language the user uses. If the user writes in Russian, answer in natural Russian. Keep replies short because the display is very small.";

    const size_t startIndex = messages.size() > AI_MAX_CONTEXT_MESSAGES ? messages.size() - AI_MAX_CONTEXT_MESSAGES : 0;
    for (size_t index = startIndex; index < messages.size(); ++index) {
        JsonObject item = requestMessages.add<JsonObject>();
        item["role"] = messages[index].role;
        item["content"] = messages[index].content;
    }

    String payload;
    serializeJson(request, payload);

    const String url = completionsUrl(config.apiBaseUrl);
    String response;
    String error;
    if (!postPayload(config, url, payload, response, error)) {
        setError(error);
        return false;
    }

    if (response.length() > AI_RESPONSE_MAX_BYTES) {
        LogManager::append(F("AI RESPONSE TOO LARGE"), String(F("bytes=")) + response.length() + '\n' + response.substring(0, 1024));
        setError(F("AI response is too large"));
        return false;
    }

    return parseReply(response, assistantReply);
}

bool AiClient::sendChatWithImages(const AppConfig& config, const std::vector<ChatMessage>& messages, const std::vector<PhotoAttachment>& photos, String& assistantReply) {
    if (photos.empty()) {
        return sendChat(config, messages, assistantReply);
    }

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

    size_t imageBytes = 0;
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
        imageBytes += fileSize;
    }

    String payload;
    payload.reserve(4096 + (4 * ((imageBytes + 2) / 3)) + photos.size() * 96);
    payload += F("{\"model\":");
    JsonDocument scalar;
    scalar.set(config.modelName);
    serializeJson(scalar, payload);
    payload += F(",\"temperature\":0.7,\"max_tokens\":420,\"messages\":[");
    payload += F("{\"role\":\"system\",\"content\":\"You are MyMicroAI, a concise helpful assistant running on a tiny ESP32 handheld. Reply in the same language the user uses. If the user writes in Russian, answer in natural Russian. Keep replies short because the display is very small. When images are attached, inspect them carefully and read visible text or numbers.\"}");

    const size_t startIndex = messages.size() > AI_MAX_CONTEXT_MESSAGES ? messages.size() - AI_MAX_CONTEXT_MESSAGES : 0;
    const size_t lastIndex = messages.size() - 1;
    for (size_t index = startIndex; index < messages.size(); ++index) {
        payload += ',';
        payload += F("{\"role\":");
        scalar.clear();
        scalar.set(messages[index].role);
        serializeJson(scalar, payload);
        payload += F(",\"content\":");

        if (index == lastIndex && messages[index].role == F("user")) {
            payload += '[';
            payload += F("{\"type\":\"text\",\"text\":");
            scalar.clear();
            scalar.set(messages[index].content.length() > 0 ? messages[index].content : String(F("Please analyze the attached photo.")));
            serializeJson(scalar, payload);
            payload += '}';

            for (const PhotoAttachment& photo : photos) {
                payload += F(",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,");
                if (!appendFileBase64(photo.path, payload)) {
                    setError(String(F("Photo read failed: ")) + photo.label);
                    return false;
                }
                payload += F("\"}}");
            }
            payload += ']';
        } else {
            scalar.clear();
            scalar.set(messages[index].content);
            serializeJson(scalar, payload);
        }
        payload += '}';
    }
    payload += F("]}");

    const String url = completionsUrl(config.apiBaseUrl);
    String response;
    String error;
    if (!postPayload(config, url, payload, response, error)) {
        setError(error);
        return false;
    }

    if (response.length() > AI_RESPONSE_MAX_BYTES) {
        LogManager::append(F("AI RESPONSE TOO LARGE"), String(F("bytes=")) + response.length() + '\n' + response.substring(0, 1024));
        setError(F("AI response is too large"));
        return false;
    }

    return parseReply(response, assistantReply);
}

const char* AiClient::lastError() const {
    return lastError_.c_str();
}

String AiClient::completionsUrl(String baseUrl) const {
    baseUrl = trimSlash(baseUrl);
    if (baseUrl.endsWith("/chat/completions")) {
        return baseUrl;
    }
    return baseUrl + F("/chat/completions");
}

bool AiClient::parseReply(const String& payload, String& assistantReply) {
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

void AiClient::setError(const __FlashStringHelper* error) {
    lastError_ = String(error);
}

void AiClient::setError(const String& error) {
    lastError_ = error;
}
