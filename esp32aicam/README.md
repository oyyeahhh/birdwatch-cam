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

Board: **DFRobot ESP32-S3 AI CAM (DFR1154)** — select "ESP32S3 Dev Module"
with USB CDC On Boot Enabled, Flash 16MB, PSRAM: OPI PSRAM, Partition:
Huge APP. Full walkthrough in [docs/SETUP.md](../docs/SETUP.md).

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" esp32aicam
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" -p /dev/cu.usbmodem* esp32aicam
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
| `/api/reset-counters` | POST | manually zero today's counters (escape hatch if NTP never syncs) |
| `/api/capture` | POST | test capture to SD, no AI call |
| `/api/restart` | POST | reboot |
| `/stream` | GET | MJPEG preview |

## Changelog

Versioning follows [semver](https://semver.org): MAJOR.MINOR.PATCH.

### [0.2.0] - 2026-07-28

**Fixed**
- Light sensor permanently read 0 lux / daylight gate never opened.
  `setupCamera()` and `LightSensor::begin()` both default to GPIO8/9 on
  this board (camera SCCB vs. Wire); whichever ran second silently won
  the pins. Camera now initializes first, since it only touches SCCB
  once at boot, letting `Wire` own the bus uncontested afterward.
- Daily counters (API calls, triggers, etc.) could silently never reset
  at midnight if NTP failed to sync (e.g. blocked outbound UDP/123) -
  `configTzTime()` never confirmed success. Boot now waits up to ~10s
  for a confirmed sync and logs/reports failure instead of staying
  silent.
- `/api/config` numeric/boolean fields could be silently ignored (strict
  `is<int>()`/`is<bool>()` rejected values sent as JSON strings) or, in
  an earlier attempted fix, silently coerced from blank strings to `0`
  and clamped to the range floor. Validation now accepts real numbers
  or clean numeric strings and ignores blank/malformed ones.
- Motion false-triggers at dawn: the motion detector's reference frame
  went stale overnight (never updated while the daylight gate was
  closed), so the first check after sunrise always compared against a
  frame from the previous dusk - guaranteed false trigger on lighting
  alone. Baseline now resets the moment the gate opens, plus a 15s
  settling window for auto-exposure to catch up.
- Motion false-triggers continuing through the sunrise ramp: each
  auto-exposure step shifts nearly the whole frame's brightness at
  once, which used to read as motion. Frames where ≥60% of all grid
  cells change simultaneously are now treated as a lighting shift, not
  a trigger.
- The synchronous web server could only service one connection at a
  time; an open live-view `/stream` tab could block `/api/config` and
  `/api/upload` requests for up to 90s. Stream's max duration per
  connection shortened to 20s.

**Added**
- `POST /api/reset-counters` - manual escape hatch to zero today's
  counters if NTP never syncs and the automatic midnight rollover can't
  run.
- `time_synced`, `cooldown_remaining_s`, and `recent_triggers` fields on
  `/api/status`, so cooldown/NTP behavior can be checked after the fact
  instead of needing a live serial connection at the exact moment
  something looks wrong.
- Light sensor boot diagnostics: logs register readback (`ctrl`/`rate`/
  `gain`) and raw ALS bytes, so a future I2C fault is visible in serial
  immediately instead of just reading 0 with no explanation.
- Rejected motion triggers ("no bird in either frame") now keep the
  most recent capture at `/birds/_last_miss.jpg` instead of deleting it,
  so a false-trigger spike can be visually diagnosed after the fact.

### [0.1.0] - 2026-07-07

Initial standalone fork from `ai_camera/firmware/standalone/ESP32_AI_Camera_v3_2_5`.

## Defaults (all editable via `/api/config`, stored in NVS)

publish threshold 80 · review threshold 50 · cooldown 45 s · daily API cap 60
· daylight-only on (lux ≥ 30) · motion sensitivity 8 % of grid cells ·
sharing mode `local` (stored but inert until Phase 2).
