# DFRobot DFR1154 ESP32-S3 AI Camera

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Version](https://img.shields.io/badge/firmware-v1.2.16%20%7C%20v3.2.0-green)
![License](https://img.shields.io/badge/license-MIT-brightgreen)
![Arduino](https://img.shields.io/badge/IDE-Arduino%202.x-teal)
![Status](https://img.shields.io/badge/status-active-success)

An ESP32-S3 based AI camera platform integrating **OpenAI GPT-4 Vision**, **Whisper speech-to-text**, and **TTS APIs** into a self-contained embedded system with a custom web UI. Developed in association with [CIJE — Center for Innovation in Jewish Education](https://www.cije.org).

---

## Table of Contents

- [Overview](#overview)
- [Hardware](#hardware)
- [Firmware Versions](#firmware-versions)
- [Getting Started](#getting-started)
  - [Arduino IDE Setup](#arduino-ide-setup)
  - [Board Configuration](#board-configuration)
  - [Required Libraries](#required-libraries)
  - [Credentials Setup](#credentials-setup)
  - [Upload & Run](#upload--run)
- [Web UI](#web-ui)
- [Known Limitations](#known-limitations)
- [Repository Structure](#repository-structure)
- [Contributing](#contributing)
- [Credits](#credits)
- [License](#license)

---

## Overview

The DFRobot DFR1154 AI Camera provides a low-cost, highly integrated platform for embedding AI vision and audio into projects. This firmware suite wraps the hardware capabilities into two application lines:

| App Line | Folder | Description |
|---|---|---|
| **Standalone App** | `firmware/standalone/` | Camera + audio UI for image capture, recording, and OpenAI analysis |
| **Automation Agent** | `firmware/automation_agent/` | Adds Arduino UNO serial control for automated workflows |

Both apps serve the board's custom web interface over Wi-Fi, making setup and operation accessible from any browser without additional software.

---

## Hardware

| Component | Details |
|---|---|
| **MCU** | ESP32-S3R8 — Dual-Core 240MHz, 8MB OPI PSRAM |
| **Camera** | OV3660 — 160° wide-angle, IR-capable |
| **IR Illumination** | GPIO 47 |
| **Microphone** | PDM I2S digital mic — GPIO 38 (CLK), GPIO 39 (DATA) |
| **Audio Amplifier** | MAX98357A I2S — BCLK GPIO 45, LRCLK GPIO 46, DIN GPIO 42 |
| **Speaker** | MX1.25-2P connector |
| **Storage** | MicroSD slot (up to 32GB), SPI — MO/MI/SCLK/CS: GPIO 11/13/12/10 |
| **Flash** | 16MB |
| **Ambient Light** | LTR-308 ALS — SDA GPIO 8, SCL GPIO 9 |
| **Status LED** | GPIO 3 |
| **Power** | USB Type-C / VIN 3.7–15V DC |
| **UART** | TX GPIO 43, RX GPIO 44 (Gravity connector) |

See [`hardware/`](hardware/) for full pin diagrams and the on-board function diagram.

---

## Firmware Versions

### Automation Agent Line (`firmware/automation_agent/`)

| Version | Notes |
|---|---|
| **v1.2.16** | Full PSRAM TTS buffering via `ps_malloc()`; UI timing fix — text displays before audio via `pendingTTSDelayMs` |
| v1.2.15 | TTS micro-dropout and chunk header contamination fixes |
| v1.2.0 | Initial Arduino UNO serial automation controller |

### Standalone Line (`firmware/standalone/`)

| Version | Notes |
|---|---|
| **v3.2.0** | Dynamic IP/DHCP; live `/settings` page for credential editing without reflashing |
| v2.0.28 | Stable pairing workflow — capture → record → send to OpenAI → TTS playback |

---

## Getting Started

### Arduino IDE Setup

1. Open **File → Preferences** in Arduino IDE 2.x
2. Add this URL to *Additional Boards Manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools → Boards Manager**, search for `esp32`, and install **esp32 by Espressif**

> **Timeout fix:** If installation times out, add `network:\n  connection_timeout: 600s` to your `arduino-cli.yaml` and retry. See the [Quick Start Guide](docs/quick_start_guide.pdf) for details.

### Board Configuration

Under **Tools**, set the following:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | OPI PSRAM |

### Required Libraries

| Library | Version |
|---|---|
| ArduinoJson by Benoit Blanchon | 7.4.2 or newer |

### Credentials Setup

> ⚠️ **NEVER commit API keys or passwords to GitHub.** The repository is configured to ignore `secrets.h`.

Copy `secrets_template.h` to `secrets.h` and fill in your values:

```cpp
// secrets.h — DO NOT COMMIT THIS FILE
#define WIFI_SSID      "YourNetworkName"
#define WIFI_PASSWORD  "YourPassword"
#define OPENAI_API_KEY "sk-..."
```

Get your OpenAI API key at [platform.openai.com/api-keys](https://platform.openai.com/api-keys).

### Upload & Run

1. Connect the DFR1154 via USB Type-C and select the correct COM port
2. Open the target `.ino` file and upload
3. Open **Serial Monitor** at 115200 baud
4. Watch for the assigned IP address:
   ```
   WiFi connected
   IP: 192.168.0.181
   ```
5. Enter that IP into your browser

---

## Web UI

The browser interface provides:

- **Live camera stream** with Capture and Review controls
- **Audio recording** (Record button → Whisper transcription)
- **OpenAI Analysis Results** panel (text result displayed before TTS audio plays)
- **Audio Playback Bar** with Speaker output
- **Resource management** — image and audio file lists with Save to PC / Export options
- **Send to OpenAI** / **Replay TTS** / **Export Package** actions

The Automation Agent version also accepts serial commands from an Arduino UNO for triggering capture and analysis workflows programmatically.

---

## Known Limitations

| Issue | Notes |
|---|---|
| **I2S conflict** | PDM microphone (GPIO 38/39) and camera DMA both require I2S_NUM_0 — real-time simultaneous recording is not supported; `recordWAV()` is a blocking operation |
| **SSL validation** | OpenAI's certificate chain cannot be fully validated on ESP32; `.setInsecure()` maintains encryption while bypassing chain validation |
| **Stack size** | FreeRTOS `loopTask` stack is ~8KB — large audio buffers must be declared `static` or allocated to PSRAM via `ps_malloc()` |
| **WDT** | `esp_task_wdt_reset()` requires prior task subscription; use `vTaskDelay(1)` in `loop()` as the correct yield/WDT pattern in Arduino Core builds |
| **UI blocking** | Blocking operations in `loop()` starve `handleClient()`; async state machines with enforced delays are required for browser UI updates |

---

## Repository Structure

```
ai_repository/
├── cad/
│   ├── stl/
│   └── step/
├── firmware/
│   ├── standalone/          # v3.x line — standalone camera + OpenAI app
│   ├── automation_agent/    # v1.2.x line — Arduino UNO serial automation
│   └── arduino_uno/         # UNO-side controller firmware
├── hardware/
│   ├── dfr1154_pinout.png
│   └── dfr1154_function_diagram.png
├── docs/
│   ├── quick_start_guide.pdf
│   └── DeepResearch.md
├── tools/
│   └── wav_forensics/       # Python WAV analysis scripts
├── secrets_template.h       # Copy → secrets.h and fill in credentials
├── .gitignore
├── CHANGELOG.md
├── CONTRIBUTING.md
└── LICENSE
```

---

## Contributing

Pull requests and issue reports are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.

For CIJE educators and collaborators — the [Quick Start Guide](docs/quick_start_guide.pdf) covers setup from scratch without requiring any firmware background.

https://github.com/Twarner491/AvianVisitors/tree/avian-visitors
---

## Credits

- **Robert Jones** — Firmware architecture and developmentEngineer, former CIJE mentor
- **Orly Nader** — Director of Innovation, CIJE
- **Aryeh Laufer** — AI Process Consultant
- **DFRobot** — DFR1154 hardware platform

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
