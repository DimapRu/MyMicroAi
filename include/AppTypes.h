#pragma once

#include <Arduino.h>

static constexpr const char* CONFIG_FILE_PATH = "/config.json";
static constexpr const char* CHAT_LOG_FILE_PATH = "/chatlog.jsonl";

struct AppConfig {
    String wifiSsid;
    String wifiPassword;
    String apiBaseUrl;
    String apiKey;
    String modelName;

    bool isComplete() const {
        return wifiSsid.length() > 0 && apiBaseUrl.length() > 0 && apiKey.length() > 0 && modelName.length() > 0;
    }
};

enum class AppMode : uint8_t {
    Boot,
    Configuration,
    Work,
    Error
};

enum class AppError : uint8_t {
    None,
    PsramUnavailable,
    StorageUnavailable,
    ConfigInvalid,
    WifiFailed
};
