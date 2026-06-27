// relays.h — 4 reléer mot Lenco, med interlock og sikker av-tilstand
#pragma once
#include <Arduino.h>

class Relays {
public:
  void begin();                                   // alle av FØRST (trygt ved boot)
  void apply(bool lu, bool ld, bool ru, bool rd); // interlock: aldri U+D samme side
  void allOff() { apply(false,false,false,false); }

  uint8_t bits() const { return _bits; }          // bit0 LU,1 LD,2 RU,3 RD
  bool lu() const { return _bits & 0x1; }
  bool ld() const { return _bits & 0x2; }
  bool ru() const { return _bits & 0x4; }
  bool rd() const { return _bits & 0x8; }

private:
  void write(int pin, bool on);
  uint8_t _bits = 0;
};
