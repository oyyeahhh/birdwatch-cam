/*
 * bird_id.h - two-stage bird identification via OpenAI vision
 *
 * Stage 1 (cheap gate, gpt-4o-mini): "is a bird clearly visible?" —
 * filters squirrels, branches, and window reflections for a fraction
 * of a cent before the full call.
 *
 * Stage 2 (full ID, gpt-4o): species + numeric confidence as strict
 * JSON, with the camera's region hint folded into the prompt.
 *
 * Both calls reuse the v3.2.5 firmware's proven building blocks:
 * chunked base64 encoding from SD, manual JSON body assembly (memory
 * control), WiFiClientSecure via the ssl_validation.h mode switch.
 */
#ifndef BIRD_ID_H
#define BIRD_ID_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "mbedtls/base64.h"
#include "ssl_validation.h"
#include "credentials_manager.h"

extern int currentSSLMode;
extern WebServer server;

static const char* OPENAI_CHAT_URL = "https://api.openai.com/v1/chat/completions";
#define STAGE1_MODEL "gpt-4o-mini"
#define STAGE2_MODEL "gpt-4o"

struct BirdResult {
  bool ok = false;          // stage-2 call + parse succeeded
  bool birdPresent = false;
  String commonName;
  String scientificName;
  int confidence = 0;       // 0..100
  int count = 1;
  String notes;
  String error;
};

class BirdID {
public:
  // Escape a user-editable string for embedding in the hand-built JSON body.
  static String escapeJson(String s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\n", " ");
    s.replace("\r", " ");
    return s;
  }

  static WiFiClientSecure* createSecureClient() {
    WiFiClientSecure* client = new WiFiClientSecure();
    switch (currentSSLMode) {
      case 1: delete client; client = SecureOpenAIClient_Pinned::createClient(); break;
      case 2: delete client; client = SecureOpenAIClient_RootCA::createClient(); break;
      default: client->setInsecure(); client->setTimeout(30); break;
    }
    return client;
  }

  // Stage 1: returns 1 = bird, 0 = no bird, -1 = call failed.
  static int stage1HasBird(String imagePath) {
    String content = postVision(STAGE1_MODEL,
      "Is there a bird clearly visible in this image? Respond with JSON only: {\\\"bird\\\": true} or {\\\"bird\\\": false}.",
      imagePath, 16, "low");
    if (content.startsWith("Error")) return -1;
    JsonDocument doc;
    if (deserializeJson(doc, content) != DeserializationError::Ok) {
      Serial.println("Stage1: parse failed: " + content);
      return -1;
    }
    return doc["bird"].as<bool>() ? 1 : 0;
  }

  // Stage 2: full identification with numeric confidence.
  static BirdResult identify(String imagePath, String regionHint) {
    BirdResult r;
    String prompt =
      "You are a bird identification expert. This photo is from a fixed camera at a "
      + escapeJson(regionHint) +
      ", photographed through a window, so expect some blur and glare - lower your "
      "confidence for blurry or ambiguous shots. Identify the most prominent bird. "
      "Respond with JSON only, exactly this shape: "
      "{\\\"bird_present\\\": true, \\\"common_name\\\": \\\"Northern Cardinal\\\", "
      "\\\"scientific_name\\\": \\\"Cardinalis cardinalis\\\", \\\"confidence_pct\\\": 92, "
      "\\\"count\\\": 1, \\\"notes\\\": \\\"adult male, at feeder\\\"} "
      "confidence_pct is an integer 0-100. If no bird is visible set bird_present to "
      "false and confidence_pct to 0.";
    // Escape for embedding in the JSON request body we build by hand.
    prompt.replace("\n", " ");
    String content = postVision(STAGE2_MODEL, prompt, imagePath, 200, "auto");
    if (content.startsWith("Error")) { r.error = content; return r; }
    JsonDocument doc;
    if (deserializeJson(doc, content) != DeserializationError::Ok) {
      r.error = "Error: stage2 JSON parse failed";
      Serial.println(r.error + ": " + content);
      return r;
    }
    r.ok = true;
    r.birdPresent = doc["bird_present"].as<bool>();
    r.commonName = doc["common_name"].as<String>();
    r.scientificName = doc["scientific_name"].as<String>();
    r.confidence = doc["confidence_pct"].as<int>();
    r.count = doc["count"] | 1;
    r.notes = doc["notes"].as<String>();
    return r;
  }

private:
  // POST one image + prompt to the chat completions endpoint; returns the
  // message content, or a string starting with "Error".
  static String postVision(const char* model, String promptText, String imagePath,
                           int maxTokens, const char* detail) {
    if (ESP.getFreeHeap() < 60000) return "Error: low memory";

    WiFiClientSecure* client = createSecureClient();
    HTTPClient https;
    if (!https.begin(*client, OPENAI_CHAT_URL)) {
      delete client;
      return "Error: HTTPS begin failed";
    }
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + CredentialsManager::getOpenAIKey());
    https.setTimeout(60000);

    String body = "{\"model\":\"" + String(model) +
      "\",\"max_tokens\":" + String(maxTokens) +
      ",\"response_format\":{\"type\":\"json_object\"}" +
      ",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"" +
      promptText + "\"},{\"type\":\"image_url\",\"image_url\":{\"detail\":\"" + String(detail) +
      "\",\"url\":\"data:image/jpeg;base64,";

    String b64 = base64EncodeFile(imagePath);
    if (b64.length() == 0) {
      https.end(); delete client;
      return "Error: base64 encoding failed";
    }
    body += b64;
    b64 = "";
    body += "\"}}]}]}";
    yield();
    server.handleClient();   // stay responsive during the slow part

    Serial.printf("BirdID: POST %s (%d bytes body)\n", model, body.length());
    int code = https.POST(body);
    body = "";

    String content;
    if (code == 200) {
      String resp = https.getString();
      JsonDocument doc;
      if (deserializeJson(doc, resp) == DeserializationError::Ok) {
        content = doc["choices"][0]["message"]["content"].as<String>();
      } else {
        content = "Error: response parse failed";
      }
    } else if (code > 0) {
      Serial.println("BirdID: HTTP " + String(code) + " " + https.getString().substring(0, 200));
      content = "Error: HTTP " + String(code);
    } else {
      content = "Error: connection failed";
    }
    https.end();
    delete client;
    return content;
  }

  // Chunked base64 from SD (from v3.2.5 base64EncodeFileOptimized).
  static String base64EncodeFile(String filePath) {
    File file = SD_MMC.open(filePath, FILE_READ);
    if (!file) return "";
    size_t fileSize = file.size();
    String result = "";
    result.reserve(((fileSize + 2) / 3) * 4 + 100);
    const size_t chunkSize = 3000;   // multiple of 3 for clean concatenation
    uint8_t* in = (uint8_t*)malloc(chunkSize);
    uint8_t* out = (uint8_t*)malloc(chunkSize * 2);
    if (!in || !out) {
      if (in) free(in);
      if (out) free(out);
      file.close();
      return "";
    }
    while (file.available()) {
      size_t n = file.read(in, chunkSize);
      if (n == 0) break;
      size_t outLen = 0;
      if (mbedtls_base64_encode(out, chunkSize * 2, &outLen, in, n) != 0) break;
      for (size_t i = 0; i < outLen; i++) result += (char)out[i];
      yield();
    }
    file.close();
    free(in);
    free(out);
    return result;
  }
};

#endif
