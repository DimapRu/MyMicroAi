#include "DisplayManager.h"
#include "BoardPins.h"

namespace {
static constexpr int32_t DISPLAY_WIDTH = 240;
static constexpr int32_t DISPLAY_HEIGHT = 320;
static constexpr int32_t TOUCH_WIDTH = 320;
static constexpr int32_t TOUCH_HEIGHT = 240;
static constexpr uint8_t CST816_ADDR = 0x15;
static constexpr uint8_t CST816_ID_REG = 0xA7;
static constexpr uint8_t CST816_TOUCH_NUM_REG = 0x02;
static constexpr uint8_t CST816_TOUCH_XH_REG = 0x03;
static constexpr uint8_t CST816_TOUCH_YH_REG = 0x05;
static constexpr int32_t WORK_BUTTON_X = 12;
static constexpr int32_t WORK_BUTTON_Y = 186;
static constexpr int32_t WORK_BUTTON_W = 216;
static constexpr int32_t WORK_BUTTON_H = 44;
static constexpr int32_t CHAT_TOP_Y = 40;
static constexpr int32_t CHAT_INPUT_Y = 204;
static constexpr int32_t KEYBOARD_Y = 0;
static constexpr int32_t KEYBOARD_SCREEN_W = 320;
static constexpr int32_t KEYBOARD_SCREEN_H = 240;
static constexpr int32_t KEY_W = 46;
static constexpr int32_t KEY_H = 31;
static constexpr uint16_t COLOR_BACKGROUND = 0x0000;
static constexpr uint16_t COLOR_TEXT = 0xFFFF;
static constexpr uint16_t COLOR_ACCENT = 0xFA60;
static constexpr uint16_t COLOR_MUTED = 0x7BEF;
static constexpr uint16_t COLOR_WARNING = COLOR_ACCENT;
static constexpr uint16_t COLOR_ERROR = COLOR_ACCENT;
static constexpr uint16_t COLOR_BUTTON = COLOR_ACCENT;
static constexpr uint16_t COLOR_PANEL = COLOR_BACKGROUND;
static constexpr uint16_t COLOR_USER = COLOR_ACCENT;

bool hasUtf8Characters(const String& text) {
    for (size_t index = 0; index < text.length(); ++index) {
        if (static_cast<uint8_t>(text[index]) >= 0x80) {
            return true;
        }
    }
    return false;
}

int32_t estimateWrappedHeight(MyMicroAIDisplay& display, const String& text, int32_t width, int32_t lineHeight) {
    const lgfx::IFont* previousFont = display.getFont();
    display.setTextSize(1);
    if (hasUtf8Characters(text)) {
        display.setFont(&fonts::efontCN_12);
        lineHeight = max<int32_t>(lineHeight, 14);
    }

    int32_t lines = 1;
    String line;
    for (size_t i = 0; i < text.length();) {
        const uint8_t firstByte = static_cast<uint8_t>(text[i]);
        const size_t charLen = firstByte < 0x80 ? 1 :
                               (firstByte & 0xE0) == 0xC0 ? 2 :
                               (firstByte & 0xF0) == 0xE0 ? 3 :
                               (firstByte & 0xF8) == 0xF0 ? 4 : 1;
        const String current = text.substring(i, min(i + charLen, text.length()));
        i += charLen;

        if (current == "\n") {
            ++lines;
            line = "";
            continue;
        }

        line += current;
        if (display.textWidth(line) >= width) {
            ++lines;
            line = "";
        }
    }

    display.setFont(previousFont);
    return lines * lineHeight;
}

String utf8Tail(const String& text, size_t maxBytes) {
    if (text.length() <= maxBytes) {
        return text;
    }

    size_t start = text.length() - maxBytes;
    while (start < text.length() && (static_cast<uint8_t>(text[start]) & 0xC0) == 0x80) {
        ++start;
    }
    return text.substring(start);
}

bool readCst816Register(uint8_t reg, uint8_t* data, uint8_t length) {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) {
        return false;
    }

    if (Wire.requestFrom(CST816_ADDR, length) != length) {
        return false;
    }

    for (uint8_t index = 0; index < length; ++index) {
        data[index] = Wire.read();
    }
    return true;
}
}

MyMicroAIDisplay::MyMicroAIDisplay() {
    {
        auto cfg = bus_.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 40000000;
        cfg.freq_read = 16000000;
        cfg.spi_3wire = false;
        cfg.use_lock = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk = BoardPins::LCD_SCLK;
        cfg.pin_mosi = BoardPins::LCD_MOSI;
        cfg.pin_miso = BoardPins::LCD_MISO;
        cfg.pin_dc = BoardPins::LCD_DC;
        bus_.config(cfg);
        panel_.setBus(&bus_);
    }

    {
        auto cfg = panel_.config();
        cfg.pin_cs = BoardPins::LCD_CS;
        cfg.pin_rst = BoardPins::LCD_RST;
        cfg.pin_busy = -1;
        cfg.panel_width = DISPLAY_WIDTH;
        cfg.panel_height = DISPLAY_HEIGHT;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits = 1;
        cfg.readable = false;
        cfg.invert = true;
        cfg.rgb_order = false;
        cfg.dlen_16bit = false;
        cfg.bus_shared = true;
        panel_.config(cfg);
    }

    {
        auto cfg = light_.config();
        cfg.pin_bl = BoardPins::LCD_BL;
        cfg.invert = false;
        cfg.freq = 44100;
        cfg.pwm_channel = 7;
        light_.config(cfg);
        panel_.setLight(&light_);
    }

    setPanel(&panel_);
}

bool DisplayManager::begin() {
    ready_ = display_.init();
    if (!ready_) {
        return false;
    }

    pinMode(BoardPins::LCD_BL, OUTPUT);
    digitalWrite(BoardPins::LCD_BL, HIGH);
    display_.setRotation(1);
    display_.setBrightness(255);
    Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL);
    Wire.setClock(400000);
    uint8_t touchId = 0;
    touchReady_ = readCst816Register(CST816_ID_REG, &touchId, 1) && touchId == 0xB6;
    Serial.printf("CST816 touch: %s\n", touchReady_ ? "ready" : "not found");
    display_.fillScreen(COLOR_BACKGROUND);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setTextSize(2);
    showBoot("Display initialized");
    return true;
}

bool DisplayManager::isReady() const {
    return ready_;
}

void DisplayManager::showBoot(const char* message) {
    showStatus("MyMicroAI", message);
}

void DisplayManager::showConfigurationMode(const String& address) {
    if (!ready_) {
        return;
    }

    display_.setTextSize(1);
    display_.fillScreen(COLOR_BACKGROUND);
    drawHeader("[ AP MODE ]", COLOR_BACKGROUND);
    display_.drawRoundRect(14, 64, 212, 94, 10, COLOR_MUTED);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setFont(&fonts::efontCN_16);
    display_.setCursor(28, 86);
    display_.print("MyMicroAI-Setup");
    display_.setFont(&fonts::efontCN_12);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(28, 121);
    display_.print("connect to MyMicroAI Wi-Fi");
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setCursor(16, 182);
    display_.print("http://");
    display_.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    drawWrappedText(address + String("/"), 64, 182, 160, 16);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(16, 220);
    display_.print("login admin / pass admin");
    display_.setFont(&fonts::Font0);
}

void DisplayManager::showWorkMode(const String& address) {
    if (!ready_) {
        return;
    }

    workButtonVisible_ = true;
    display_.setTextSize(1);
    display_.fillScreen(COLOR_BACKGROUND);
    drawHeader("WORK MODE", COLOR_BACKGROUND);
    display_.setFont(&fonts::efontCN_16);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setCursor(14, 66);
    display_.print("Wi-Fi connected");
    display_.setFont(&fonts::efontCN_12);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(14, 100);
    display_.print("local address");
    display_.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    drawWrappedText(address, 14, 124, 212, 16);
    display_.setFont(&fonts::Font0);
    drawWorkSettingsButton();
}

void DisplayManager::showChatWorkMode(const std::vector<ChatSessionInfo>& sessions,
                                      int activeIndex,
                                      const std::vector<ChatMessage>& messages,
                                      const String& inputText,
                                      bool keyboardVisible,
                                      const String& statusText,
                                      int32_t scrollOffset,
                                      bool chatsMenuVisible,
                                      int chatsMenuOffset,
                                      bool clearChatsConfirmVisible) {
    if (!ready_) {
        return;
    }

    workButtonVisible_ = true;
    chatKeyboardVisible_ = keyboardVisible;
    chatsMenuVisible_ = chatsMenuVisible;
    clearChatsConfirmVisible_ = clearChatsConfirmVisible;
    chatsMenuOffset_ = chatsMenuOffset;
    cameraPreviewVisible_ = false;
    display_.setTextSize(1);
    display_.fillScreen(COLOR_BACKGROUND);
    display_.fillRect(0, 0, KEYBOARD_SCREEN_W, 24, COLOR_BACKGROUND);
    display_.drawFastHLine(12, 24, KEYBOARD_SCREEN_W - 24, COLOR_MUTED);
    display_.fillCircle(14, 12, 3, COLOR_ACCENT);
    display_.setFont(&fonts::efontCN_12);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setCursor(24, 8);
    display_.print("MyMicroAI");

    display_.fillRoundRect(12, 34, 68, 22, 6, COLOR_ACCENT);
    display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
    display_.setCursor(25, 40);
    display_.print("CHATS");

    display_.drawRoundRect(210, 34, 50, 22, 6, COLOR_MUTED);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(218, 40);
    display_.print("SETUP");

    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(92, 40);
    if (activeIndex >= 0 && static_cast<size_t>(activeIndex) < sessions.size()) {
        String title = sessions[activeIndex].title;
        if (title.length() > 10) {
            title = title.substring(0, 10);
        }
        display_.print(title);
    } else {
        display_.print("No chat");
    }

    if (chatsMenuVisible) {
        drawChatsMenu(sessions, activeIndex, chatsMenuOffset);
        return;
    }

    if (clearChatsConfirmVisible) {
        drawClearChatsConfirm();
        return;
    }

    const int32_t messageTop = 66;
    const int32_t messageBottom = keyboardVisible ? 72 : 198;
    int32_t contentHeight = 0;
    for (const ChatMessage& message : messages) {
        contentHeight += max<int32_t>(28, estimateWrappedHeight(display_, message.content, 248, 14) + 4);
    }

    const int32_t viewportHeight = max<int32_t>(0, messageBottom - messageTop);
    const int32_t maxScroll = max<int32_t>(0, contentHeight - viewportHeight);
    const int32_t effectiveScroll = min<int32_t>(max<int32_t>(scrollOffset, 0), maxScroll);
    int32_t y = messageTop - effectiveScroll;

    for (const ChatMessage& message : messages) {
        const bool isUser = message.role == F("user");
        const int32_t blockHeight = max<int32_t>(28, estimateWrappedHeight(display_, message.content, 248, 14) + 4);
        if (y + blockHeight >= messageTop && y < messageBottom) {
            display_.setTextColor(isUser ? COLOR_ACCENT : COLOR_TEXT, COLOR_BACKGROUND);
            display_.setTextSize(1);
            display_.setCursor(12, y);
            display_.print(isUser ? "* You:" : "* AI:");
            display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
            drawWrappedText(message.content, 58, y, 248, 14, messageTop, messageBottom);
        }
        y += blockHeight;
    }

    if (maxScroll > 0) {
        const int32_t barTrack = max<int32_t>(18, viewportHeight - 4);
        const int32_t barHeight = max<int32_t>(10, (viewportHeight * barTrack) / max<int32_t>(contentHeight, 1));
        const int32_t barY = messageTop + 2 + ((barTrack - barHeight) * effectiveScroll) / max<int32_t>(maxScroll, 1);
        display_.drawFastVLine(316, messageTop + 2, barTrack, COLOR_MUTED);
        display_.fillRoundRect(313, barY, 5, barHeight, 2, COLOR_ACCENT);
    }

    if (statusText.length() > 0) {
        display_.setTextColor(COLOR_WARNING, COLOR_BACKGROUND);
        display_.setTextSize(1);
        drawWrappedText(statusText, 8, messageBottom - 16, 224, 10);
    }

    if (keyboardVisible) {
        drawChatKeyboard(inputText);
    } else {
        display_.fillRoundRect(12, CHAT_INPUT_Y, 142, 34, 7, COLOR_PANEL);
        display_.drawRoundRect(12, CHAT_INPUT_Y, 142, 34, 7, COLOR_MUTED);
        display_.setTextColor(inputText.length() > 0 ? COLOR_TEXT : COLOR_MUTED, COLOR_PANEL);
        display_.setTextSize(1);
        String preview = inputText.length() > 0 ? utf8Tail(inputText, 22) : F("tap to type...");
        if (hasUtf8Characters(preview)) {
            display_.setFont(&fonts::efontCN_12);
            display_.setCursor(20, CHAT_INPUT_Y + 8);
            display_.print(preview);
            display_.setFont(&fonts::Font0);
        } else {
            display_.setCursor(20, CHAT_INPUT_Y + 13);
            display_.print(preview);
        }

        display_.drawRoundRect(160, CHAT_INPUT_Y, 36, 34, 7, COLOR_MUTED);
        display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
        display_.setCursor(168, CHAT_INPUT_Y + 13);
        display_.print("CAM");

        display_.fillRoundRect(202, CHAT_INPUT_Y, 56, 34, 7, COLOR_ACCENT);
        display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
        display_.setCursor(214, CHAT_INPUT_Y + 13);
        display_.print("SEND");
    }
}

void DisplayManager::showCameraPreview(camera_fb_t* frame, size_t queuedPhotos, const String& statusText, bool fullRedraw) {
    if (!ready_) {
        return;
    }

    cameraPreviewVisible_ = true;
    chatKeyboardVisible_ = false;
    chatsMenuVisible_ = false;
    clearChatsConfirmVisible_ = false;
    workButtonVisible_ = false;
    display_.setTextSize(1);

    if (fullRedraw) {
        display_.fillScreen(COLOR_BACKGROUND);
        display_.fillRect(0, 0, 320, 28, COLOR_BACKGROUND);
        display_.drawRoundRect(284, 4, 30, 22, 5, COLOR_MUTED);
        display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
        display_.setCursor(296, 12);
        display_.print("X");

        display_.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
        display_.setCursor(10, 10);
        display_.print("PHOTO");

        display_.fillRect(0, 198, 320, 42, COLOR_BACKGROUND);
        display_.drawFastHLine(12, 198, 296, COLOR_MUTED);
        display_.fillRoundRect(105, 206, 110, 28, 7, COLOR_ACCENT);
        display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
        display_.setCursor(128, 217);
        display_.print("SEND PHOTO");
    }

    if (frame && frame->buf && frame->len > 0) {
        display_.drawJpg(frame->buf, frame->len, 0, 28, 320, 170, 0, 0, 1.0f, 1.0f, datum_t::top_left);
    } else if (fullRedraw) {
        display_.drawRoundRect(12, 58, 296, 104, 10, COLOR_MUTED);
        display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
        display_.setCursor(92, 106);
        display_.print("camera preview");
    }

    if (fullRedraw || statusText.length() > 0) {
        display_.fillRect(64, 6, 160, 16, COLOR_BACKGROUND);
        display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
        display_.setCursor(70, 10);
        display_.printf("queued %u", static_cast<unsigned>(queuedPhotos));

        display_.fillRect(8, 216, 84, 14, COLOR_BACKGROUND);
        if (statusText.length() > 0) {
            display_.setCursor(10, 217);
            String clipped = statusText;
            if (clipped.length() > 14) {
                clipped = clipped.substring(0, 14);
            }
            display_.print(clipped);
        }
    }
}

void DisplayManager::showError(AppError error, const char* message) {
    if (!ready_) {
        return;
    }

    display_.fillScreen(COLOR_BACKGROUND);
    drawHeader("ERROR", COLOR_ERROR);
    display_.setTextColor(COLOR_ERROR, COLOR_BACKGROUND);
    display_.setTextSize(2);
    display_.setCursor(12, 58);
    display_.printf("Code: %u", static_cast<unsigned>(error));
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    drawWrappedText(message, 12, 96, 216, 22);
}

bool DisplayManager::pollChatAction(ChatUiAction& action) {
    action = ChatUiAction{};
    if (!ready_ || !touchReady_) {
        return false;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    if (!consumeTouch(x, y, chatKeyboardVisible_ ? 80 : 260)) {
        return false;
    }

    if (cameraPreviewVisible_) {
        if (x >= 282 && y <= 32) {
            action.type = ChatUiActionType::CloseCamera;
            return true;
        }
        if (x >= 92 && x <= 228 && y >= 200 && y <= 238) {
            action.type = ChatUiActionType::CapturePhoto;
            return true;
        }
        return false;
    }

    if (clearChatsConfirmVisible_) {
        if (y >= 170 && y <= 212) {
            if (x >= 36 && x <= 142) {
                action.type = ChatUiActionType::CancelClearChats;
                return true;
            }
            if (x >= 190 && x <= 284) {
                action.type = ChatUiActionType::ConfirmClearChats;
                return true;
            }
        }
        action.type = ChatUiActionType::CancelClearChats;
        return true;
    }

    if (chatsMenuVisible_) {
        if (y >= 196 && y <= 232) {
            if (x < 78) {
                action.type = ChatUiActionType::CloseChats;
                return true;
            }
            if (x < 156) {
                action.type = ChatUiActionType::NewChat;
                return true;
            }
            if (x < 212) {
                action.type = ChatUiActionType::OpenClearChatsConfirm;
                return true;
            }
            if (x < 260) {
                action.type = ChatUiActionType::ChatsUp;
                return true;
            }
            action.type = ChatUiActionType::ChatsDown;
            return true;
        }

        if (y >= 66 && y < 186) {
            const int row = (y - 66) / 30;
            action.type = ChatUiActionType::SelectChat;
            action.index = chatsMenuOffset_ + row;
            return true;
        }

        return false;
    }

    if (!chatKeyboardVisible_ && y >= 74 && y < CHAT_INPUT_Y) {
        action.type = x < 160 ? ChatUiActionType::ScrollUp : ChatUiActionType::ScrollDown;
        return true;
    }

    if (!chatKeyboardVisible_ && y >= 34 && y <= 58) {
        if (x >= 8 && x <= 84) {
            action.type = ChatUiActionType::OpenChats;
            return true;
        }
        if (x >= 210 && x <= 260) {
            action.type = ChatUiActionType::Settings;
            return true;
        }
    }

    if (chatKeyboardVisible_) {
        if (x >= 284 && y >= 4 && y <= 34) {
            action.type = ChatUiActionType::CloseKeyboard;
            return true;
        }

        if (y >= 216 && y <= 238) {
            if (x >= 4 && x <= 62) {
                keyboardLayout_ = keyboardLayout_ == KeyboardLayout::English ? KeyboardLayout::Russian :
                                  keyboardLayout_ == KeyboardLayout::Russian ? KeyboardLayout::Numbers : KeyboardLayout::English;
                action.type = ChatUiActionType::LayoutSwitch;
                return true;
            }
            if (x >= 68 && x <= 126) {
                action.type = ChatUiActionType::Backspace;
                return true;
            }
            if (x >= 132 && x <= 216) {
                action.type = ChatUiActionType::Character;
                action.text = " ";
                return true;
            }
            if (x >= 222 && x <= 316) {
                action.type = ChatUiActionType::Send;
                return true;
            }
        }

        static constexpr const char* EN_ROWS[] = {"qwertyu", "iopasdf", "ghjklzx", "cvbnm.,", "?!'-_/"};
        static constexpr const char* RU_ROWS[][8] = {
            {"й", "ц", "у", "к", "е", "н", "г", nullptr},
            {"ш", "щ", "з", "х", "ъ", "ф", "ы", nullptr},
            {"в", "а", "п", "р", "о", "л", "д", nullptr},
            {"ж", "э", "я", "ч", "с", "м", "и", nullptr},
            {"т", "ь", "б", "ю", ".", ",", "?", nullptr}
        };
        static constexpr const char* NUM_ROWS[] = {"1234567", "890+-*/", "=:;()[]", "{}<>@#$", "%&!?.,_"};
        static constexpr int32_t ROW_Y[] = {40, 75, 110, 145, 180};
        static constexpr uint8_t ROW_COUNTS[] = {7, 7, 7, 7, 7};
        for (uint8_t row = 0; row < 5; ++row) {
            for (uint8_t col = 0; col < ROW_COUNTS[row]; ++col) {
                const int32_t keyX = col * KEY_W;
                if (x >= keyX && x < keyX + KEY_W && y >= ROW_Y[row] && y < ROW_Y[row] + KEY_H) {
                    action.type = ChatUiActionType::Character;
                    if (keyboardLayout_ == KeyboardLayout::English) {
                        action.text = String(EN_ROWS[row][col]);
                    } else if (keyboardLayout_ == KeyboardLayout::Russian) {
                        action.text = RU_ROWS[row][col];
                    } else {
                        action.text = String(NUM_ROWS[row][col]);
                    }
                    return true;
                }
            }
        }

        return false;
    }

    if (y >= CHAT_INPUT_Y && y <= CHAT_INPUT_Y + 34) {
        if (x <= 154) {
            action.type = ChatUiActionType::OpenKeyboard;
            return true;
        }
        if (x >= 158 && x <= 198) {
            action.type = ChatUiActionType::OpenCamera;
            return true;
        }
        if (x >= 200) {
            action.type = ChatUiActionType::Send;
            return true;
        }
    }

    return false;
}

bool DisplayManager::wasWorkSettingsButtonPressed() {
    if (!ready_ || !touchReady_ || !workButtonVisible_) {
        return false;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    if (!consumeTouch(x, y, 900)) {
        return false;
    }

    const bool insideButton = x >= WORK_BUTTON_X && x <= WORK_BUTTON_X + WORK_BUTTON_W &&
                              y >= WORK_BUTTON_Y && y <= WORK_BUTTON_Y + WORK_BUTTON_H;
    if (!insideButton) {
        return false;
    }

    display_.fillRoundRect(WORK_BUTTON_X, WORK_BUTTON_Y, WORK_BUTTON_W, WORK_BUTTON_H, 10, COLOR_WARNING);
    display_.setTextColor(COLOR_BACKGROUND, COLOR_WARNING);
    display_.setTextSize(2);
    display_.setCursor(WORK_BUTTON_X + 34, WORK_BUTTON_Y + 13);
    display_.print("OPEN SETUP");
    return true;
}

void DisplayManager::showStatus(const char* title, const char* message) {
    if (!ready_) {
        return;
    }

    workButtonVisible_ = false;
    display_.fillScreen(COLOR_BACKGROUND);
    drawHeader(title, COLOR_ACCENT);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setTextSize(2);
    drawWrappedText(message, 12, 64, 216, 22);
}

bool DisplayManager::readTouchPoint(uint16_t& x, uint16_t& y) {
    uint8_t touchCount = 0;
    if (!readCst816Register(CST816_TOUCH_NUM_REG, &touchCount, 1) || touchCount == 0) {
        return false;
    }

    uint8_t rawX[2] = {0, 0};
    uint8_t rawY[2] = {0, 0};
    if (!readCst816Register(CST816_TOUCH_XH_REG, rawX, 2) || !readCst816Register(CST816_TOUCH_YH_REG, rawY, 2)) {
        return false;
    }

    const uint16_t touchRawX = static_cast<uint16_t>(((rawX[0] & 0x0F) << 8) | rawX[1]);
    const uint16_t touchRawY = static_cast<uint16_t>(((rawY[0] & 0x0F) << 8) | rawY[1]);
    x = touchRawY;
    y = TOUCH_HEIGHT - 1 - touchRawX;
    return true;
}

bool DisplayManager::consumeTouch(uint16_t& x, uint16_t& y, uint32_t debounceMs) {
    if (!readTouchPoint(x, y)) {
        touchHeld_ = false;
        return false;
    }

    if (touchHeld_) {
        return false;
    }

    if (millis() - lastWorkButtonPressMs_ < debounceMs) {
        return false;
    }

    touchHeld_ = true;
    lastWorkButtonPressMs_ = millis();
    return true;
}

void DisplayManager::drawHeader(const char* title, uint16_t color) {
    display_.setTextSize(1);
    display_.fillRect(0, 0, KEYBOARD_SCREEN_W, 32, color);
    display_.drawFastHLine(12, 31, KEYBOARD_SCREEN_W - 24, COLOR_MUTED);
    display_.setTextColor(COLOR_ACCENT, color);
    display_.setFont(&fonts::efontCN_12);
    display_.setCursor(14, 10);
    display_.print(title);
    display_.setFont(&fonts::Font0);
}

void DisplayManager::drawWorkSettingsButton() {
    display_.setTextSize(1);
    display_.fillRoundRect(WORK_BUTTON_X, WORK_BUTTON_Y, WORK_BUTTON_W, WORK_BUTTON_H, 8, COLOR_BUTTON);
    display_.setTextColor(COLOR_BACKGROUND, COLOR_BUTTON);
    display_.setFont(&fonts::efontCN_16);
    display_.setCursor(WORK_BUTTON_X + 56, WORK_BUTTON_Y + 13);
    display_.print("SETUP");
    display_.setFont(&fonts::Font0);
}

void DisplayManager::drawChatsMenu(const std::vector<ChatSessionInfo>& sessions, int activeIndex, int menuOffset) {
    display_.setTextSize(1);
    display_.fillRoundRect(8, 62, 304, 174, 10, COLOR_BACKGROUND);
    display_.drawRoundRect(8, 62, 304, 174, 10, COLOR_MUTED);
    display_.setFont(&fonts::efontCN_12);
    display_.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    display_.setCursor(20, 78);
    display_.print("CHATS");
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(82, 78);
    display_.printf("%u saved", static_cast<unsigned>(sessions.size()));

    const int safeOffset = max<int>(0, min<int>(menuOffset, max<int>(0, static_cast<int>(sessions.size()) - 1)));
    for (int row = 0; row < 4; ++row) {
        const int index = safeOffset + row;
        const int32_t rowY = 98 + row * 23;
        if (index >= static_cast<int>(sessions.size())) {
            display_.drawFastHLine(20, rowY + 17, 272, COLOR_MUTED);
            continue;
        }

        const bool isActive = index == activeIndex;
        if (isActive) {
            display_.fillRoundRect(16, rowY - 3, 280, 21, 6, COLOR_ACCENT);
            display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
        } else {
            display_.drawRoundRect(16, rowY - 3, 280, 21, 6, COLOR_MUTED);
            display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
        }

        String title = sessions[index].title;
        if (title.length() > 28) {
            title = title.substring(0, 28);
        }
        display_.setCursor(24, rowY + 2);
        display_.print(title);
    }

    display_.fillRoundRect(14, 198, 64, 30, 7, COLOR_BACKGROUND);
    display_.drawRoundRect(14, 198, 64, 30, 7, COLOR_MUTED);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(31, 209);
    display_.print("BACK");

    display_.fillRoundRect(84, 198, 64, 30, 7, COLOR_ACCENT);
    display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
    display_.setCursor(105, 209);
    display_.print("NEW");

    display_.drawRoundRect(154, 198, 52, 30, 7, COLOR_MUTED);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(169, 209);
    display_.print("CLR");

    display_.drawRoundRect(212, 198, 46, 30, 7, COLOR_MUTED);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setCursor(225, 209);
    display_.print("UP");

    display_.drawRoundRect(264, 198, 42, 30, 7, COLOR_MUTED);
    display_.setCursor(270, 209);
    display_.print("DOWN");
    display_.setFont(&fonts::Font0);
}

void DisplayManager::drawClearChatsConfirm() {
    display_.setTextSize(1);
    display_.fillRoundRect(28, 76, 264, 136, 12, COLOR_BACKGROUND);
    display_.drawRoundRect(28, 76, 264, 136, 12, COLOR_ACCENT);
    display_.setFont(&fonts::efontCN_12);
    display_.setTextColor(COLOR_ACCENT, COLOR_BACKGROUND);
    display_.setCursor(58, 96);
    display_.print("DELETE ALL CHATS?");
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(50, 124);
    display_.print("This removes saved SD chats");
    display_.setCursor(76, 140);
    display_.print("and creates a new empty one.");

    display_.drawRoundRect(36, 170, 106, 38, 8, COLOR_MUTED);
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setCursor(78, 184);
    display_.print("NO");

    display_.fillRoundRect(190, 174, 94, 30, 7, COLOR_ACCENT);
    display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
    display_.setCursor(224, 184);
    display_.print("YES");
    display_.setFont(&fonts::Font0);
}

void DisplayManager::refreshKeyboardPreview(const String& inputText) {
    if (!ready_ || !chatKeyboardVisible_) {
        return;
    }

    drawKeyboardPreview(inputText);
}

void DisplayManager::drawChatKeyboard(const String& inputText) {
    display_.setTextSize(1);
    display_.fillScreen(COLOR_PANEL);
    drawKeyboardPreview(inputText);

    display_.drawRoundRect(284, 4, 32, 30, 6, COLOR_MUTED);
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(297, 15);
    display_.print("X");

    static constexpr const char* EN_ROWS[] = {"qwertyu", "iopasdf", "ghjklzx", "cvbnm.,", "?!'-_/"};
    static constexpr const char* RU_ROWS[][8] = {
        {"й", "ц", "у", "к", "е", "н", "г", nullptr},
        {"ш", "щ", "з", "х", "ъ", "ф", "ы", nullptr},
        {"в", "а", "п", "р", "о", "л", "д", nullptr},
        {"ж", "э", "я", "ч", "с", "м", "и", nullptr},
        {"т", "ь", "б", "ю", ".", ",", "?", nullptr}
    };
    static constexpr const char* NUM_ROWS[] = {"1234567", "890+-*/", "=:;()[]", "{}<>@#$", "%&!?.,_"};
    static constexpr int32_t ROW_Y[] = {40, 75, 110, 145, 180};
    static constexpr uint8_t ROW_COUNTS[] = {7, 7, 7, 7, 7};
    display_.setTextSize(1);
    for (uint8_t row = 0; row < 5; ++row) {
        for (uint8_t col = 0; col < ROW_COUNTS[row]; ++col) {
            const int32_t x = col * KEY_W;
            display_.fillRoundRect(x + 1, ROW_Y[row] + 1, KEY_W - 2, KEY_H - 2, 6, COLOR_BACKGROUND);
            display_.drawRoundRect(x + 1, ROW_Y[row] + 1, KEY_W - 2, KEY_H - 2, 6, COLOR_MUTED);
            display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
            if (keyboardLayout_ == KeyboardLayout::English) {
                char keyBuffer[2] = {EN_ROWS[row][col], '\0'};
                display_.setCursor(x + 20, ROW_Y[row] + 12);
                display_.print(keyBuffer);
            } else if (keyboardLayout_ == KeyboardLayout::Russian) {
                display_.setFont(&fonts::efontCN_16);
                display_.setCursor(x + 15, ROW_Y[row] + 8);
                display_.print(RU_ROWS[row][col]);
                display_.setFont(&fonts::Font0);
            } else {
                char keyBuffer[2] = {NUM_ROWS[row][col], '\0'};
                display_.setCursor(x + 20, ROW_Y[row] + 12);
                display_.print(keyBuffer);
            }
        }
    }

    display_.fillRoundRect(4, 216, 58, 22, 5, COLOR_ACCENT);
    display_.drawRoundRect(68, 216, 58, 22, 5, COLOR_MUTED);
    display_.drawRoundRect(132, 216, 84, 22, 5, COLOR_MUTED);
    display_.fillRoundRect(222, 216, 94, 22, 5, COLOR_ACCENT);
    display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
    display_.setCursor(keyboardLayout_ == KeyboardLayout::Numbers ? 22 : 26, 224);
    display_.print(keyboardLayout_ == KeyboardLayout::English ? "EN" : keyboardLayout_ == KeyboardLayout::Russian ? "RU" : "123");
    display_.setTextColor(COLOR_TEXT, COLOR_BACKGROUND);
    display_.setCursor(87, 224);
    display_.print("DEL");
    display_.setTextColor(COLOR_MUTED, COLOR_BACKGROUND);
    display_.setCursor(159, 224);
    display_.print("SPACE");
    display_.setTextColor(COLOR_BACKGROUND, COLOR_ACCENT);
    display_.setCursor(252, 224);
    display_.print("SEND");
}

void DisplayManager::drawKeyboardPreview(const String& inputText) {
    display_.fillRoundRect(4, 4, 276, 30, 6, COLOR_BACKGROUND);
    display_.drawRoundRect(4, 4, 276, 30, 6, COLOR_MUTED);
    display_.setTextColor(inputText.length() > 0 ? COLOR_TEXT : COLOR_MUTED, COLOR_BACKGROUND);
    display_.setTextSize(1);
    String preview = inputText.length() > 0 ? utf8Tail(inputText, 42) : F("type...");
    if (hasUtf8Characters(preview)) {
        display_.setFont(&fonts::efontCN_12);
        display_.setCursor(12, 10);
        display_.print(preview);
        display_.setFont(&fonts::Font0);
    } else {
        display_.setCursor(12, 15);
        display_.print(preview);
    }
}

void DisplayManager::drawWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight) {
    drawWrappedText(text, x, y, width, lineHeight, -32768, 32767);
}

void DisplayManager::drawWrappedText(const String& text, int32_t x, int32_t y, int32_t width, int32_t lineHeight, int32_t clipTop, int32_t clipBottom) {
    display_.setTextSize(1);
    const bool useUtf8Font = hasUtf8Characters(text);
    if (useUtf8Font) {
        display_.setFont(&fonts::efontCN_12);
        lineHeight = max<int32_t>(lineHeight, 14);
    }

    String line;
    int32_t cursorY = y;
    display_.setCursor(x, cursorY);

    for (size_t i = 0; i < text.length();) {
        const uint8_t firstByte = static_cast<uint8_t>(text[i]);
        const size_t charLen = firstByte < 0x80 ? 1 :
                               (firstByte & 0xE0) == 0xC0 ? 2 :
                               (firstByte & 0xF0) == 0xE0 ? 3 :
                               (firstByte & 0xF8) == 0xF0 ? 4 : 1;
        const String current = text.substring(i, min(i + charLen, text.length()));
        i += charLen;

        if (current == "\n") {
            if (cursorY >= clipTop && cursorY < clipBottom) {
                display_.println(line);
            }
            line = "";
            cursorY += lineHeight;
            display_.setCursor(x, cursorY);
            continue;
        }

        line += current;
        if (display_.textWidth(line) >= width) {
            if (cursorY >= clipTop && cursorY < clipBottom) {
                display_.println(line);
            }
            line = "";
            cursorY += lineHeight;
            display_.setCursor(x, cursorY);
        }
    }

    if (line.length() > 0 && cursorY >= clipTop && cursorY < clipBottom) {
        display_.println(line);
    }

    if (useUtf8Font) {
        display_.setFont(&fonts::Font0);
    }
}
