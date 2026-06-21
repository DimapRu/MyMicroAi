#include "LogManager.h"
#include <SD.h>

bool LogManager::clear() {
    if (SD.exists(LOG_FILE_PATH)) {
        SD.remove(LOG_FILE_PATH);
    }

    File file = SD.open(LOG_FILE_PATH, FILE_WRITE);
    if (!file) {
        return false;
    }

    file.println(F("--- MyMicroAI runtime log ---"));
    file.print(F("Boot ms: "));
    file.println(millis());
    file.close();
    return true;
}

bool LogManager::append(const String& category, const String& message) {
    File file = SD.open(LOG_FILE_PATH, FILE_APPEND);
    if (!file) {
        return false;
    }

    file.print('[');
    file.print(timestamp());
    file.print(F("] "));
    file.println(category);
    file.println(message);
    file.println();
    file.close();
    return true;
}

bool LogManager::append(const __FlashStringHelper* category, const String& message) {
    return append(String(category), message);
}

String LogManager::timestamp() {
    String value;
    value.reserve(16);
    value += millis();
    value += F(" ms");
    return value;
}
