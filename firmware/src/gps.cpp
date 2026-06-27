#include "gps.h"
#include "config.h"
#include <TinyGPSPlus.h>

static TinyGPSPlus gps;
#define GPSSerial Serial2

void Gps::begin() {
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

void Gps::poll() {
  while (GPSSerial.available() > 0) {
    gps.encode(GPSSerial.read());
  }
  if (gps.location.isValid() && gps.satellites.isValid()) {
    _sats = (uint8_t)gps.satellites.value();
  }
  if (gps.speed.isValid() && gps.speed.isUpdated()) {
    _sogKn = (float)gps.speed.knots();
    _fix   = gps.location.isValid() && (gps.satellites.value() >= 4);
    _lastFixMs = millis();
  }
  // Mist fix hvis ingen gyldig data på 3 s
  if (millis() - _lastFixMs > 3000) {
    _fix = false;
  }
}

uint32_t Gps::ageMs() const {
  return millis() - _lastFixMs;
}
