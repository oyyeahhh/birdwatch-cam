# BirdWatch Cam — camera setup guide

Step-by-step setup for the **DFRobot ESP32-S3 AI CAM (DFR1154)** running the
Phase 1 firmware in `esp32aicam/`. No coding required — flashing takes about
15 minutes, and after that everything is done from a web browser.

## 1. What you need

- DFRobot ESP32-S3 AI CAM (DFR1154)
- USB-C data cable (some charge-only cables won't work)
- microSD card, 32 GB or smaller, formatted **FAT32**
- 2.4 GHz Wi-Fi network name and password (the board doesn't do 5 GHz)
- An OpenAI API key (starts with `sk-`) — https://platform.openai.com/api-keys
- A computer with the Arduino IDE (or `arduino-cli`)
- Window mount + a feeder you can place 2–5 feet from the glass

## 2. Prepare the SD card

Copy the web interface from this repo onto the card:

```
SD card
├── web/
│   ├── index.html          <- copy of  web/local/index.html
│   └── assets/
│       └── illustrations/  <- copy of  web/assets/illustrations/
```

On macOS, with the card mounted as `/Volumes/SDCARD`:

```bash
mkdir -p /Volumes/SDCARD/web
cp web/local/index.html /Volumes/SDCARD/web/index.html
cp -r web/assets /Volumes/SDCARD/web/assets
```

**Alternative — install the UI over Wi-Fi** (no card reader needed): if the
camera is already flashed and on the network, push the files to it with the
helper script instead of moving the card:

```bash
tools/push_web.sh 192.168.1.182     # the camera's IP
```

This uploads `index.html` and every illustration to the card via the
firmware's `/api/upload` endpoint — the same way UI updates are delivered
later without pulling the card.

Insert the card into the camera **before** powering on. (Without it, the
camera still boots and shows a plain fallback page, but nothing can be
saved.)

## 3. Flash the firmware

### Option A — Arduino IDE

1. Install the Arduino IDE, then in **File → Preferences → Additional board
   manager URLs** add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. **Tools → Board → Boards Manager** — install **esp32 by Espressif**
   (version 3.x).
3. **Sketch → Include Library → Manage Libraries** — install **ArduinoJson**
   (version 7.x).
4. Open `esp32aicam/esp32aicam.ino`.
5. Set the Tools menu exactly as the DFRobot wiki specifies for this board:

   | Tools setting | Value |
   |---|---|
   | Board | **ESP32S3 Dev Module** |
   | USB CDC On Boot | **Enabled** |
   | Flash Size | **16MB (128Mb)** |
   | PSRAM | **OPI PSRAM** |
   | Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
   | Port | the port that appears when you plug the camera in |

6. Click **Upload**. If the port never appears, hold the board's **BOOT**
   button while plugging in the USB cable, then upload.

### Option B — command line

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" esp32aicam
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" -p /dev/cu.usbmodem* esp32aicam
```

## 4. First boot — connect it to Wi-Fi

1. Power the camera (USB-C wall adapter is fine from here on).
2. On your phone or laptop, join the Wi-Fi network **`AI-Camera-Setup`**.
   A setup page opens automatically (if not, browse to `http://192.168.4.1`).
3. Enter your school/home **Wi-Fi name and password**, your **OpenAI API
   key**, and a device name. Save — the camera reboots and joins your
   network.

## 5. Find the camera and open the app

- Open the Arduino IDE **Serial Monitor** at **115200 baud** and press the
  camera's reset button: it prints its address, e.g.
  `http://192.168.0.181/`.
- Open that address in any browser on the same network. You should see the
  BirdWatch app (collage · stats · atlas · health · config).

Ask whoever runs the router to give the camera a **DHCP reservation** so the
address never changes (the current IP always shows at the bottom of the
config tab).

## 6. Aim the camera

1. Browse to `http://<camera-ip>/stream` — a live video preview.
2. Mount the camera with the lens **pressed against the window glass**
   (the gasket mount stops reflections). No lights behind the camera.
3. Place the feeder 2–5 feet away with a fixed perch, centered in the
   preview. Clean the glass.

## 7. Configure

In the app's **config** tab:

- **Device ID** — `org-site-window` style, e.g. `yst-nj-lab`
- **Region hint** — e.g. *"Backyard feeder in northern New Jersey"* (this
  goes into the AI prompt and noticeably improves identification)
- Leave the tuning defaults to start: publish 80%, review 50%, cooldown
  45 s, 60 API calls/day, daylight-only on
- **Sharing mode: local** — in Phase 1 nothing ever leaves your network
  regardless of this setting

Press **Save** (settings persist across reboots).

## 8. Verify it works

- Config tab → **test capture**: saves a photo to the SD card without
  spending an API call.
- Wave a hand in front of the feeder: within a minute the **health** tab's
  funnel should count a motion trigger. If a real bird lands, expect
  `triggers → bird gate → identified → in gallery` to advance and the bird
  to appear on the collage.

## Troubleshooting

| Symptom | Fix |
|---|---|
| No `AI-Camera-Setup` network | Hold BOOT 10 s while plugging in to factory-reset credentials |
| "SD Card Mount Failed" in serial monitor | Reformat the card FAT32, ≤32 GB; reseat it |
| Plain fallback page instead of the app | The `/web` folder is missing on the SD card (step 2) |
| Light level shows "no sensor" | Harmless — detection runs without the daylight gate |
| Triggers but never identifies | Check the OpenAI key and that the account has credit; errors appear on the health tab |
| Constant false triggers | Lower motion sensitivity (config), check for moving branches/reflections in the preview |
| Camera unreachable after a while | IP changed — get a DHCP reservation (step 5) |
