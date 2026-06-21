#pragma once

#include <Arduino.h>

namespace BoardPins {
    static constexpr int LCD_SCLK = 39;
    static constexpr int LCD_MOSI = 38;
    static constexpr int LCD_MISO = 40;
    static constexpr int LCD_CS = 45;
    static constexpr int LCD_DC = 42;
    static constexpr int LCD_RST = -1;
    static constexpr int LCD_BL = 1;

    static constexpr int SD_SCK = 39;
    static constexpr int SD_MISO = 40;
    static constexpr int SD_MOSI = 38;
    static constexpr int SD_CS = 41;

    static constexpr int USER_BUTTON = 0;

    static constexpr int I2C_SDA = 48;
    static constexpr int I2C_SCL = 47;

    static constexpr int CAM_PWDN = 17;
    static constexpr int CAM_RESET = -1;
    static constexpr int CAM_XCLK = 8;
    static constexpr int CAM_SIOD = 21;
    static constexpr int CAM_SIOC = 16;
    static constexpr int CAM_Y9 = 2;
    static constexpr int CAM_Y8 = 7;
    static constexpr int CAM_Y7 = 10;
    static constexpr int CAM_Y6 = 14;
    static constexpr int CAM_Y5 = 11;
    static constexpr int CAM_Y4 = 15;
    static constexpr int CAM_Y3 = 13;
    static constexpr int CAM_Y2 = 12;
    static constexpr int CAM_VSYNC = 6;
    static constexpr int CAM_HREF = 4;
    static constexpr int CAM_PCLK = 9;
}
