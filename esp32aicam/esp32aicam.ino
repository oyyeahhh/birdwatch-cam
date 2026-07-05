/*
 * BirdWatch Cam - Phase 1 firmware (standalone / local network)
 *
 * Forked from ai_camera standalone v3.2.5 (camera bring-up, credentials
 * captive portal, Wi-Fi supervision, SSL mode switch, chunked base64,
 * OpenAI HTTPS pattern). Audio/TTS stripped: the PDM mic shares I2S_NUM_0
 * with the camera DMA, so sound is permanently out of scope on this board.
 *
 * What this adds (design doc docs/DESIGN.md, Phase 1):
 *   - frame-differencing motion trigger (motion.h), daylight-gated by the
 *     LTR-308 light sensor (light_sensor.h)
 *   - two-stage identification: gpt-4o-mini bird gate, then gpt-4o full ID
 *     returning strict JSON with numeric confidence (bird_id.h)
 *   - detection log on SD + JSON API for the local UI (detections.h):
 *     GET  /api/status      health + today's funnel counters
 *     GET  /api/detections  newest detections (?all=1 includes review queue)
 *     GET  /api/config      current configuration
 *     POST /api/config      save configuration (JSON body)
 *     POST /api/capture     test capture (no AI call)
 *     POST /api/restart     reboot
 *     GET  /stream          MJPEG preview (for aiming at the feeder)
 *   - the BirdWatch local UI served from SD /web/ (web_api.h)
 *
 * The loop is an async state machine: no blocking waits between motion
 * checks; the only long operations are the OpenAI calls themselves, which
 * pump server.handleClient() at their slow points (same pattern v3.2.5
 * used for analysis).
 *
 * Sharing mode is stored and reported from day one, but Phase 1 publishes
 * nothing anywhere: results stay on this camera regardless of mode.
 */

#include "time.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "SD_MMC.h"
#include "FS.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"

#define FIRMWARE_VERSION "0.1.0"
#define FIRMWARE_BASE "v3.2.5"

// SSL mode (see ssl_validation.h): 0 = insecure (v3.2.5 default),
// 2 = root CA validation. Phase 2 flips the default to 2 and extends
// the CA bundle to Supabase.
#define SSL_SECURITY_MODE 0
int currentSSLMode = SSL_SECURITY_MODE;

// ---- First-run defaults ----
// Leave the sentinels as-is to get the captive-portal setup wizard on
// first boot (credentials_manager.h treats "YourWiFiSSID" / "sk-Your..."
// as unset). Replace them only to bake credentials in at compile time.
#define DEFAULT_WIFI_SSID     "YourWiFiSSID"
#define DEFAULT_WIFI_PASSWORD "YourPassword"
#define DEFAULT_OPENAI_API_KEY "sk-Your-OpenAI-Key"
#define DEFAULT_DEVICE_NAME   "BirdWatch-Cam"

// ---- DFR1154 pin map (from v3.2.5) ----
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 5
#define SIOD_GPIO_NUM 8
#define SIOC_GPIO_NUM 9
#define Y9_GPIO_NUM 4
#define Y8_GPIO_NUM 6
#define Y7_GPIO_NUM 7
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 17
#define Y4_GPIO_NUM 21
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 16
#define VSYNC_GPIO_NUM 1
#define HREF_GPIO_NUM 2
#define PCLK_GPIO_NUM 15
#define SD_MMC_CMD 11
#define SD_MMC_CLK 12
#define SD_MMC_D0 13

WebServer server(80);
volatile bool analysisInProgress = false;   // referenced by error_handling.h WiFi guard

#include "credentials_manager.h"
#include "ssl_validation.h"
#include "error_handling.h"
#include "bird_config.h"
#include "light_sensor.h"
#include "motion.h"
#include "bird_id.h"
#include "detections.h"
#include "web_api.h"

// ---- Runtime state ----
unsigned long bootMillis = 0;
bool sdOk = false;
unsigned long lastMotionCheck = 0;
unsigned long cooldownUntil = 0;
unsigned long lastLightRead = 0;
unsigned long lastCounterSave = 0;
float lastLux = -1;
bool daylight = true;
String lastErrors[4];
int lastErrorCount = 0;

const unsigned long MOTION_INTERVAL_MS = 500;
const unsigned long LIGHT_INTERVAL_MS = 10000;

void noteError(String msg) {
  Serial.println("ERROR: " + msg);
  for (int i = 3; i > 0; i--) lastErrors[i] = lastErrors[i - 1];
  struct tm t;
  String stamp = "";
  if (getLocalTime(&t, 0)) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    stamp = String(buf) + "  ";
  }
  lastErrors[0] = stamp + msg;
  if (lastErrorCount < 4) lastErrorCount++;
}

// ---- Camera (from v3.2.5, SVGA JPEG) ----
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;   // 800x600: motion decode /8 = 100x75
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 0);
  s->set_hmirror(s, 1);
}

// ---- Detection pipeline (runs when motion fires and all gates pass) ----
// The frame is saved to SD and RELEASED before the slow API calls, so the
// single camera frame buffer isn't held hostage for the 10-30s round trip.
bool savePipelineImage(camera_fb_t* fb, String& path) {
  path = Detections::newImagePath();
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    noteError("SD write failed for " + path);
    return false;
  }
  f.write(fb->buf, fb->len);
  f.close();
  Serial.printf("Pipeline: saved %s (%u bytes)\n", path.c_str(), fb->len);
  return true;
}

void runPipeline(String path) {
  analysisInProgress = true;

  // 2. Stage 1 - cheap bird gate.
  BirdConfig::apiCallsToday++;
  int gate = BirdID::stage1HasBird(path);
  if (gate < 0) {
    noteError("stage1 call failed");
    SD_MMC.remove(path);
    analysisInProgress = false;
    return;
  }
  if (gate == 0) {
    Serial.println("Pipeline: stage1 says no bird - discarding");
    SD_MMC.remove(path);
    analysisInProgress = false;
    return;
  }
  BirdConfig::stage1PassToday++;

  // 3. Stage 2 - full identification.
  BirdConfig::apiCallsToday++;
  BirdResult r = BirdID::identify(path, BirdConfig::regionHint);
  if (!r.ok) {
    noteError(r.error.length() ? r.error : "stage2 failed");
    analysisInProgress = false;
    return;
  }
  if (!r.birdPresent) {
    Serial.println("Pipeline: stage2 says no bird - discarding");
    SD_MMC.remove(path);
    analysisInProgress = false;
    return;
  }
  BirdConfig::idsToday++;

  // 4. Log. Below the review threshold: held out of the gallery.
  Detection d;
  d.commonName = r.commonName;
  d.scientificName = r.scientificName;
  d.confidence = r.confidence;
  d.count = r.count;
  d.notes = r.notes;
  d.image = path;
  d.published = r.confidence >= BirdConfig::revThreshold;
  Detections::add(d);
  if (d.published) BirdConfig::publishedToday++;
  BirdConfig::saveCounters();

  Serial.printf("Pipeline: %s (%s) %d%% -> %s\n",
                r.commonName.c_str(), r.scientificName.c_str(), r.confidence,
                d.published ? "gallery" : "review queue");
  analysisInProgress = false;
}

// The non-blocking heart: called every loop() pass.
void birdLoop() {
  unsigned long now = millis();

  if (now - lastLightRead >= LIGHT_INTERVAL_MS || lastLightRead == 0) {
    lastLightRead = now;
    lastLux = LightSensor::readLux();
    daylight = LightSensor::isDaylight(BirdConfig::luxThreshold);
    BirdConfig::rollDayIfNeeded();
  }
  if (now - lastCounterSave >= 60000) {
    lastCounterSave = now;
    BirdConfig::saveCounters();
  }

  if (analysisInProgress) return;
  // Warm-up: the sensor's auto-exposure settles over the first seconds
  // after boot, which reads as a whole-frame luma change (202/300 cells
  // on the bench) and would fire a false trigger + API call.
  if (now - bootMillis < 12000) return;
  if (now - lastMotionCheck < MOTION_INTERVAL_MS) return;
  lastMotionCheck = now;

  if (BirdConfig::daylightOnly && !daylight) return;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;
  bool motion = MotionDetector::check(fb, BirdConfig::motionPct);

  bool gatesOpen = now >= cooldownUntil && BirdConfig::underApiCap()
                   && WiFiManager::isCurrentlyConnected();
  if (motion && gatesOpen) {
    Serial.printf("Motion: %d cells changed - pipeline start\n", MotionDetector::lastChangedCells);
    BirdConfig::triggersToday++;
    // The triggering frame is mid-motion (a bird still landing, wings
    // blurred). Let the subject settle, then capture a fresh frame for
    // the analysis - that's the photo worth identifying and keeping.
    esp_camera_fb_return(fb);
    delay(450);
    camera_fb_t* shot = esp_camera_fb_get();
    if (shot) {
      String path;
      bool saved = sdOk && savePipelineImage(shot, path);
      esp_camera_fb_return(shot);
      if (saved) runPipeline(path);
    }
    cooldownUntil = millis() + (unsigned long)BirdConfig::cooldownS * 1000;
    MotionDetector::reset();
    return;
  }
  if (motion && !BirdConfig::underApiCap()) {
    static unsigned long lastCapNote = 0;
    if (now - lastCapNote > 600000) {
      lastCapNote = now;
      noteError("daily API cap reached (" + String(BirdConfig::dailyCap) + ")");
    }
  }
  esp_camera_fb_return(fb);
}

// ---- JSON API ----
void handleApiStatus() {
  JsonDocument doc;
  doc["device_id"] = BirdConfig::deviceId;
  doc["name"] = BirdConfig::friendlyName;
  doc["fw"] = FIRMWARE_VERSION;
  doc["fw_base"] = FIRMWARE_BASE;
  doc["mode"] = BirdConfig::sharingMode;
  doc["uptime_s"] = (uint32_t)((millis() - bootMillis) / 1000);
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["psram_free"] = ESP.getFreePsram();
  doc["sd_ok"] = sdOk;
  doc["sd_used_mb"] = sdOk ? (uint32_t)(SD_MMC.usedBytes() / 1048576ULL) : 0;
  doc["sd_total_mb"] = sdOk ? (uint32_t)(SD_MMC.totalBytes() / 1048576ULL) : 0;
  doc["lux"] = lastLux;
  doc["light_sensor"] = LightSensor::present;
  doc["daylight"] = daylight;
  doc["detecting"] = (!BirdConfig::daylightOnly || daylight) && BirdConfig::underApiCap();
  doc["analysis_in_progress"] = (bool)analysisInProgress;
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    doc["time"] = buf;
  }
  JsonObject c = doc["counters"].to<JsonObject>();
  c["triggers"] = BirdConfig::triggersToday;
  c["stage1_pass"] = BirdConfig::stage1PassToday;
  c["identified"] = BirdConfig::idsToday;
  c["published"] = BirdConfig::publishedToday;
  c["api_calls"] = BirdConfig::apiCallsToday;
  c["api_cap"] = BirdConfig::dailyCap;
  JsonArray errs = doc["errors"].to<JsonArray>();
  for (int i = 0; i < lastErrorCount; i++) errs.add(lastErrors[i]);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiDetections() {
  int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 50;
  bool all = server.hasArg("all") && server.arg("all") == "1";
  limit = constrain(limit, 1, Detections::RING);
  String out = "{\"detections\":" + Detections::listJson(limit, !all) + "}";
  server.send(200, "application/json", out);
}

void handleApiConfigGet() {
  JsonDocument doc;
  doc["device_id"] = BirdConfig::deviceId;
  doc["name"] = BirdConfig::friendlyName;
  doc["region_hint"] = BirdConfig::regionHint;
  doc["sharing_mode"] = BirdConfig::sharingMode;
  doc["pub_threshold"] = BirdConfig::pubThreshold;
  doc["rev_threshold"] = BirdConfig::revThreshold;
  doc["cooldown_s"] = BirdConfig::cooldownS;
  doc["daily_cap"] = BirdConfig::dailyCap;
  doc["daylight_only"] = BirdConfig::daylightOnly;
  doc["lux_threshold"] = BirdConfig::luxThreshold;
  doc["motion_pct"] = BirdConfig::motionPct;
  // Never expose the key itself, just whether a real one is stored.
  doc["openai_key_set"] = CredentialsManager::getOpenAIKey().startsWith("sk-")
                          && CredentialsManager::getOpenAIKey().indexOf("pending") < 0
                          && CredentialsManager::getOpenAIKey().indexOf("Your") < 0;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleApiConfigPost() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
    return;
  }
  if (doc["device_id"].is<String>())   BirdConfig::deviceId = doc["device_id"].as<String>();
  if (doc["name"].is<String>())        BirdConfig::friendlyName = doc["name"].as<String>();
  if (doc["region_hint"].is<String>()) BirdConfig::regionHint = doc["region_hint"].as<String>();
  if (doc["sharing_mode"].is<String>()) {
    String m = doc["sharing_mode"].as<String>();
    if (m == "local" || m == "diag" || m == "cloud") BirdConfig::sharingMode = m;
  }
  if (doc["pub_threshold"].is<int>()) BirdConfig::pubThreshold = constrain(doc["pub_threshold"].as<int>(), 50, 100);
  if (doc["rev_threshold"].is<int>()) BirdConfig::revThreshold = constrain(doc["rev_threshold"].as<int>(), 0, 79);
  if (doc["cooldown_s"].is<int>())    BirdConfig::cooldownS = constrain(doc["cooldown_s"].as<int>(), 5, 600);
  if (doc["daily_cap"].is<int>())     BirdConfig::dailyCap = constrain(doc["daily_cap"].as<int>(), 2, 500);
  if (doc["daylight_only"].is<bool>()) BirdConfig::daylightOnly = doc["daylight_only"].as<bool>();
  if (doc["lux_threshold"].is<int>()) BirdConfig::luxThreshold = constrain(doc["lux_threshold"].as<int>(), 1, 5000);
  if (doc["motion_pct"].is<int>())    BirdConfig::motionPct = constrain(doc["motion_pct"].as<int>(), 1, 80);
  // Allow updating the OpenAI key from the config tab (design doc §4)
  // so a key change never requires a factory reset.
  if (doc["openai_key"].is<String>()) {
    String k = doc["openai_key"].as<String>();
    k.trim();
    if (k.startsWith("sk-") && k.length() > 20) {
      CredentialsManager::saveCredentials(
        CredentialsManager::getWiFiSSID(), CredentialsManager::getWiFiPassword(),
        k, CredentialsManager::getDeviceName(),
        CredentialsManager::getUseStaticIP(), CredentialsManager::getStaticIP(),
        CredentialsManager::getGateway(), CredentialsManager::getSubnet(),
        CredentialsManager::getDNS());
      CredentialsManager::loadCredentials();
      Serial.println("Config: OpenAI key updated");
    }
  }
  BirdConfig::save();
  server.send(200, "application/json", "{\"saved\":true}");
}

void handleApiCapture() {
  if (!sdOk) {
    server.send(500, "application/json", "{\"error\":\"no SD card\"}");
    return;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "application/json", "{\"error\":\"capture failed\"}");
    return;
  }
  String path = "/birds/TEST_" + String(millis()) + ".jpg";
  File f = SD_MMC.open(path, FILE_WRITE);
  bool ok = false;
  if (f) {
    f.write(fb->buf, fb->len);
    f.close();
    ok = true;
  }
  size_t len = fb->len;
  esp_camera_fb_return(fb);
  if (!ok) {
    server.send(500, "application/json", "{\"error\":\"SD write failed\"}");
    return;
  }
  server.send(200, "application/json",
              "{\"saved\":\"" + path + "\",\"bytes\":" + String(len) + "}");
}

// MJPEG preview for aiming the camera at the feeder (from v3.2.5 pattern).
// Streaming monopolizes the loop (detection pauses), so a single viewer
// is capped at 90s — the local UI's Live tab has a "restart preview"
// button for longer aiming sessions.
void handleStream() {
  WiFiClient client = server.client();
  unsigned long streamEnd = millis() + 90000UL;
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println();
  while (client.connected() && millis() < streamEnd) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) break;
    client.println("--frame");
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.println();
    esp_camera_fb_return(fb);
    if (analysisInProgress) break;   // free the camera for the pipeline
    delay(66);                       // ~15 fps
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("BirdWatch Cam v" FIRMWARE_VERSION " (base " FIRMWARE_BASE ")");
  Serial.println("========================================\n");
  bootMillis = millis();

  // Light sensor first (official DFR1154 examples init it before the camera).
  LightSensor::begin();

  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (SD_MMC.begin("/sdcard", true)) {
    sdOk = true;
    Serial.println("SD Card OK");
  } else {
    // Keep going: /stream, /api/status and configuration still work,
    // so the camera can be aimed and diagnosed before a card arrives.
    Serial.println("WARNING: SD Card Mount Failed - no photos or web UI");
    noteError("SD card not mounted");
  }

  setupCamera();
  Serial.println("Camera OK");

  BirdConfig::begin();
  if (sdOk) Detections::begin();
  if (!MotionDetector::begin(800 / 8, 600 / 8)) {
    Serial.println("ERROR: motion detector init failed");
  }

  if (!CredentialsManager::begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD,
                                 DEFAULT_OPENAI_API_KEY, DEFAULT_DEVICE_NAME)) {
    Serial.println("ERROR: Credentials manager failed");
    while (1) { delay(1000); }
  }

  // Routers often refuse quick reassociations right after a reboot, so a
  // single failed round doesn't mean the credentials are bad. Retry a few
  // rounds; only then assume wrong/stale credentials (e.g. the school
  // changed its Wi-Fi password) and reopen the setup wizard rather than
  // bricking until a 10s BOOT-button factory reset.
  bool wifiUp = false;
  for (int round = 1; round <= 3 && !wifiUp; round++) {
    if (round > 1) {
      Serial.printf("WiFi retry round %d/3 in 5s...\n", round);
      delay(5000);
    }
    wifiUp = WiFiManager::connect(CredentialsManager::getWiFiSSID().c_str(),
                                  CredentialsManager::getWiFiPassword().c_str());
  }
  if (!wifiUp) {
    Serial.println("ERROR: WiFi failed after 3 rounds - reopening setup wizard");
    CredentialsManager::startSetupWizard();
    ESP.restart();
  }

  // Local time for timestamps + midnight counter rollover (default US-East;
  // adjust here if a school is elsewhere).
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");

  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/detections", HTTP_GET, handleApiDetections);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.on("/api/capture", HTTP_POST, handleApiCapture);
  server.on("/api/restart", HTTP_POST, []() {
    server.send(200, "application/json", "{\"restarting\":true}");
    delay(300);
    ESP.restart();
  });
  server.on("/stream", HTTP_GET, handleStream);
  server.onNotFound(WebFiles::handleNotFound);
  server.begin();

  Serial.println("\nBirdWatch Cam ready:");
  Serial.println("  http://" + WiFi.localIP().toString() + "/");
  Serial.printf("  daylight-only=%s lux=%.0f cap=%d/day cooldown=%ds motion=%d%%\n",
                BirdConfig::daylightOnly ? "yes" : "no", LightSensor::readLux(),
                BirdConfig::dailyCap, BirdConfig::cooldownS, BirdConfig::motionPct);
}

void loop() {
  server.handleClient();
  WiFiManager::monitor();
  birdLoop();
  yield();
}
