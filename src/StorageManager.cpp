#include "StorageManager.h"
#include "BoardPins.h"
#include <SPI.h>

namespace {
SPIClass sdSpi(FSPI);
}

bool StorageManager::begin() {
    sdSpi.begin(BoardPins::SD_SCK, BoardPins::SD_MISO, BoardPins::SD_MOSI, BoardPins::SD_CS);

    if (!SD.begin(BoardPins::SD_CS, sdSpi, 25000000U)) {
        ready_ = false;
        setError(F("SD card initialization failed"));
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        ready_ = false;
        setError(F("SD card not detected"));
        return false;
    }

    ready_ = true;
    lastError_ = "";
    return true;
}

bool StorageManager::isReady() const {
    return ready_;
}

bool StorageManager::exists(const char* path) const {
    return ready_ && SD.exists(path);
}

bool StorageManager::ensureDirectory(const char* path) const {
    if (!ready_) {
        return false;
    }

    if (SD.exists(path)) {
        File file = SD.open(path, FILE_READ);
        const bool directoryExists = file && file.isDirectory();
        file.close();
        return directoryExists;
    }

    return SD.mkdir(path);
}

bool StorageManager::readTextFile(const char* path, String& outContent, size_t maxBytes) const {
    outContent = "";

    if (!ready_) {
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    size_t fileSize = file.size();
    if (fileSize > maxBytes) {
        file.close();
        return false;
    }

    outContent.reserve(fileSize + 1);
    while (file.available()) {
        outContent += static_cast<char>(file.read());
    }
    file.close();
    return true;
}

bool StorageManager::writeTextFileAtomic(const char* path, const String& content) const {
    if (!ready_) {
        return false;
    }

    String tempPath = String(path) + ".tmp";
    File file = SD.open(tempPath.c_str(), FILE_WRITE);
    if (!file) {
        return false;
    }

    size_t written = file.print(content);
    file.flush();
    file.close();

    if (written != content.length()) {
        SD.remove(tempPath.c_str());
        return false;
    }

    if (SD.exists(path)) {
        SD.remove(path);
    }

    return SD.rename(tempPath.c_str(), path);
}

bool StorageManager::appendTextFile(const char* path, const String& content) const {
    if (!ready_) {
        return false;
    }

    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        return false;
    }

    const size_t written = file.print(content);
    file.flush();
    file.close();
    return written == content.length();
}

bool StorageManager::remove(const char* path) const {
    return ready_ && SD.remove(path);
}

bool StorageManager::rename(const char* fromPath, const char* toPath) const {
    return ready_ && SD.rename(fromPath, toPath);
}

const char* StorageManager::lastError() const {
    return lastError_.c_str();
}

void StorageManager::setError(const __FlashStringHelper* error) {
    lastError_ = String(error);
}

void StorageManager::setError(const String& error) {
    lastError_ = error;
}
