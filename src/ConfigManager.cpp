#include "ConfigManager.h"

namespace {
static constexpr size_t CONFIG_MAX_BYTES = 4096;
}

bool ConfigManager::load(StorageManager& storage) {
    String content;
    if (!storage.readTextFile(CONFIG_FILE_PATH, content, CONFIG_MAX_BYTES)) {
        lastError_ = "Cannot read config file";
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        lastError_ = String("Config JSON parse error: ") + error.c_str();
        return false;
    }

    config_.wifiSsid = doc["wifi"]["ssid"] | "";
    config_.wifiPassword = doc["wifi"]["password"] | "";
    config_.apiBaseUrl = doc["ai"]["base_url"] | "";
    config_.apiKey = doc["ai"]["api_key"] | "";
    config_.modelName = doc["ai"]["model"] | "";

    if (!config_.isComplete()) {
        lastError_ = "Config is incomplete";
        return false;
    }

    lastError_ = "";
    return true;
}

bool ConfigManager::save(StorageManager& storage, const AppConfig& config) {
    if (!config.isComplete()) {
        lastError_ = "Cannot save incomplete config";
        return false;
    }

    JsonDocument doc;
    doc["wifi"]["ssid"] = config.wifiSsid;
    doc["wifi"]["password"] = config.wifiPassword;
    doc["ai"]["base_url"] = config.apiBaseUrl;
    doc["ai"]["api_key"] = config.apiKey;
    doc["ai"]["model"] = config.modelName;

    String output;
    output.reserve(1024);
    serializeJsonPretty(doc, output);

    if (!storage.writeTextFileAtomic(CONFIG_FILE_PATH, output)) {
        lastError_ = "Cannot write config file";
        return false;
    }

    config_ = config;
    lastError_ = "";
    return true;
}

const AppConfig& ConfigManager::config() const {
    return config_;
}

const char* ConfigManager::lastError() const {
    return lastError_.c_str();
}
