# esp32aicam — BirdWatch Cam firmware (Phase 1, standalone)

Forked from `ai_camera/firmware/standalone/ESP32_AI_Camera_v3_2_5`. Runs the
full Local Only product from the design doc: motion-triggered capture,
two-stage OpenAI bird identification, detection log on SD, and the BirdWatch
local web UI at the camera's IP. Nothing is published anywhere — results stay
on the camera and its network.

## What's inherited vs. new

| From v3.2.5 | New here |
|---|---|
| Camera bring-up (`setupCamera`, DFR1154 pin map) | `motion.h` — frame-differencing trigger (1/8-scale decode, 20×15 luma grid) |
| `credentials_manager.h` — NVS + captive-portal Wi-Fi/key setup | `bird_id.h` — stage-1 gpt-4o-mini bird gate, stage-2 gpt-4o ID with JSON confidence |
| `ssl_validation.h` — SSL mode switch (insecure default; root-CA path ready for Phase 2) | `bird_config.h` — thresholds, cooldown, daily cap, sharing mode, daily counters in NVS |
| `error_handling.h` — Wi-Fi supervision, retry/backoff (audio/TTS parts removed) | `light_sensor.h` — LTR-308 register driver, daylight gate |
| Chunked base64 + manual JSON body pattern for OpenAI | `detections.h` — JSONL log + RAM ring, `web_api.h` — SD-served UI |

Audio/TTS is stripped entirely: the PDM mic and camera DMA share `I2S_NUM_0`.

## Build

Arduino IDE or CLI, ESP32 core 3.x, ArduinoJson 7.

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app" esp32aicam
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app" -p /dev/cu.usbmodem* esp32aicam
```

## SD card layout

```
/web/index.html            <- copy of web/local/index.html
/web/assets/illustrations/ <- copy of web/assets/illustrations/
/birds/                    <- created by firmware: IMG_n.jpg + detections.jsonl
```

Without `/web`, a built-in fallback page appears with links to the JSON API.

## First boot

1. No saved credentials → the captive portal from v3.2.5 opens
   (`AI-Camera-Setup` access point) for Wi-Fi + OpenAI key.
2. Serial monitor (115200) prints the assigned IP.
3. Open `http://<ip>/` — the local UI. `http://<ip>/stream` shows a live
   MJPEG preview for aiming the camera at the feeder.

## JSON API

| Route | Method | Purpose |
|---|---|---|
| `/api/status` | GET | health, lux, today's trigger→gate→ID→published funnel, API budget, recent errors |
| `/api/detections?limit=50&all=1` | GET | newest detections (`all=1` includes the sub-threshold review queue) |
| `/api/config` | GET/POST | read / save configuration (JSON body on POST) |
| `/api/capture` | POST | test capture to SD, no AI call |
| `/api/restart` | POST | reboot |
| `/stream` | GET | MJPEG preview |

## Defaults (all editable via `/api/config`, stored in NVS)

publish threshold 80 · review threshold 50 · cooldown 45 s · daily API cap 60
· daylight-only on (lux ≥ 30) · motion sensitivity 8 % of grid cells ·
sharing mode `local` (stored but inert until Phase 2).
