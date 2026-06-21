#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

class StorageManager {
public:
    bool begin();
    bool isReady() const;
    bool exists(const char* path) const;
    bool ensureDirectory(const char* path) const;
    bool readTextFile(const char* path, String& outContent, size_t maxBytes) const;
    bool writeTextFileAtomic(const char* path, const String& content) const;
    bool appendTextFile(const char* path, const String& content) const;
    bool remove(const char* path) const;
    bool rename(const char* fromPath, const char* toPath) const;
    const char* lastError() const;

private:
    bool ready_ = false;
    String lastError_;
    void setError(const __FlashStringHelper* error);
    void setError(const String& error);
};
