// imu.h — BNO055: rå accel+gyro -> rollvinkel via komplementærfilter
// Montering: X opp, Y på tvers, Z langskips (kort står loddrett).
// roll_accel = atan2(aY, aX);  rollrate = gyro Z.  Heave dempes av filteret.
#pragma once
#include <Arduino.h>
#include "params.h"

class Imu {
public:
  bool  begin();
  void  update(float dt, const AutotrimParams &p);  // dt i sekunder
  void  captureZero();                              // sett nåværende roll som 0 (i ro, vater)

  bool  ok()         const { return _ok; }
  bool  sane()       const { return _sane; }
  float rollDeg()    const { return _roll - _offset; }
  float rollRateDps()const { return _rate; }
  float accX()       const { return _aX; }
  float accY()       const { return _aY; }

private:
  bool  _ok   = false;
  bool  _sane = true;
  bool  _init = false;   // første sample initialiserer filteret fra accel
  float _roll = 0.0f;    // intern filtertilstand (deg)
  float _rate = 0.0f;    // dps
  float _aX = 0, _aY = 0, _aZ = 0;
  float _offset = 0.0f;
};
