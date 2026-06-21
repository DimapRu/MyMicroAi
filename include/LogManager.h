#pragma once

#include <Arduino.h>

class LogManager {
public:
    static constexpr const char* LOG_FILE_PATH = "/logs.txt";

    static bool clear();
    static bool append(const String& category, const String& message);
    static bool append(const __FlashStringHelper* category, const String& message);

private:
    static String timestamp();
};
