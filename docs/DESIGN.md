# BirdWatch Cam — Design Outline (v3.2)
### Image-based bird identification on the DFRobot DFR1154 ESP32-S3 AI Camera, published to a public webpage

**Inspiration:** AvianVisitors (BirdNET-Pi) — a live collage of birds detected near a window
**Key difference:** AvianVisitors identifies birds by *sound* on a Raspberry Pi. This project identifies birds by *image* on the DFR1154, and publishes results to a webpage reachable from any browser via a URL.

**Decisions locked in:**
1. Camera mounted **indoors, shooting through a window**
2. Backend: **Supabase**
3. Webpage includes a **Diagnostics tab**; detections below **80% confidence** display their confidence percentage
4. Architecture is **multi-camera / multi-site ready** from day one (every record carries a `device_id`)
5. Each camera has a **Configuration tab** with a sharing mode selector — schools can run fully local (opt out of public sharing) while still supporting diagnostics
6. Gallery ships with a **small bundled set of common-species illustrations** used as the species-card image until a real photo of that species clears the quality bar (see Section 9)

**Changes in v3.2:**
- Open question #7 resolved: bundle a small illustration set for common feeder species (decision locked in as #6 above)

**Changes in v3.1** (from repo verification of `ai_repository` and AvianVisitors):
- Starting firmware updated from v3.2.0 to **v3.2.5** (`ai_camera/firmware/standalone/ESP32_AI_Camera_v3_2_5/`), the newest standalone version in this repo
- TLS certificate pinning added as a Phase 2 requirement and a Risks row — the firmware defaults to bypassing certificate validation (`.setInsecure()`), though v3.2.5's `ssl_validation.h` already scaffolds a `setCACert()` path we can enable
- Documented that audio-based ID is **permanently out of scope on this board** (I2S hardware conflict), so the hybrid sound+image idea doesn't resurface
- Gallery display policy made explicit: species cards show the **best-ever** photo per species, with optional stock-illustration fallback (pattern borrowed from AvianVisitors)

---

## 1. Project Goal

Point the DFR1154 through a window at a bird feeder. When a bird lands, the camera captures a photo, identifies the species using AI vision, and displays results — either on the shared public webpage, or only on the school's own local network, depending on that camera's configured sharing mode.

## 2. What We Already Have (from ai_repository)

The v3.2.5 standalone firmware (`ai_camera/firmware/standalone/ESP32_AI_Camera_v3_2_5/`) is the starting point — the newest standalone version in the repo, with `credentials_manager.h`, `error_handling.h`, and `ssl_validation.h` already factored out. It already provides:

- Camera capture on the OV3660 (160° wide-angle)
- Wi-Fi connection with DHCP and a live `/settings` page for credential editing without reflashing — **this is the seed of the Configuration tab**
- Working HTTPS calls to the OpenAI API (vision analysis of captured images)
- A local browser UI served from the board
- MicroSD storage for images
- Ambient light sensor (LTR-308) for daylight awareness

What we add:

1. **A trigger** — frame-differencing motion detection
2. **A bird identification prompt** returning structured JSON with numeric confidence
3. **A local Gallery + Diagnostics UI** on the board itself (works in every mode)
4. **A publish step** — HTTPS POST to Supabase, gated by sharing mode
5. **A Configuration tab** — device identity, sharing mode, thresholds
6. **The public frontend** — for cameras that opt in

**Hardware constraint worth stating once, permanently:** the DFR1154's PDM microphone (GPIO 38/39) and the camera DMA both require `I2S_NUM_0`. Simultaneous audio + camera operation is not supported on this board. That means AvianVisitors-style sound identification can never be added alongside the camera — a hybrid audio+image mode is out of scope on this hardware, not merely deferred.

**Security note on the inherited firmware:** the OpenAI calls default to bypassing TLS certificate validation via `.setInsecure()`. Acceptable for a single hobby device; not acceptable for a fleet of school cameras transmitting API keys. The good news: v3.2.5's `ssl_validation.h` already contains a `setCACert(getRootCACertificates())` code path — we enable it and extend the CA bundle to cover Supabase. See Section 8 and Phase 2.

## 3. Sharing Modes (the Configuration decision)

Every camera runs identical firmware. The Configuration tab selects one of three modes:

| Mode | Bird photos & detections | Diagnostics/heartbeat | Who can see the birds |
|---|---|---|---|
| **Local Only** | Stay on the board (SD + local UI) | Stays local too | Only people on the school's network, via the camera's local IP |
| **Diagnostics Only** | Stay on the board | Sent to Supabase (status/counters only — **no images, no species data**) | Locally only; CIJE can remotely see the camera is healthy |
| **Full Cloud** | Published to Supabase | Sent to Supabase | Everyone, on the public URL |

**Why the middle mode matters:** it separates *privacy* from *supportability*. A school can decline to share any bird imagery publicly, while CIJE can still see on the central Diagnostics tab that the camera is online, its Wi-Fi is strong, and its API budget is fine — which is what makes supporting a fleet of classroom cameras across schools practical without site visits. The heartbeat payload in this mode is deliberately minimal: device_id, uptime, free memory, Wi-Fi RSSI, light level, trigger/API counters, firmware version. Nothing captured by the camera ever leaves the building.

Mode is changeable at any time from the Configuration tab, takes effect immediately, and is stored in flash (survives reboot). Switching from Full Cloud to a more private mode stops all future uploads; previously published detections can be bulk-hidden per device from the Supabase side.

## 4. The Local Web UI (every camera, every mode)

The board's built-in web server (reachable at its local IP, e.g., `http://192.168.0.181`) becomes the complete standalone experience, so Local Only mode is a first-class product rather than a degraded one:

- **Gallery tab:** species cards built from detections stored on the SD card, same 80% badge rule and same best-photo display policy as the public site (Section 9)
- **Timeline tab:** today's visits
- **Diagnostics tab:** uptime, Wi-Fi signal, free memory, light level, today's trigger→Stage-1→Stage-2→published funnel, API usage vs. daily cap, recent errors, and the low-confidence (<50%) review queue
- **Configuration tab:**
  - Device identity: `device_id`, friendly name, location/region hint (feeds the ID prompt)
  - **Sharing mode selector: Local Only / Diagnostics Only / Full Cloud** with plain-language descriptions of exactly what leaves the network in each mode
  - Supabase URL + device API key (only required for the two cloud modes)
  - Wi-Fi credentials and OpenAI key (extending the existing `/settings` page)
  - Tuning: confidence thresholds (80/50 defaults), motion sensitivity, cooldown seconds, daily API call cap, daylight-only on/off
  - Actions: test capture, test Supabase connection, restart

Design note: the ESP32 serves this from flash/SD as static HTML+JS hitting small JSON endpoints on the board (`/api/detections`, `/api/status`, `/api/config`). Keeping the local UI and the public site as two thin frontends over similar JSON shapes means shared design language and less duplicated work.

One practical caveat for schools: the local IP can change with DHCP. The Configuration tab should display the current IP prominently, and the setup guide should recommend a DHCP reservation on the school router (or mDNS, e.g., `http://birdcam-yst.local`, which works on most school networks).

## 5. System Architecture

```
[Camera, any school]  — identical firmware, per-device config
   1. Motion trigger (frame differencing, daylight only)
   2. Capture JPEG → save to SD
   3. OpenAI Vision (2-stage) → {species, confidence_pct}
   4. Show on LOCAL UI always (Gallery/Timeline/Diagnostics/Configuration)
   5. Depending on sharing mode:
        Local Only        → nothing leaves the network
        Diagnostics Only  → heartbeat JSON → Supabase (no images/species)
        Full Cloud        → detections + thumbnails + heartbeat → Supabase
                     |
                     v
[Supabase]
   - devices:    device_id, name, location, sharing_mode, api_key_hash,
                 last_heartbeat, firmware_version
   - detections: id, device_id, timestamp, common_name, scientific_name,
                 confidence_pct, image_url, published, stage1_passed
   - heartbeats: device_id, ts, uptime, rssi, free_heap, lux,
                 triggers_today, api_calls_today, errors
   - Storage bucket for thumbnails (Full Cloud devices only)
   - RLS: public read on published detections; writes need device key
                     |
                     v
[Public Webpage]  (Netlify/Vercel/GitHub Pages)
   - Gallery | Timeline | Stats | Diagnostics
   - Shows only Full Cloud devices' birds
   - Central Diagnostics shows Full Cloud + Diagnostics Only devices
   - URL like: https://cije-birdwatch.netlify.app
```

**Why push-to-cloud instead of exposing each ESP32 publicly?** Each board sits behind school Wi-Fi (NAT), so its web server is only reachable locally. Pushing outward requires zero network configuration per site, aggregates every opted-in camera on one page, and keeps the page up when cameras are offline. Adding a school = flash firmware, set device_id + key + mode in the Configuration tab. No backend or frontend changes. (AvianVisitors solves the same problem by exposing each Raspberry Pi via a Cloudflare Tunnel — workable for one enthusiast's Pi, but it requires per-site network setup, which is exactly what push-to-cloud avoids for a school fleet.)

## 6. Indoor Through-Window Setup

- **Glare and reflections:** lens pressed against the glass with a rubber/foam gasket ring (3D-printed window mount extending the repo's `cad/` files). No interior lights behind the camera; dark backdrop if needed.
- **No IR through glass:** onboard IR illumination stays disabled (it reflects off the window). Detection is daylight-only, scheduled via the LTR-308 lux threshold — which also saves API calls.
- **Clean glass matters** — note in the school setup guide.
- **Feeder placement:** 2–5 feet from the window with a fixed perch; window suction feeders pair well with the 160° lens.

## 7. Species Identification

Two-stage prompt strategy to control cost and false positives:

**Stage 1 — cheap gate** (e.g., gpt-4o-mini): *"Is there a bird clearly visible? JSON only: {\"bird\": true/false}"*. Filters squirrels, branches, reflections for a fraction of a cent.

**Stage 2 — full ID** with numeric confidence:

```json
{
  "bird_present": true,
  "common_name": "Northern Cardinal",
  "scientific_name": "Cardinalis cardinalis",
  "confidence_pct": 92,
  "count": 1,
  "notes": "adult male, at feeder"
}
```

**Confidence handling (80% rule):**
- **≥ 80%:** published normally, no badge
- **50–79%:** published with an amber badge — "House Finch — 68% confident"
- **< 50%:** held out of the gallery (`published = false`); appears only in the Diagnostics review queue (local queue in Local Only mode, central queue for Full Cloud devices)

**Prompt tips:** include the device's region hint from Configuration ("backyard feeder in northern New Jersey, photographed through a window") and instruct the model to lower confidence for blurry/ambiguous shots. Thresholds are tunable per device in the Configuration tab.

**Cost:** ~30 real detections/day per camera ≈ a few dollars/month per camera. Note the OpenAI calls happen in every mode — Local Only governs where *results* go, not how identification works. (A school wanting zero external traffic at all would be a different, on-device-model project — out of scope for v1. And per the Section 2 hardware constraint, an audio-based alternative is not possible on this board either.)

## 8. Supabase Backend

- Tables as in the architecture diagram; `sharing_mode` lives in `devices` as the server-side record of each camera's declared mode
- Server-side enforcement: the detections insert policy rejects rows from any device whose registered mode isn't Full Cloud — so privacy doesn't depend only on firmware behaving
- Heartbeats accepted from Diagnostics Only and Full Cloud devices
- Storage bucket holds ~640px thumbnails from Full Cloud devices; full-res stays on each SD card
- Per-device API keys, revocable individually
- **TLS with real certificate validation:** the inherited firmware's `.setInsecure()` default must not carry over to fleet firmware. v3.2.5's `ssl_validation.h` already scaffolds the secure path (`setCACert(getRootCACertificates())`); make it the default and add Supabase's root CA to the bundle (both Supabase and OpenAI chain to well-known roots; the ESP32 handles this cheaply). Without it, anyone on a school's Wi-Fi could MITM the connection and harvest the device's Supabase key and the OpenAI key.
- Free tier comfortably covers several cameras

## 9. Public Webpage

Static site on Netlify/Vercel/GitHub Pages reading from Supabase. Four tabs:

**Gallery (default):** hero panel with most recent visitor, species cards (visit count, first/last seen), amber badges under 80%. Camera/location filter appears when multiple Full Cloud devices exist.

**Species card display policy:** through-window photos will sometimes be blurry or badly framed, so a card shows the **best-ever photo** of that species (highest confidence, or manually pinned from the review workflow) rather than the most recent capture. When no photo of a species has cleared the quality bar yet, the card falls back to a **bundled illustration** — the pattern AvianVisitors uses for its entire collage (498 bundled illustrations, 249 species; theirs are selected by audio detection). The hero panel still shows the actual latest capture, since "what's at the feeder right now" is the point of that panel.

**Bundled illustration set (decided):** ship ~20–25 illustrations covering the common feeder species for the pilot region (for northern NJ: Northern Cardinal, Blue Jay, American Goldfinch, House Finch, House Sparrow, Mourning Dove, Carolina Chickadee, Black-capped Chickadee, Tufted Titmouse, Downy Woodpecker, Red-bellied Woodpecker, White-breasted Nuthatch, Dark-eyed Junco, American Robin, Carolina Wren, Song Sparrow, White-throated Sparrow, European Starling, Common Grackle, Red-winged Blackbird, House Wren, Northern Mockingbird). Illustrations are static assets in the public frontend and copied to each camera's SD card for the local Gallery, keyed by scientific name so both UIs resolve them identically. A detection of a species outside the bundle simply shows its photo (or a generic bird silhouette until one clears the bar). Licensing note: generate the set ourselves (as AvianVisitors did with the Gemini API) or use public-domain sources, so schools can reuse the assets freely; illustrations should be visually labeled as illustrations so students don't mistake them for the camera's own capture.

**Timeline:** today's visits, per camera or combined.

**Stats:** species count, busiest hour, weekly leaderboard; cross-school comparisons among opted-in sites ("which school has the most cardinals?").

**Diagnostics:** per-camera status cards for every reporting device (Full Cloud *and* Diagnostics Only): online/offline from heartbeat age, Wi-Fi signal, uptime, memory, light level, firmware version; the trigger→publish funnel; API usage vs. caps; central low-confidence review queue (Full Cloud devices); recent errors. Diagnostics Only devices appear here with health data but no bird content — exactly the fleet-support view. Consider putting this tab behind a simple admin login since it's operational rather than public-facing.

## 10. Hardware & Mounting

- Indoor window mount, lens-to-glass gasket; feeder 2–5 feet away with fixed perch
- Extend the repo's `cad/` STL/STEP files into a suction-cup or sill-clamp window mount
- USB-C wall power
- Firmware constraint: no blocking operations in `loop()` — motion/capture/upload as an async state machine with `vTaskDelay(1)` yields (per the repo's known limitations)
- Firmware constraint: PDM mic and camera DMA share `I2S_NUM_0` — the microphone is unusable while the camera runs (see Section 2); audio features should be stripped from the fork rather than left dormant

## 11. Build Phases

**Phase 1 — Local product (1–2 weeks)**
Start from standalone v3.2.5. Add frame-differencing trigger, two-stage ID with numeric confidence, and the four-tab local UI including the Configuration tab (with the mode selector present but only Local Only active). Success = a fully working Local Only camera: birds identified and browsable at the local IP, diagnostics and tuning on-device.

**Phase 2 — Cloud modes (1 week)**
Stand up Supabase (devices/detections/heartbeats + mode enforcement policy), implement heartbeat and detection publishing gated by mode, **enable `ssl_validation.h`'s pinned root CA path as the default and extend the CA bundle to Supabase**, build the public site with Gallery + Diagnostics, deploy to Netlify. Success = flipping one camera to Full Cloud puts its birds on the public URL; flipping to Diagnostics Only keeps birds local while the camera stays visible on central Diagnostics.

**Phase 3 — Polish & scale**
Timeline/Stats tabs, review-queue workflow (including pinning a species' best photo), the bundled illustration set (~20–25 regional feeder species, Section 9), prompt refinement from real misidentifications, admin login for central Diagnostics, mDNS/`.local` naming for school networks, second-site pilot to validate multi-device + opt-out end to end.

## 12. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Window glare / reflections | Lens-to-glass gasket mount, no interior backlight, clean glass |
| False triggers (wind, squirrels, reflections) | Stage-1 gate + cooldown + motion sensitivity tuning (exposed in Configuration) |
| Misidentified species | Region hint, 80% badge rule, <50% review queue |
| School privacy concerns | Local Only / Diagnostics Only modes; server-side mode enforcement; plain-language mode descriptions in Configuration |
| Keys exposed to on-network MITM (inherited `.setInsecure()`) | Pin root CA bundles for Supabase + OpenAI in Phase 2; per-device keys limit blast radius |
| Local IP changes (DHCP) | Show current IP in Configuration; recommend DHCP reservation or mDNS name |
| API cost creep | Two-stage gating, per-device daily cap surfaced in Diagnostics |
| Wi-Fi dropouts | Queue detections on SD, retry; offline visible via heartbeat age |
| Blocking `loop()` starves web UI | Async state machine, `vTaskDelay(1)` yield pattern |
| Blurry/unflattering species photos | Best-ever photo per card, review-queue pinning, bundled illustration fallback |
| Multi-site key management | Per-device revocable API keys |

## 13. Remaining Open Questions

1. Default mode for new school cameras: start in Local Only (privacy-first, school opts *in* to sharing) or Full Cloud (sharing-first, school opts *out*)? Recommendation: **default Local Only** — it makes the opt-in an explicit, documented choice by the school.
2. Should Diagnostics Only heartbeats include the daily species *count* (a number, no names/photos) so central Stats can say "12 detections at YST today"? Or is any detection data too much for opted-out sites?
3. Who reviews low-confidence queues — one central admin for Full Cloud devices, and the school's teacher locally for Local Only devices?
4. `device_id` naming convention (suggest `org-site-window`, e.g., `yst-nj-lab`)
5. Domain: free `*.netlify.app` or `birdwatch.cije.org`?
6. Should the central Diagnostics tab be public or behind an admin login? (Recommendation: login.)

~~7. Illustration fallback~~ — **Decided (v3.2):** bundle a small set of common-species illustrations; see Section 9 for the species list, asset placement, and licensing note.
