#include "control.h"
#include "config.h"

static const float POS_DEADBAND_MS = 120.0f;   // posisjons-dødbånd (unngå klapring)

void Control::begin(Relays *relays) {
  _r = relays;
  setState(State::BOOT_UP);
}

void Control::setState(State s) {
  _state = s;
  _stateTimeMs = 0;
  if (s == State::ACTIVE) { _integ = 0; _integSide = Side::NONE; _filtTarget = 0; }
}

// Sentinel-verdier; update() klemmer mot full slaglengde (BOTH_UP) eller +30% (RECAL).
void Control::requestBothUp() { _recalMs = 1e9f; }
void Control::requestRecal()  { _recalMs = 2e9f; }

// Estimerer planposisjon ut fra hvilke reléer som er på i dette intervallet.
void Control::integratePosition(float dt, bool lu, bool ld, bool ru, bool rd, const AutotrimParams &p) {
  float dms = dt * 1000.0f;
  if (ld) _posLeftMs  += dms;  if (lu) _posLeftMs  -= dms;
  if (rd) _posRightMs += dms;  if (ru) _posRightMs -= dms;
  _posLeftMs  = constrain(_posLeftMs,  0.0f, p.fullStrokeMs);
  _posRightMs = constrain(_posRightMs, 0.0f, p.fullStrokeMs);
}

void Control::update(float dt, float rollDeg, float rollRateDps, bool imuSane,
                     float sogKn, bool gpsFix, const AutotrimParams &p) {
  if (!_r || dt <= 0) return;
  _stateTimeMs += dt * 1000.0f;

  bool lu=false, ld=false, ru=false, rd=false;

  // --- Tvungen oppkjøring / re-kalibrering (kommando eller oppstart) ---
  if (_recalMs > 0) {
    float cap = (_recalMs > 1.5e9f) ? p.fullStrokeMs * 1.3f   // RECAL: ekstra margin
                                    : p.fullStrokeMs;          // BOTH_UP: full slaglengde
    if (_recalMs > cap) _recalMs = cap;
    lu = true; ru = true;                  // begge opp
    _recalMs -= dt * 1000.0f;
    if (_recalMs <= 0) { _posLeftMs = 0; _posRightMs = 0; _recalMs = 0; }
    _active = Side::NONE;
    integratePosition(dt, lu, ld, ru, rd, p);
    _r->apply(lu, ld, ru, rd);
    return;
  }

  switch (_state) {

    case State::BOOT_UP: {
      lu = true; ru = true;                 // kjør begge opp
      if (_stateTimeMs >= STARTUP_UP_MS) {
        _posLeftMs = 0; _posRightMs = 0;    // referanse: helt oppe
        setState(State::STANDBY);
      }
      break;
    }

    case State::FAULT:
    case State::STANDBY: {
      _active = Side::NONE;
      // "Ved tvil -> opp": retrahér til helt oppe (eller nøytral), ellers av.
      float neutMs = constrain(p.neutralFrac, 0.0f, 1.0f) * p.fullStrokeMs;
      if (_posLeftMs  > neutMs + POS_DEADBAND_MS) lu = true;
      if (_posRightMs > neutMs + POS_DEADBAND_MS) ru = true;

      bool faulted = !imuSane; // hardware/IMU-sanity
      bool wantActive = p.autoEnabled && imuSane && gpsFix;

      // fartslås med hysterese
      if (sogKn >= p.speedOnKn)  _autoLatched = true;
      if (sogKn <  p.speedOffKn) _autoLatched = false;

      if (_state == State::STANDBY && wantActive && _autoLatched) setState(State::ACTIVE);
      if (faulted) setState(State::FAULT);
      if (_state == State::FAULT && imuSane) setState(State::STANDBY);
      break;
    }

    case State::ACTIVE: {
      // Forlat ACTIVE hvis vilkår faller bort -> begge opp via STANDBY-logikken
      if (sogKn < p.speedOffKn) _autoLatched = false;
      bool keep = p.autoEnabled && imuSane && gpsFix && _autoLatched;
      if (!keep) { requestBothUp(); setState(State::STANDBY); break; }

      // e > 0 => styrbord lav (roll positiv).  Bekreftet: høyre plan retter styrbord-slagside,
      // venstre plan retter babord-slagside (venstre ned tipper mot styrbord).
      float e = rollDeg - p.rollSetpointDeg;
      Side desired = Side::NONE;
      float mag = 0;
      if (e >  p.rollDeadbandDeg) { desired = Side::RIGHT; mag = e - p.rollDeadbandDeg; }
      else if (e < -p.rollDeadbandDeg) { desired = Side::LEFT; mag = (-e) - p.rollDeadbandDeg; }

      // PI på magnitude; nullstill integrator ved sidebytte / dødbånd
      if (desired != _integSide) { _integ = 0; _integSide = desired; }
      float targetFrac = 0;
      if (desired != Side::NONE) {
        _integ += mag * dt;
        // anti-windup: klem I-bidraget til maks deploy
        float iClampHi = (p.kI > 1e-6f) ? (p.maxDeployFrac / p.kI) : 0.0f;
        _integ = constrain(_integ, 0.0f, iClampHi);
        targetFrac = p.kP * mag + p.kI * _integ;
        targetFrac = constrain(targetFrac, 0.0f, p.maxDeployFrac);
      }

      // ~5 s lavpass på pådraget (forhindrer at planet jager bølger)
      float tau = max(0.1f, p.cmdTauSec);
      float a = dt / (tau + dt);
      _filtTarget = _filtTarget + a * (targetFrac - _filtTarget);
      _active = desired;

      // Mål-ms per side: aktiv side = filtrert pådrag, inaktiv side = helt opp (0)
      float tgtLeftMs  = 0, tgtRightMs = 0;
      if (_active == Side::LEFT)  tgtLeftMs  = _filtTarget * p.fullStrokeMs;
      if (_active == Side::RIGHT) tgtRightMs = _filtTarget * p.fullStrokeMs;

      // Posisjonsregulator (bang-bang m/ dødbånd) på hver side
      if (_posLeftMs  < tgtLeftMs  - POS_DEADBAND_MS) ld = true;
      else if (_posLeftMs  > tgtLeftMs  + POS_DEADBAND_MS) lu = true;
      if (_posRightMs < tgtRightMs - POS_DEADBAND_MS) rd = true;
      else if (_posRightMs > tgtRightMs + POS_DEADBAND_MS) ru = true;
      break;
    }
  }

  integratePosition(dt, lu, ld, ru, rd, p);
  _r->apply(lu, ld, ru, rd);
}
