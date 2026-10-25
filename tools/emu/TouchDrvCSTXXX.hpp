// Touch stub: the SDL layer writes the mouse position/state into these globals
// and the sketch reads them through the same API as the real CST9217.
//
// It mirrors SensorLib's getTouchPoints()/TouchPoints, and deliberately does NOT
// offer the deprecated getPoint(): if the sketch went back to it, the emulator
// build would fail here instead of quietly accepting what the board warns about.
#pragma once
#include "Arduino.h"

extern volatile int g_touchX, g_touchY;
extern volatile bool g_touchDown;

struct TouchPoint {
  uint16_t x = 0, y = 0, pressure = 0;
  uint8_t id = 0, event = 0;
};

class TouchPoints {
public:
  static constexpr uint8_t MAX_POINTS = 5;
  uint8_t getPointCount() const { return count; }
  bool hasPoints() const { return count > 0; }
  const TouchPoint &getPoint(uint8_t index) const { return points[index]; }
  void clear() { count = 0; }
  bool addPoint(uint16_t x, uint16_t y) {
    if (count >= MAX_POINTS) return false;
    points[count].x = x;
    points[count].y = y;
    count++;
    return true;
  }
private:
  TouchPoint points[MAX_POINTS];
  uint8_t count = 0;
};

class TouchDrvCST92xx {
public:
  void setPins(int, int) {}
  bool begin(class FakeWire &, uint8_t, int, int) { return true; }
  void reset() {}
  void setMaxCoordinates(int, int) {}
  void setMirrorXY(bool, bool) {}
  const TouchPoints &getTouchPoints() {
    current.clear();
    if (g_touchDown) current.addPoint((uint16_t)g_touchX, (uint16_t)g_touchY);
    return current;
  }
private:
  TouchPoints current;
};
