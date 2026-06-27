// gps.h — TBS M10N på UART2, TinyGPS++ -> fart over grunn (SOG) + fix
#pragma once
#include <Arduino.h>

class Gps {
public:
  void  begin();
  void  poll();              // les tilgjengelige bytes (kall ofte)
  bool  hasFix()  const { return _fix; }
  float sogKn()   const { return _sogKn; }
  uint8_t sats()  const { return _sats; }
  uint32_t ageMs()const;     // ms siden siste gyldige oppdatering

private:
  bool     _fix = false;
  float    _sogKn = 0.0f;
  uint8_t  _sats = 0;
  uint32_t _lastFixMs = 0;
};
