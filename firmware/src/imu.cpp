#include "imu.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

static Adafruit_BNO055 bno(55, 0x28, &Wire);

bool Imu::begin() {
  if (PIN_BNO_RST >= 0) {
    pinMode(PIN_BNO_RST, OUTPUT);
    digitalWrite(PIN_BNO_RST, LOW);  delay(5);
    digitalWrite(PIN_BNO_RST, HIGH); delay(30);
  }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // AMG-modus: rå accel + gyro (+ mag), ingen innebygd fusjon -> vi fusjonerer selv.
  if (!bno.begin(OPERATION_MODE_ACCGYRO)) {
    _ok = false;
    return false;
  }
  delay(20);
  bno.setExtCrystalUse(true);
  _ok = true;
  _init = false;
  return true;
}

void Imu::update(float dt, const AutotrimParams &p) {
  if (!_ok || dt <= 0.0f) return;

  imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER); // m/s^2
  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);     // dps

  _aX = a.x(); _aY = a.y(); _aZ = a.z();

  // Sanity: total akselerasjon skal ligge i nærheten av 1 g i ro/marsj.
  float amag = sqrtf(_aX*_aX + _aY*_aY + _aZ*_aZ);
  _sane = (amag > 4.0f && amag < 25.0f) && !isnan(amag);

  // Rollvinkel fra accel (X opp, Y tvers). atan2(aY, aX).
  float rollAcc = atan2f(_aY, _aX) * 57.2957795f * (float)p.rollSign;

  // Rollrate fra gyro om Z.
  _rate = g.z() * (float)p.gyroSign;

  if (!_init) {            // initier filteret fra accel ved første gyldige sample
    _roll = rollAcc;
    _init = true;
    return;
  }

  // Komplementærfilter: gyro kortsiktig (demper heave), accel korrigerer drift sakte.
  float a_alpha = constrain(p.fusionAlpha, 0.5f, 0.9999f);
  _roll = a_alpha * (_roll + _rate * dt) + (1.0f - a_alpha) * rollAcc;
}

void Imu::captureZero() {
  _offset = _roll;
}
