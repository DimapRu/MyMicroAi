# MyMicroAI

<p align="center">
  <strong>A pocket-sized multimodal AI terminal built on ESP32-S3.</strong>
</p>

<p align="center">
  <a href="https://github.com/DimapRu/MyMicroAi"><img alt="PlatformIO" src="https://img.shields.io/badge/PlatformIO-ESP32--S3-orange"></a>
  <img alt="Framework" src="https://img.shields.io/badge/Framework-Arduino-blue">
  <img alt="AI" src="https://img.shields.io/badge/AI-OpenAI--compatible-111111">
  <img alt="Status" src="https://img.shields.io/badge/Status-Working%20prototype-brightgreen">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/DimapRu/MyMicroAi/main/picture/picture_1.jpg" alt="MyMicroAI device UI" width="42%">
  <img src="https://raw.githubusercontent.com/DimapRu/MyMicroAi/main/picture/picture_2.jpg" alt="MyMicroAI camera and chat workflow" width="42%">
</p>

## What Is MyMicroAI?

**MyMicroAI** is an ambitious handheld AI device for the **Waveshare ESP32-S3 CAM Touch LCD 2.1**. It combines a touch UI, full-screen keyboard, camera capture, SD-card memory, Wi-Fi setup portal, saved chats, and OpenAI-compatible multimodal requests inside one compact embedded firmware.

The project is built like a real product prototype: not just a board demo, but a complete pocket AI terminal with persistent state, clean UX, and a camera workflow designed for sending photos to vision-capable models.

## Key Features

- **AI chat on-device** — type messages directly on the 2.1-inch touchscreen.
- **Multimodal photo workflow** — open camera preview, capture photos, queue them as `photo1`, `photo2`, then send them with your prompt.
- **OpenAI-compatible API support** — configurable base URL, API key, and model name.
- **First-run setup portal** — local Wi-Fi AP with a web wizard for network and AI provider configuration.
- **Persistent SD-card memory** — saved config, chat sessions, message history, and temporary photos.
- **Chat session manager** — create new chats, switch between old chats, scroll long replies, and clear all chats with confirmation.
- **Full-screen keyboard** — English, Russian, numbers, symbols, backspace, space, send, and close controls.
- **Optimized ESP32-S3 camera path** — OV5640 JPEG capture with PSRAM and SD-backed image queue.
- **Public-safe repository** — no hardcoded API keys, Wi-Fi passwords, or personal credentials.

## Hardware

Target board:

- **Waveshare ESP32-S3 CAM Touch LCD 2.1** / **ESP32-S3-Touch-LCD-2**
- ESP32-S3 with PSRAM
- ST7789 240x320 SPI LCD
- CST816 capacitive touch
- OV5640 autofocus camera
- microSD card slot
- Wi-Fi

The hardware pin map lives in [`include/BoardPins.h`](include/BoardPins.h).

## Architecture

```text
include/
  AiClient.h          OpenAI-compatible text/image client
  AppStateMachine.h   Main firmware state machine
  AppTypes.h          Shared app config and state types
  BoardPins.h         Board-specific pin map
  CameraManager.h     OV5640 preview, capture, and JPEG save path
  ChatManager.h       SD-backed chat sessions
  ConfigManager.h     Runtime configuration storage
  DisplayManager.h    LCD, touch, keyboard, chat, and camera UI
  NetworkManager.h    Setup AP and web configuration portal
  StorageManager.h    SD card bootstrap

src/
  AiClient.cpp
  AppStateMachine.cpp
  CameraManager.cpp
  ChatManager.cpp
  ConfigManager.cpp
  DisplayManager.cpp
  NetworkManager.cpp
  StorageManager.cpp
  main.cpp
```

## First Boot Flow

1. Flash the firmware.
2. Insert a microSD card.
3. Power on the board.
4. Connect to the `MyMicroAI-Setup` Wi-Fi network.
5. Open the setup page and enter Wi-Fi + AI provider settings.
6. Save the configuration.
7. Start chatting from Work Mode.

Runtime credentials are stored on the SD card, not in the repository.

## Build

Install PlatformIO and run:

```bash
platformio run
```

Upload firmware:

```bash
platformio run --target upload
```

Open serial monitor:

```bash
platformio device monitor
```

The default environment is configured in [`platformio.ini`](platformio.ini). If your board uses another serial port, change or remove `monitor_port = COM10`.

## AI Backend

MyMicroAI sends requests to an OpenAI-compatible chat completions endpoint. The setup wizard lets you configure:

- API base URL
- API key
- model name

Queued camera photos are encoded as JPEG data URLs and sent together with the text prompt for models that support image input.

## Storage

The SD card is used for runtime data:

- `config.json` — local device configuration
- chat logs and sessions
- temporary captured photos before sending

These runtime files and folders are ignored by Git to prevent accidental leaks of private data.

## Security

- No API keys are committed.
- No Wi-Fi passwords are committed.
- Generated build folders are ignored.
- Local VS Code and PlatformIO artifacts are ignored.
- SD-card runtime data is ignored.

Always review copied SD-card files before sharing hardware logs or backups.

## Roadmap

- Streaming responses
- Better camera presets for documents and OCR
- Voice input and audio output
- Battery telemetry screen
- OTA updates
- Provider presets and model capability detection
- 3D-printable enclosure

## Status

MyMicroAI is a working embedded AI prototype with a complete UI loop, setup portal, persistent chats, camera capture, photo queue, and multimodal AI request path.

## License

A license has not been selected yet. Add one before accepting external contributions.
