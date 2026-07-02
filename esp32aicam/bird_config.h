/*
 * bird_config.h - BirdWatch Cam persistent configuration + daily counters
 *
 * Everything the Configuration tab edits lives here, stored in NVS so it
 * survives reboot. Sharing mode is stored from day one (multi-site ready)
 * but only "local" is functional in Phase 1 — the publish step lands in
 * Phase 2 and reads the same field.
 */
#ifndef BIRD_CONFIG_H
#define BIRD_CONFIG_H

#include <Preferences.h>
#include "time.h"

class BirdConfig {
public:
  // Identity
  static String deviceId;      // org-site-window, e.g. yst-nj-lab
  static String friendlyName;  // "Science lab window"
  static String regionHint;    // fed to the ID prompt

  // Sharing mode: "local" | "diag" | "cloud"  (Phase 1: only local acts)
  static String sharingMode;

  // Detection tuning
  static int  pubThreshold;    // >= this: published clean (default 80)
  static int  revThreshold;    // < this: held for review    (default 50)
  static int  cooldownS;       // seconds between capture pipelines
  static int  dailyCap;        // max OpenAI calls per day
  static bool daylightOnly;    // gate detection on the light sensor
  static int  luxThreshold;    // below this lux = night
  static int  motionPct;       // % of grid cells that must change to trigger

  // Daily counters (persisted so a reboot doesn't reset the API budget)
  static int  triggersToday;
  static int  stage1PassToday;
  static int  idsToday;
  static int  publishedToday;
  static int  apiCallsToday;
  static int  counterDay;      // day-of-year the counters belong to

  static void begin() {
    prefs.begin("birdcfg", false);
    deviceId     = prefs.getString("devid", "my-birdcam");
    friendlyName = prefs.getString("name", "BirdWatch Cam");
    regionHint   = prefs.getString("region", "Backyard feeder, northern New Jersey");
    sharingMode  = prefs.getString("mode", "local");
    pubThreshold = prefs.getInt("pubth", 80);
    revThreshold = prefs.getInt("revth", 50);
    cooldownS    = prefs.getInt("cool", 45);
    dailyCap     = prefs.getInt("cap", 60);
    daylightOnly = prefs.getBool("daylight", true);
    luxThreshold = prefs.getInt("luxth", 30);
    motionPct    = prefs.getInt("motion", 8);
    triggersToday   = prefs.getInt("c_trig", 0);
    stage1PassToday = prefs.getInt("c_s1", 0);
    idsToday        = prefs.getInt("c_ids", 0);
    publishedToday  = prefs.getInt("c_pub", 0);
    apiCallsToday   = prefs.getInt("c_api", 0);
    counterDay      = prefs.getInt("c_day", -1);
    Serial.printf("BirdConfig: %s (%s) mode=%s thresholds=%d/%d cap=%d\n",
                  deviceId.c_str(), friendlyName.c_str(), sharingMode.c_str(),
                  pubThreshold, revThreshold, dailyCap);
  }

  static void save() {
    prefs.putString("devid", deviceId);
    prefs.putString("name", friendlyName);
    prefs.putString("region", regionHint);
    prefs.putString("mode", sharingMode);
    prefs.putInt("pubth", pubThreshold);
    prefs.putInt("revth", revThreshold);
    prefs.putInt("cool", cooldownS);
    prefs.putInt("cap", dailyCap);
    prefs.putBool("daylight", daylightOnly);
    prefs.putInt("luxth", luxThreshold);
    prefs.putInt("motion", motionPct);
  }

  static void saveCounters() {
    prefs.putInt("c_trig", triggersToday);
    prefs.putInt("c_s1", stage1PassToday);
    prefs.putInt("c_ids", idsToday);
    prefs.putInt("c_pub", publishedToday);
    prefs.putInt("c_api", apiCallsToday);
    prefs.putInt("c_day", counterDay);
  }

  // Roll counters at midnight. Uses NTP local time; before time sync,
  // day stays -1 and counters just accumulate (safe default).
  static void rollDayIfNeeded() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    if (t.tm_yday != counterDay) {
      counterDay = t.tm_yday;
      triggersToday = stage1PassToday = idsToday = publishedToday = apiCallsToday = 0;
      saveCounters();
      Serial.println("BirdConfig: daily counters reset");
    }
  }

  static bool underApiCap() { return apiCallsToday < dailyCap; }

private:
  static Preferences prefs;
};

Preferences BirdConfig::prefs;
String BirdConfig::deviceId;
String BirdConfig::friendlyName;
String BirdConfig::regionHint;
String BirdConfig::sharingMode;
int  BirdConfig::pubThreshold = 80;
int  BirdConfig::revThreshold = 50;
int  BirdConfig::cooldownS = 45;
int  BirdConfig::dailyCap = 60;
bool BirdConfig::daylightOnly = true;
int  BirdConfig::luxThreshold = 30;
int  BirdConfig::motionPct = 8;
int  BirdConfig::triggersToday = 0;
int  BirdConfig::stage1PassToday = 0;
int  BirdConfig::idsToday = 0;
int  BirdConfig::publishedToday = 0;
int  BirdConfig::apiCallsToday = 0;
int  BirdConfig::counterDay = -1;

#endif
