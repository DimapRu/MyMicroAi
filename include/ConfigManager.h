#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "AppTypes.h"
#include "StorageManager.h"

class ConfigManager {
public:
    bool load(StorageManager& storage);
    bool save(StorageManager& storage, const AppConfig& config);
    const AppConfig& config() const;
    const char* lastError() const;

private:
    AppConfig config_;
    String lastError_;
};
