// params.h — justerbare parametere (lagres i NVS, eksponeres over BLE)
#pragma once
#include <Arduino.h>

// Pakket struct => kan sendes rått over BLE. App må bruke samme layout (se README).
#pragma pack(push, 1)
struct AutotrimParams {
  uint16_t magic;        // 0xA770 = gyldig
  uint8_t  version;      // layout-versjon
  uint8_t  autoEnabled;  // 0/1 — auto på/av

  float    speedOnKn;    // aktiver auto over denne farten (kn)   default 17
  float    speedOffKn;   // deaktiver under denne (kn)            default 14

  float    rollSetpointDeg;  // ønsket rollvinkel (0 = vater)
  float    rollDeadbandDeg;  // dødbånd rundt settpunkt

  float    kP;           // P-ledd (deg -> deploy-fraksjon)
  float    kI;           // I-ledd
  float    fusionAlpha;  // komplementærfilter (0.90–0.999), default 0.98
  float    cmdTauSec;    // lavpass på pådrag, default 5.0 s

  float    fullStrokeMs; // aktuator full slaglengde opp->ned (ms)
  float    maxDeployFrac;// maks deploy 0..1
  float    neutralFrac;  // "nøytral" deploy-fraksjon (litt over plan)

  int8_t   rollSign;     // +1/-1: fortegn rollvinkel (kalibreres ved sjøprøve)
  int8_t   gyroSign;     // +1/-1: fortegn gyro-Z mot roll
  uint8_t  reserved0;
  uint8_t  reserved1;
};
#pragma pack(pop)

void   params_setDefaults(AutotrimParams &p);
void   params_load(AutotrimParams &p);   // leser NVS, faller til default
void   params_save(const AutotrimParams &p);
bool   params_valid(const AutotrimParams &p);
