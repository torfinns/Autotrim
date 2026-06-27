#include "relays.h"
#include "config.h"

void Relays::write(int pin, bool on) {
  bool level = RELAY_ACTIVE_HIGH ? on : !on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void Relays::begin() {
  // Sett av-nivå FØR pinModen settes til utgang -> ingen puls ved init.
  int pins[4] = {PIN_REL_LU, PIN_REL_LD, PIN_REL_RU, PIN_REL_RD};
  for (int p : pins) {
    write(p, false);
    pinMode(p, OUTPUT);
    write(p, false);
  }
  _bits = 0;
}

void Relays::apply(bool lu, bool ld, bool ru, bool rd) {
  // Interlock: motstridende kommando på samme side -> begge av på den siden.
  if (lu && ld) { lu = false; ld = false; }
  if (ru && rd) { ru = false; rd = false; }

  write(PIN_REL_LU, lu);
  write(PIN_REL_LD, ld);
  write(PIN_REL_RU, ru);
  write(PIN_REL_RD, rd);

  _bits = (lu ? 0x1 : 0) | (ld ? 0x2 : 0) | (ru ? 0x4 : 0) | (rd ? 0x8 : 0);
}
