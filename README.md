# MyMicroAI

<p align="center">
  <strong>A pocket-sized multimodal AI terminal built on ESP32-S3.</strong>
</p>

<p align="center">
  <a href="https://github.com/DimapRu/MyMicroAi"><img alt="PlatformIO" src="https://img.shields.io/badge/PlatformIO-ESP32--S3-orange"></a>
  <img alt="Framework" src="https://img.shields.io/badge/Framework-Arduino-blue">
  <img alt="AI" src="https://img.shields.io/badge/AI-Claude%20%7C%20OpenAI--compatible-111111">
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
- **Autofocus camera** — OV5640 AF firmware loaded at camera start, continuous focus in preview, tap-to-focus, and a focus pass before every 1600x1200 shot.
- **Claude support** — native Claude Messages API when the base URL is `https://api.anthropic.com/v1`, or through the [bridge](bridge/README.md) on your Claude subscription.
- **OpenAI-compatible API support** — configurable base URL, API key, and model name.
- **First-run setup portal** — local Wi-Fi AP with a web wizard for network and AI provider configuration.
- **Persistent SD-card memory** — saved config, chat sessions, message history, and temporary photos.
- **Chat session manager** — create new chats, switch between old chats, scroll long replies, and clear all chats with confirmation.
- **PC chat over USB** — plug the device into a computer and chat with the same AI from a browser page; the conversation is shared with the device screen.
- **Full-screen keyboard** — English, Russian, numbers, symbols, backspace, space, send, and close controls.
- **Optimized ESP32-S3 camera path** — fast QVGA preview, full-resolution JPEG capture in PSRAM, and an SD-backed image queue.
- **Public-safe repository** — no hardcoded API keys, Wi-Fi passwords, or personal credentials.

## Hardware

Target board:

- **Waveshare ESP32-S3 CAM Touch LCD 2.1** / **ESP32-S3-Touch-LCD-2**
- ESP32-S3 with PSRAM
- ST7789 240x320 SPI LCD
- CST816 capacitive touch
- OV5640 camera (the AF version with a focus motor for autofocus)
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
  CameraManager.h     OV5640 preview, autofocus, capture, and JPEG save path
  ChatManager.h       SD-backed chat sessions
  ConfigManager.h     Runtime configuration storage
  DisplayManager.h    LCD, touch, keyboard, chat, and camera UI
  LogManager.h        SD-card runtime log
  Ov5640Af.h          OV5640 autofocus MCU control (firmware upload, focus commands)
  Ov5640AfFirmware.h  OV5640 AF microcode
  NetworkManager.h    Setup AP and web configuration portal
  StorageManager.h    SD card bootstrap
  UsbBridge.h         JSON-lines protocol for the PC chat over USB serial
  TlsConfig.h         Root CA bundle hookup for HTTPS
  Utf8.h              UTF-8 safe string clipping

data/cert/
  x509_crt_bundle.bin Mozilla root CA bundle embedded into the firmware

src/
  AiClient.cpp
  AppStateMachine.cpp
  CameraManager.cpp
  ChatManager.cpp
  ConfigManager.cpp
  DisplayManager.cpp
  LogManager.cpp
  Ov5640Af.cpp
  NetworkManager.cpp
  StorageManager.cpp
  UsbBridge.cpp
  main.cpp

tools/pc-chat/
  index.html          Browser chat for the PC (Web Serial, no install)

bridge/
  mymicroai_bridge.py Bridge to Claude via the API or Claude Code (see bridge/README.md)
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

The setup wizard lets you configure the API base URL, API key and model name, with presets for OpenAI, Claude and the bridge.

- **Claude (Anthropic API)**: base URL `https://api.anthropic.com/v1` and an Anthropic API key. The device then speaks the native Messages API: photos go as base64 image blocks, `output_config.effort` is `low` for quick answers on supported models, `max_tokens` leaves room for Claude's thinking, and on `claude-opus-5-5`, `claude-opus-5`, `claude-sonnet-5-5` and `claude-fable-5-1` the request opts into server-side refusal fallback (`fallbacks: "default"`). The setup page preselects `claude-opus-5-5`.
- **OpenAI-compatible**: any `/chat/completions` endpoint; photos go as JPEG data URLs.
- **Bridge**: run [`bridge/mymicroai_bridge.py`](bridge/README.md) on a computer or server and point the device at it. It forwards to the Claude API, or to Claude Code (`claude -p`) so requests run on your Claude subscription instead of an API key.

Each queued photo is labeled (`photo1:`, `photo2:` …) right before its image, matching the labels in your message, so you can ask about a specific photo. Up to 4 photos go with one message.

A message is saved to the chat only after the AI answers. If a request fails, the error is shown in the status line and the text is put back into the input, so tapping Send retries it.

### TLS

HTTPS certificates are verified against the Mozilla root CA bundle in `data/cert/x509_crt_bundle.bin`, embedded via `board_build.embed_files`. To refresh it, run ESP-IDF v4.4 `components/mbedtls/esp_crt_bundle/gen_crt_bundle.py -i cacrt_all.pem` (the v4.4 script matches the bundle format of Arduino-ESP32 2.x) and copy the resulting `x509_crt_bundle` over the file. A provider with a self-signed certificate will be rejected; use plain `http://` for local servers.

## Camera and Autofocus

The camera starts in a QVGA preview drawn to fit the screen, so the preview shows the whole frame the photo will contain. Taking a photo:

1. If the module is an OV5640 AF, the autofocus microcode (about 4 KB) is uploaded when the camera opens and continuous focus runs during preview. The status bar shows `AF auto`, `AF lock`, `AF miss` or `AF off`.
2. Tap the preview to focus on that spot (touch-zone AF); the lock is reused if you capture within a few seconds.
3. **CAPTURE** runs a single focus pass, switches the sensor to 1600x1200, drops the frames still in flight at preview size and saves a JPEG (typically 150–300 KB) to `/photos`.

The status line then shows the label, resolution and size, e.g. `photo1 1600x1200 212 KB`.

A plain OV5640 (without the focus motor) works as a fixed-focus camera and shows `AF off`. Some AF modules wire the motor supply separately; on those the firmware loads but the lens does not move, so check that close-up shots sharpen when you tap.

## PC Chat over USB

1. Connect the device to the computer with a USB cable and wait for Work Mode.
2. Open [`tools/pc-chat/index.html`](tools/pc-chat/index.html) in Google Chrome or Microsoft Edge (Web Serial is not available in Firefox or Safari).
3. Click **Подключить** and pick the device's serial port. Close the PlatformIO serial monitor first: only one program can hold the port.

The browser remembers the port, so after the first time the page connects by itself whenever the device is plugged in; keep it open in a pinned tab. The device still talks to the AI over its own Wi-Fi: the PC only sends text and shows replies. Messages sent from the PC land in the active chat and appear on the device screen, and messages typed on the device show up on the PC. Photos queued on the device are not attached to messages sent from the PC.

The firmware listens on `Serial` and, on boards where that is not the ESP32-S3 native USB port, on `USBSerial` as well, so it works whether the board's USB connector goes through a USB-UART bridge or straight to the chip.

Protocol, one JSON object per line at 115200 baud:

- PC → device: `{"cmd":"hello"}`, `{"cmd":"history"}`, `{"cmd":"send","text":"..."}`, `{"cmd":"new_chat"}`
- Device → PC, each line prefixed with `@mma `: `hello` (with `state`: `work`, `setup`, `error`), `history_begin` / `message` / `history_end`, `message` (`role`, `content`), `status` (`thinking`), `error` (`text`)

Lines without the `@mma ` prefix are ordinary debug logs; the page shows them under **Лог устройства**.

## Storage

The SD card is used for runtime data:

- `config.json` — local device configuration
- chat logs and sessions (each chat file is compacted to its latest messages once it grows past 64 KB)
- temporary captured photos before sending

These runtime files and folders are ignored by Git to prevent accidental leaks of private data.

## Security

- No API keys are committed.
- No Wi-Fi passwords are committed.
- HTTPS connections to the AI provider verify the server certificate.
- Generated build folders are ignored.
- Local VS Code and PlatformIO artifacts are ignored.
- SD-card runtime data is ignored.

Always review copied SD-card files before sharing hardware logs or backups.

## Roadmap

- Streaming responses
- Camera presets for documents and OCR (higher resolution, manual focus distance)
- Voice input and audio output
- Battery telemetry screen
- OTA updates
- Provider presets and model capability detection
- 3D-printable enclosure

## Status

MyMicroAI is a working embedded AI prototype with a complete UI loop, setup portal, persistent chats, camera capture, photo queue, and multimodal AI request path.

## License

A license has not been selected yet. Add one before accepting external contributions.
