#include "params.h"
#include <Preferences.h>

static const uint16_t PARAMS_MAGIC = 0xA770;
static const uint8_t  PARAMS_VERSION = 1;

void params_setDefaults(AutotrimParams &p) {
  p.magic           = PARAMS_MAGIC;
  p.version         = PARAMS_VERSION;
  p.autoEnabled     = 0;          // av som default — føreren slår på
  p.speedOnKn       = 17.0f;
  p.speedOffKn      = 14.0f;
  p.rollSetpointDeg = 0.0f;
  p.rollDeadbandDeg = 1.5f;
  p.kP              = 0.06f;       // startverdi, tunes ved sjøprøve
  p.kI              = 0.01f;
  p.fusionAlpha     = 0.98f;
  p.cmdTauSec       = 5.0f;
  p.fullStrokeMs    = 6000.0f;     // måles på faktisk aktuator
  p.maxDeployFrac   = 0.8f;
  p.neutralFrac     = 0.0f;        // 0 = helt oppe; sett litt > 0 for "litt over plan"
  p.rollSign        = -1;          // benk-test 2026-06-24: med +1 ga styrbord-lav NEGATIV roll;
                                   // konvensjon er positiv = styrbord lav -> snudd til -1.
  p.gyroSign        = 1;           // benk-test 2026-06-24: verifisert konsistent med rollSign=-1
                                   // (styrbord-vipp -> roll øker positivt OG rate positiv).
  p.reserved0       = 0;
  p.reserved1       = 0;
}

bool params_valid(const AutotrimParams &p) {
  return p.magic == PARAMS_MAGIC && p.version == PARAMS_VERSION;
}

void params_load(AutotrimParams &p) {
  Preferences prefs;
  prefs.begin("autotrim", true);              // read-only
  size_t n = prefs.getBytesLength("params");
  if (n == sizeof(AutotrimParams)) {
    prefs.getBytes("params", &p, sizeof(p));
  }
  prefs.end();
  if (!params_valid(p)) params_setDefaults(p);
}

void params_save(const AutotrimParams &p) {
  Preferences prefs;
  prefs.begin("autotrim", false);
  prefs.putBytes("params", &p, sizeof(p));
  prefs.end();
}
