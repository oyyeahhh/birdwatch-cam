/*
 * light_sensor.h - LTR-308 ambient light sensor (daylight gate)
 *
 * Minimal register-level driver so we don't depend on the DFRobot
 * library. Registers per the LiteOn LTR-308ALS datasheet:
 *   0x00 MAIN_CTRL   (0x02 = ALS enable)
 *   0x04 ALS_MEAS_RATE (default 18-bit / 100ms)
 *   0x05 ALS_GAIN    (0x01 = gain x3, library default)
 *   0x06 PART_ID     (0xB1)
 *   0x0D..0x0F ALS_DATA (LSB..MSB, 20-bit)
 *
 * Lux conversion mirrors DFRobot_LTR308::getLux(): lux = 0.6 * data / gain
 * with the default gain(3) and 18-bit/100ms integration.
 *
 * The DFR1154 exposes the sensor on the board's default I2C pins
 * (the official demo calls Wire.begin() with no arguments). If the
 * sensor doesn't answer, we degrade gracefully: lux reads -1 and the
 * daylight gate stays open so detection still works.
 */
#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <Wire.h>

class LightSensor {
public:
  static bool present;

  static bool begin() {
    Wire.begin();
    Wire.setClock(400000);
    uint8_t id = readReg(0x06);
    present = (id == 0xB1);
    if (present) {
      writeReg(0x00, 0x02);  // ALS active
      writeReg(0x04, 0x22);  // 18-bit, 100ms (datasheet default)
      writeReg(0x05, 0x01);  // gain x3
      Serial.println("LightSensor: LTR-308 online");
    } else {
      Serial.printf("LightSensor: not found (part id 0x%02X) — daylight gate disabled\n", id);
    }
    return present;
  }

  // Returns lux, or -1 if the sensor is absent/unreadable.
  static float readLux() {
    if (!present) return -1;
    uint8_t b0 = readReg(0x0D), b1 = readReg(0x0E), b2 = readReg(0x0F);
    uint32_t raw = ((uint32_t)(b2 & 0x0F) << 16) | ((uint32_t)b1 << 8) | b0;
    return 0.6f * raw / 3.0f;
  }

  // Daylight gate: true when bright enough to detect (or sensor absent).
  static bool isDaylight(int luxThreshold) {
    float lux = readLux();
    if (lux < 0) return true;
    return lux >= luxThreshold;
  }

private:
  static const uint8_t ADDR = 0x53;

  static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((int)ADDR, 1) != 1) return 0;
    return Wire.read();
  }
  static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }
};

bool LightSensor::present = false;

#endif
