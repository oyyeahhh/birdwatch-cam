/*
 * motion.h - frame-differencing motion trigger
 *
 * Decodes each JPEG frame at 1/8 scale (SVGA -> 100x75 RGB565 in PSRAM),
 * folds it into a GRID_W x GRID_H mean-luma grid, and compares against the
 * previous frame's grid. A cell "changed" when its luma moved more than
 * CELL_DELTA; the frame triggers when more than motionPct % of cells
 * changed. Cheap (one small decode per check), robust against JPEG noise,
 * and insensitive to gradual light changes because CELL_DELTA is absolute
 * per check, not accumulated.
 */
#ifndef MOTION_H
#define MOTION_H

#include "esp_camera.h"
#include "img_converters.h"

class MotionDetector {
public:
  static const int GRID_W = 20;
  static const int GRID_H = 15;
  static const int CELL_DELTA = 14;   // 0..255 luma change that marks a cell
  // If this large a fraction of ALL cells change at once, it's a scene-wide
  // brightness shift (auto-exposure/gain stepping to track changing
  // ambient light), not real motion - a bird, even a large one filling a
  // good chunk of the frame, doesn't move the WHOLE scene. This matters
  // most through a sunrise ramp: exposure keeps taking new steps for many
  // minutes as light keeps climbing well past the daylight threshold, not
  // just once at the moment the gate opens.
  static const int GLOBAL_SHIFT_PCT = 60;

  static int lastChangedCells;        // for diagnostics

  static bool begin(int decodedW, int decodedH) {
    dw = decodedW; dh = decodedH;
    rgbBuf  = (uint8_t*)ps_malloc(dw * dh * 2);
    prevGrid = (uint8_t*)malloc(GRID_W * GRID_H);
    currGrid = (uint8_t*)malloc(GRID_W * GRID_H);
    havePrev = false;
    if (!rgbBuf || !prevGrid || !currGrid) {
      Serial.println("Motion: buffer alloc failed");
      return false;
    }
    Serial.printf("Motion: %dx%d decode, %dx%d grid\n", dw, dh, GRID_W, GRID_H);
    return true;
  }

  // Returns true when the frame differs enough from the previous one.
  // The first frame after boot (or after reset()) only primes the grid.
  static bool check(camera_fb_t* fb, int motionPct) {
    if (!rgbBuf) return false;
    if (!jpg2rgb565(fb->buf, fb->len, rgbBuf, JPG_SCALE_8X)) {
      Serial.println("Motion: decode failed");
      return false;
    }
    // Mean luma per cell. RGB565 big-endian from the decoder.
    int cellW = dw / GRID_W, cellH = dh / GRID_H;
    for (int gy = 0; gy < GRID_H; gy++) {
      for (int gx = 0; gx < GRID_W; gx++) {
        uint32_t sum = 0;
        for (int y = gy * cellH; y < (gy + 1) * cellH; y++) {
          const uint8_t* row = rgbBuf + (y * dw + gx * cellW) * 2;
          for (int x = 0; x < cellW; x++) {
            uint16_t px = (row[x * 2] << 8) | row[x * 2 + 1];
            uint8_t r = (px >> 11) & 0x1F, g = (px >> 5) & 0x3F, b = px & 0x1F;
            sum += ((r << 3) + (g << 2) + (b << 3)) >> 2;  // fast approx luma
          }
        }
        currGrid[gy * GRID_W + gx] = sum / (cellW * cellH);
      }
    }
    bool triggered = false;
    if (havePrev) {
      int changed = 0;
      for (int i = 0; i < GRID_W * GRID_H; i++) {
        if (abs((int)currGrid[i] - (int)prevGrid[i]) > CELL_DELTA) changed++;
      }
      lastChangedCells = changed;
      int totalCells = GRID_W * GRID_H;
      if (changed >= totalCells * GLOBAL_SHIFT_PCT / 100) {
        // Scene-wide shift, not localized motion - don't trigger. Still
        // fall through to memcpy below so this frame becomes the new
        // baseline; otherwise the next check would compare against an
        // increasingly stale frame and eventually cross the real motion
        // threshold anyway once exposure has drifted far enough.
        triggered = false;
      } else {
        int needed = max(2, totalCells * motionPct / 100);
        triggered = changed >= needed;
      }
    }
    memcpy(prevGrid, currGrid, GRID_W * GRID_H);
    havePrev = true;
    return triggered;
  }

  // Forget the reference frame (call after the pipeline ran, so the bird
  // flying off doesn't immediately re-trigger).
  static void reset() { havePrev = false; }

private:
  static int dw, dh;
  static uint8_t* rgbBuf;
  static uint8_t* prevGrid;
  static uint8_t* currGrid;
  static bool havePrev;
};

int MotionDetector::dw = 0;
int MotionDetector::dh = 0;
int MotionDetector::lastChangedCells = 0;
uint8_t* MotionDetector::rgbBuf = nullptr;
uint8_t* MotionDetector::prevGrid = nullptr;
uint8_t* MotionDetector::currGrid = nullptr;
bool MotionDetector::havePrev = false;

#endif
