// control.h — state machine, fartslås, posisjonsestimering og regulator
#pragma once
#include <Arduino.h>
#include "params.h"
#include "types.h"
#include "relays.h"

class Control {
public:
  void begin(Relays *relays);

  // Kalles hver kontrolltick. dt i sekunder. Setter relé-utganger.
  void update(float dt, float rollDeg, float rollRateDps, bool imuSane,
              float sogKn, bool gpsFix, const AutotrimParams &p);

  // Kommandoer fra BLE/app
  void requestBothUp();      // kjør begge helt opp
  void requestRecal();       // re-referer (kjør opp > full slaglengde, nullstill)

  // Status til telemetri
  State state()        const { return _state; }
  Side  activeSide()   const { return _active; }
  float posLeftFrac(const AutotrimParams &p)  const { return clampFrac(_posLeftMs  / p.fullStrokeMs); }
  float posRightFrac(const AutotrimParams &p) const { return clampFrac(_posRightMs / p.fullStrokeMs); }

private:
  static float clampFrac(float f){ return f < 0 ? 0 : (f > 1 ? 1 : f); }
  void  setState(State s);
  void  integratePosition(float dt, bool lu, bool ld, bool ru, bool rd, const AutotrimParams &p);

  Relays *_r = nullptr;
  State   _state = State::BOOT_UP;
  Side    _active = Side::NONE;

  float   _stateTimeMs = 0;   // tid i nåværende tilstand
  float   _posLeftMs  = 0;    // estimert deploy (ms ned fra helt oppe)
  float   _posRightMs = 0;

  float   _integ = 0;         // PI-integrator (på aktiv magnitude)
  Side    _integSide = Side::NONE;
  float   _filtTarget = 0;    // lavpassfiltrert deploy-fraksjon (~5 s)

  bool    _autoLatched = false; // fartslås-hysterese
  float   _recalMs = 0;         // gjenstående re-kalibrerings/oppkjøringstid
};
