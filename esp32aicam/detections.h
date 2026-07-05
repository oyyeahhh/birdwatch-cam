/*
 * detections.h - SD-backed detection log
 *
 * Images land in /birds/IMG_<n>.jpg; one JSON object per line appends to
 * /birds/detections.jsonl. A small RAM ring of the newest entries serves
 * /api/detections instantly; the JSONL file is the durable record (and,
 * in Phase 2, the upload queue). Detections below the review threshold
 * carry published=false and only surface in the review queue.
 */
#ifndef DETECTIONS_H
#define DETECTIONS_H

#include "SD_MMC.h"
#include <ArduinoJson.h>

struct Detection {
  uint32_t id = 0;
  time_t ts = 0;
  String commonName;
  String scientificName;
  int confidence = 0;
  int count = 1;
  String notes;
  String image;       // /birds/IMG_n.jpg
  bool published = true;
};

class Detections {
public:
  static const int RING = 60;
  static Detection ring[RING];
  static int ringCount;     // entries in ring (<= RING)
  static uint32_t nextId;

  static void begin() {
    if (!SD_MMC.exists("/birds")) SD_MMC.mkdir("/birds");
    loadTail();
    Serial.printf("Detections: %d loaded, next id %u\n", ringCount, nextId);
  }

  static String newImagePath() {
    return "/birds/IMG_" + String(nextId) + ".jpg";
  }

  static void add(Detection& d) {
    d.id = nextId++;
    if (d.ts == 0) d.ts = time(nullptr);
    // ring: newest first
    for (int i = min(ringCount, RING - 1); i > 0; i--) ring[i] = ring[i - 1];
    ring[0] = d;
    if (ringCount < RING) ringCount++;

    File f = SD_MMC.open("/birds/detections.jsonl", FILE_APPEND);
    if (f) {
      f.println(toJson(d));
      f.close();
    } else {
      Serial.println("Detections: JSONL append failed");
    }
  }

  static String toJson(const Detection& d) {
    JsonDocument doc;
    doc["id"] = d.id;
    doc["ts"] = (uint32_t)d.ts;
    doc["common_name"] = d.commonName;
    doc["scientific_name"] = d.scientificName;
    doc["confidence_pct"] = d.confidence;
    doc["count"] = d.count;
    doc["notes"] = d.notes;
    doc["image"] = d.image;
    doc["published"] = d.published;
    String out;
    serializeJson(doc, out);
    return out;
  }

  // JSON array of the newest entries (all of the ring, or fewer).
  static String listJson(int limit, bool publishedOnly) {
    String out = "[";
    int emitted = 0;
    for (int i = 0; i < ringCount && emitted < limit; i++) {
      if (publishedOnly && !ring[i].published) continue;
      if (emitted++) out += ",";
      out += toJson(ring[i]);
    }
    out += "]";
    return out;
  }

private:
  // Rebuild the RAM ring from the newest lines of the JSONL after boot.
  // The scratch buffer lives on the heap: RING Detection objects are ~6KB,
  // which overflows the 8KB loop-task stack (double-exception panic seen
  // on hardware when this was a local array).
  static void loadTail() {
    File f = SD_MMC.open("/birds/detections.jsonl", FILE_READ);
    if (!f) return;
    // Read from a byte offset that generously covers RING lines.
    const size_t APPROX_LINE = 320;
    size_t sz = f.size();
    size_t from = sz > RING * APPROX_LINE ? sz - RING * APPROX_LINE : 0;
    f.seek(from);
    if (from > 0) f.readStringUntil('\n');   // skip the partial line
    Detection* tmp = new Detection[RING];
    if (!tmp) { f.close(); return; }
    int n = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() < 2) continue;
      JsonDocument doc;
      if (deserializeJson(doc, line) != DeserializationError::Ok) continue;
      Detection d;
      d.id = doc["id"] | 0;
      d.ts = doc["ts"] | 0;
      d.commonName = doc["common_name"].as<String>();
      d.scientificName = doc["scientific_name"].as<String>();
      d.confidence = doc["confidence_pct"] | 0;
      d.count = doc["count"] | 1;
      d.notes = doc["notes"].as<String>();
      d.image = doc["image"].as<String>();
      d.published = doc["published"] | true;
      tmp[n % RING] = d;
      n++;
      if (d.id >= nextId) nextId = d.id + 1;
    }
    f.close();
    // Copy newest-first into the ring.
    int have = min(n, (int)RING);
    for (int i = 0; i < have; i++) {
      ring[i] = tmp[((n - 1 - i) % RING + RING) % RING];
    }
    ringCount = have;
    delete[] tmp;
  }
};

Detection Detections::ring[Detections::RING];
int Detections::ringCount = 0;
uint32_t Detections::nextId = 1;

#endif
