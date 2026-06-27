// main.cpp — Autotrim hovedløkke (scheduler) som binder modulene sammen.
//
// Rekkefølge ved oppstart er viktig for sikkerhet:
//   1) Reléer av FØRST (ingen plan-bevegelse under boot)
//   2) Last parametere
//   3) BLE, sensorer
//   4) Kontroll starter i BOOT_UP -> kjører begge plan opp i 3 s (referanse)
#include <Arduino.h>
#include "config.h"
#include "params.h"
#include "types.h"
#include "imu.h"
#include "gps.h"
#include "relays.h"
#include "control.h"
#include "ble_iface.h"

static AutotrimParams params;
static Imu      imu;
static Gps      gps;
static Relays   relays;
static Control  control;
static BleIface ble;

static uint32_t tImu = 0, tCtrl = 0, tTele = 0;

static void handleCommands() {
  if (ble.consumeParamsDirty()) {
    params_save(params);                 // app skrev nye parametere -> persistér
  }
  switch (ble.takeCommand()) {
    case CMD_BOTH_UP:       control.requestBothUp(); break;
    case CMD_RECAL_UP:      control.requestRecal();  break;
    case CMD_SAVE_PARAMS:   params_save(params);     break;
    case CMD_SET_ROLL_ZERO: imu.captureZero();       break;
    default: break;
  }
}

static void publishTelemetry() {
  Telemetry t{};
  t.millisUp     = millis();
  t.rollDeg      = imu.rollDeg();
  t.rollRateDps  = imu.rollRateDps();
  t.accX         = imu.accX();
  t.accY         = imu.accY();
  t.sogKn        = gps.sogKn();
  t.gpsFix       = gps.hasFix() ? 1 : 0;
  t.sats         = gps.sats();
  t.state        = (uint8_t)control.state();
  t.activeSide   = (int8_t)control.activeSide();
  t.posLeftFrac  = control.posLeftFrac(params);
  t.posRightFrac = control.posRightFrac(params);
  t.relayBits    = relays.bits();
  t.faults       = (imu.ok() ? 0 : FAULT_IMU)
                 | (gps.hasFix() ? 0 : FAULT_GPS_FIX)
                 | (imu.sane() ? 0 : FAULT_IMU_SANITY);
  ble.publishTelemetry(t);
}

void setup() {
  Serial.begin(115200);

  relays.begin();        // 1) trygt: alle reléer av
  params_load(params);   // 2) parametere
  ble.begin(&params);    // 3) BLE
  gps.begin();
  bool imuOk = imu.begin();
  control.begin(&relays);  // 4) kontroll (starter i BOOT_UP)

  Serial.printf("Autotrim klar. IMU=%s\n", imuOk ? "OK" : "FEIL");

  uint32_t now = millis();
  tImu = tCtrl = tTele = now;
}

void loop() {
  uint32_t now = millis();

  gps.poll();            // les GPS kontinuerlig
  handleCommands();      // BLE-kommandoer / parameter-skriv

  if (now - tImu >= IMU_PERIOD_MS) {
    float dt = (now - tImu) / 1000.0f;
    tImu = now;
    imu.update(dt, params);
  }

  if (now - tCtrl >= CTRL_PERIOD_MS) {
    float dt = (now - tCtrl) / 1000.0f;
    tCtrl = now;
    bool imuSane = imu.ok() && imu.sane();
    control.update(dt, imu.rollDeg(), imu.rollRateDps(), imuSane,
                   gps.sogKn(), gps.hasFix(), params);
  }

  if (now - tTele >= TELE_PERIOD_MS) {
    tTele = now;
    publishTelemetry();
  }
}
