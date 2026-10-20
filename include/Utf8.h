#pragma once

#include <Arduino.h>

namespace Utf8 {
inline bool isContinuationByte(char value) {
    return (static_cast<uint8_t>(value) & 0xC0) == 0x80;
}

// Cuts text to at most maxBytes without splitting a multi-byte character.
inline String truncateBytes(const String& text, size_t maxBytes) {
    if (text.length() <= maxBytes) {
        return text;
    }

    size_t cut = maxBytes;
    while (cut > 0 && isContinuationByte(text[cut])) {
        --cut;
    }
    return text.substring(0, cut);
}

// Keeps the first maxChars characters (not bytes) of text.
inline String prefixChars(const String& text, size_t maxChars) {
    size_t chars = 0;
    for (size_t index = 0; index < text.length(); ++index) {
        if (isContinuationByte(text[index])) {
            continue;
        }
        if (chars == maxChars) {
            return text.substring(0, index);
        }
        ++chars;
    }
    return text;
}

inline size_t countChars(const String& text) {
    size_t chars = 0;
    for (size_t index = 0; index < text.length(); ++index) {
        if (!isContinuationByte(text[index])) {
            ++chars;
        }
    }
    return chars;
}
}
