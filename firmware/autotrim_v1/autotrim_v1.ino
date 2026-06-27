// ============================================================================
//  AUTOTRIM v1  —  komplett fastvare i én Arduino-sketch
// ----------------------------------------------------------------------------
//  DIY automatisk trimplan-styring. ESP32 retter opp sideveis slagside (roll)
//  ved å styre et Lenco trimplansystem, parallellkoblet de manuelle bryterne.
//  Roll: BNO055 (I2C). Fart: TBS M10N GPS (UART2). 4 reed-reléer mot Lenco.
//  Konfig over BLE (NimBLE 2.x).
//
//  Dette er firmware/src/* (config, types, params, imu, gps, relays, control,
//  ble_iface, main) slått sammen til én fil for enkel bygging i Arduino IDE.
//
//  SIKKERHET:
//   - Reléer AV som aller første handling ved boot.
//   - Boot: begge plan opp i 3 s -> kjent nøytral (helt oppe) uten sensorikk.
//   - Interlock i fastvare: aldri U+D på samme side samtidig.
//   - Watchdog/feil -> begge opp -> STANDBY. Manuell betjening alltid uberørt.
//
//  KREVER BIBLIOTEKER (Tools -> Manage Libraries):
//   - "NimBLE-Arduino" 2.x
//   - "Adafruit BNO055" (+ "Adafruit Unified Sensor" + "Adafruit BusIO")
//   (GPS parses internt — ingen TinyGPSPlus nødvendig)
//
//  Board = "ESP32 Dev Module". Verifisert på benk 2026-06-24:
//   GPS 38400 baud · rollSign = -1 · gyroSign = +1.
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <NimBLEDevice.h>
#include <nvs_flash.h>
#include <esp_system.h>

// Forward-deklarasjon: trygt mot Arduino sin auto-prototyp-generering
// (params-funksjonene tar AutotrimParams& før structen evt. er sett).
struct AutotrimParams;

// ============================================================================
//  CONFIG  (fra config.h)
// ============================================================================
// I2C (BNO055)
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const int PIN_BNO_RST = 4;     // valgfri; sett -1 om ikke koblet

// UART2 (TBS M10N GPS). GPS TX -> ESP RX, GPS RX -> ESP TX
static const int PIN_GPS_RX  = 16;    // ESP RX2 <- GPS TX
static const int PIN_GPS_TX  = 17;    // ESP TX2 -> GPS RX
static const uint32_t GPS_BAUD = 38400;   // verifisert på modulen (ikke 115200)

// Reléer (fortløpende på venstre header, RTC, ingen boot-glitch/strapping)
static const int PIN_REL_LU = 33;     // venstre opp
static const int PIN_REL_LD = 25;     // venstre ned
static const int PIN_REL_RU = 26;     // høyre opp
static const int PIN_REL_RD = 27;     // høyre ned

// Relémodulens trigger-polaritet. true = aktiv-høy (IN=HIGH -> relé på).
static const bool RELAY_ACTIVE_HIGH = true;

// Løkke-rater
static const uint32_t IMU_PERIOD_MS  = 10;    // 100 Hz sensorfusjon
static const uint32_t CTRL_PERIOD_MS = 50;    // 20 Hz kontroll/relé
static const uint32_t TELE_PERIOD_MS = 200;   // 5 Hz BLE-telemetri

// Oppstartsrutine
static const uint32_t STARTUP_UP_MS  = 3000;  // kjør begge opp ved boot

// BLE UUID-er (egendefinerte 128-bit; app må bruke de samme)
#define BLE_DEVICE_NAME      "Autotrim"
#define UUID_SVC             "9a8b0001-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_PARAMS     "9a8b0002-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_TELEMETRY  "9a8b0003-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_COMMAND    "9a8b0004-7b2c-4f3d-9e6a-2b1c3d4e5f60"

// ============================================================================
//  TYPES  (fra types.h)
// ============================================================================
enum class State : uint8_t { BOOT_UP = 0, STANDBY = 1, ACTIVE = 2, FAULT = 3 };
enum class Side  : int8_t  { NONE = 0, LEFT = 1, RIGHT = 2 };

enum FaultBits : uint16_t {
  FAULT_IMU        = 1 << 0,
  FAULT_GPS_FIX    = 1 << 1,
  FAULT_IMU_SANITY = 1 << 2,
};

#pragma pack(push, 1)
struct Telemetry {
  uint32_t millisUp;
  float    rollDeg;
  float    rollRateDps;
  float    accX;
  float    accY;
  float    sogKn;
  uint8_t  gpsFix;
  uint8_t  sats;
  uint8_t  state;
  int8_t   activeSide;
  float    posLeftFrac;
  float    posRightFrac;
  uint8_t  relayBits;
  uint16_t faults;
};
#pragma pack(pop)

enum Command : uint8_t {
  CMD_NONE          = 0,
  CMD_BOTH_UP       = 1,
  CMD_RECAL_UP      = 2,
  CMD_SAVE_PARAMS   = 3,
  CMD_SET_ROLL_ZERO = 4,
  // Manuell relé-test (virker KUN når testBypass != 0)
  CMD_TEST_LU       = 10,
  CMD_TEST_LD       = 11,
  CMD_TEST_RU       = 12,
  CMD_TEST_RD       = 13,
  CMD_TEST_OFF      = 14,
};

// ============================================================================
//  PARAMS  (fra params.h/.cpp)
// ============================================================================
#pragma pack(push, 1)
struct AutotrimParams {
  uint16_t magic;        // 0xA770 = gyldig
  uint8_t  version;
  uint8_t  autoEnabled;
  float    speedOnKn;
  float    speedOffKn;
  float    rollSetpointDeg;
  float    rollDeadbandDeg;
  float    kP;
  float    kI;
  float    fusionAlpha;
  float    cmdTauSec;
  float    fullStrokeMs;
  float    maxDeployFrac;
  float    neutralFrac;
  int8_t   rollSign;
  int8_t   gyroSign;
  uint8_t  testBypass;   // 0=normal, 1=overstyr fart+GPS-fix (kun benk-test; nullstilles ved boot)
  uint8_t  reserved1;
  float    mountingOffsetDeg;   // monteringskorreksjon: legg til målt roll (grader)
};
#pragma pack(pop)

static const uint16_t PARAMS_MAGIC   = 0xA770;
static const uint8_t  PARAMS_VERSION = 2;

void params_setDefaults(AutotrimParams &p) {
  p.magic           = PARAMS_MAGIC;
  p.version         = PARAMS_VERSION;
  p.autoEnabled     = 0;            // av som default — føreren slår på
  p.speedOnKn       = 17.0f;
  p.speedOffKn      = 14.0f;
  p.rollSetpointDeg = 0.0f;
  p.rollDeadbandDeg = 1.5f;
  p.kP              = 0.06f;
  p.kI              = 0.01f;
  p.fusionAlpha     = 0.98f;
  p.cmdTauSec       = 5.0f;
  p.fullStrokeMs    = 6000.0f;
  p.maxDeployFrac   = 0.8f;
  p.neutralFrac     = 0.0f;
  p.rollSign        = -1;           // benk-test 2026-06-24: styrbord-lav -> positiv roll
  p.gyroSign        = 1;            // verifisert konsistent med rollSign=-1
  p.testBypass         = 0;            // testmodus av
  p.reserved1          = 0;
  p.mountingOffsetDeg  = 0.0f;
}

bool params_valid(const AutotrimParams &p) {
  return p.magic == PARAMS_MAGIC && p.version == PARAMS_VERSION;
}

void params_load(AutotrimParams &p) {
  Preferences prefs;
  prefs.begin("autotrim", true);
  size_t n = prefs.getBytesLength("params");
  if (n == sizeof(AutotrimParams)) prefs.getBytes("params", &p, sizeof(p));
  prefs.end();
  if (!params_valid(p)) params_setDefaults(p);
}

void params_save(const AutotrimParams &p) {
  Preferences prefs;
  prefs.begin("autotrim", false);
  prefs.putBytes("params", &p, sizeof(p));
  prefs.end();
}

// ============================================================================
//  IMU  (fra imuDev.h/.cpp) — BNO055 ACCGYRO -> roll via komplementærfilter
// ============================================================================
static Adafruit_BNO055 g_bno(55, 0x28, &Wire);

class Imu {
public:
  bool begin() {
    if (PIN_BNO_RST >= 0) {
      pinMode(PIN_BNO_RST, OUTPUT);
      digitalWrite(PIN_BNO_RST, LOW);  delay(5);
      digitalWrite(PIN_BNO_RST, HIGH); delay(30);
    }
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    if (!g_bno.begin(OPERATION_MODE_ACCGYRO)) { _ok = false; return false; }
    delay(20);
    g_bno.setExtCrystalUse(true);
    _ok = true; _init = false;
    return true;
  }

  void update(float dt, const AutotrimParams &p) {
    if (!_ok || dt <= 0.0f) return;
    imu::Vector<3> a = g_bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    imu::Vector<3> g = g_bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    _aX = a.x(); _aY = a.y(); _aZ = a.z();
    float amag = sqrtf(_aX*_aX + _aY*_aY + _aZ*_aZ);
    _sane = (amag > 4.0f && amag < 25.0f) && !isnan(amag);

    float rollAcc = atan2f(_aY, _aX) * 57.2957795f * (float)p.rollSign;
    _rate = g.z() * (float)p.rollSign;

    if (!_init) { _roll = rollAcc; _init = true; return; }
    float a_alpha = constrain(p.fusionAlpha, 0.5f, 0.9999f);
    _roll = a_alpha * (_roll + _rate * dt) + (1.0f - a_alpha) * rollAcc;
  }

  void  captureZero() { _offset = _roll; }
  bool  ok()          const { return _ok; }
  bool  sane()        const { return _sane; }
  float rollDeg()     const { return _roll - _offset; }
  float rollRateDps() const { return _rate; }
  float accX()        const { return _aX; }
  float accY()        const { return _aY; }

private:
  bool  _ok = false, _sane = true, _init = false;
  float _roll = 0, _rate = 0, _aX = 0, _aY = 0, _aZ = 0, _offset = 0;
};

// ============================================================================
//  GPS  — TBS M10N på UART2. Intern NMEA-parser (ingen ekstern lib).
//  GGA felt 7 = antall satellitter, RMC felt 2 = status (A/V), felt 7 = SOG (kn).
// ============================================================================
#define GPSSerial Serial2

class Gps {
public:
  void begin() { GPSSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX); }

  void poll() {
    while (GPSSerial.available() > 0) {
      char c = (char)GPSSerial.read();
      if (c == '\n' || c == '\r') {
        if (_len > 0) { _line[_len] = '\0'; parse(_line); _len = 0; }
      } else if (_len < sizeof(_line) - 1) {
        _line[_len++] = c;
      }
    }
    if (millis() - _lastFixMs > 3000) _fix = false;     // mist fix ved 3 s stille
  }

  bool     hasFix() const { return _fix; }
  float    sogKn()  const { return _sogKn; }
  uint8_t  sats()   const { return _sats; }
  uint32_t ageMs()  const { return millis() - _lastFixMs; }

private:
  static String field(const char* s, int idx) {
    int f = 0; String out = "";
    for (const char* p = s; *p && *p != '*'; ++p) {
      if (*p == ',') { f++; continue; }
      if (f == idx) out += *p; if (f > idx) break;
    }
    return out;
  }
  void parse(const char* s) {
    if (strlen(s) < 6 || s[0] != '$') return;
    const char* t = s + 3;                              // hopp over "$Gx"
    if (!strncmp(t, "GGA", 3)) {
      String n = field(s, 7);
      if (n.length()) _sats = (uint8_t)n.toInt();
    } else if (!strncmp(t, "RMC", 3)) {
      String st = field(s, 2), sp = field(s, 7);
      bool valid = (st.length() && st[0] == 'A');
      _fix = valid && (_sats >= 4);
      _sogKn = (valid && sp.length()) ? sp.toFloat() : 0.0f;   // 0 uten gyldig fix
      if (valid) _lastFixMs = millis();
    }
  }

  char     _line[120]; uint8_t _len = 0;
  bool     _fix = false;
  float    _sogKn = 0.0f;
  uint8_t  _sats = 0;
  uint32_t _lastFixMs = 0;
};

// ============================================================================
//  RELAYS  (fra relays.h/.cpp) — interlock + sikker av-tilstand
// ============================================================================
class Relays {
public:
  void begin() {
    int pins[4] = {PIN_REL_LU, PIN_REL_LD, PIN_REL_RU, PIN_REL_RD};
    for (int p : pins) { write(p, false); pinMode(p, OUTPUT); write(p, false); }
    _bits = 0;
  }

  void apply(bool lu, bool ld, bool ru, bool rd) {
    if (lu && ld) { lu = false; ld = false; }     // interlock samme side
    if (ru && rd) { ru = false; rd = false; }
    write(PIN_REL_LU, lu); write(PIN_REL_LD, ld);
    write(PIN_REL_RU, ru); write(PIN_REL_RD, rd);
    _bits = (lu ? 0x1 : 0) | (ld ? 0x2 : 0) | (ru ? 0x4 : 0) | (rd ? 0x8 : 0);
  }
  void allOff() { apply(false, false, false, false); }
  uint8_t bits() const { return _bits; }

private:
  void write(int pin, bool on) {
    bool level = RELAY_ACTIVE_HIGH ? on : !on;
    digitalWrite(pin, level ? HIGH : LOW);
  }
  uint8_t _bits = 0;
};

// ============================================================================
//  CONTROL  (fra control.h/.cpp) — state machine, fartslås, regulator
// ============================================================================
static const float POS_DEADBAND_MS      = 120.0f;
static const float SETTLE_AFTER_MOVE_MS = 1500.0f;  // vent etter pådrag — båt/plan trenger tid
static const float CTRL_STEP_DT         = 0.50f;    // effektivt integrasjons-dt per beslutning

class Control {
public:
  void begin(Relays *relays) { _r = relays; setState(State::BOOT_UP); }

  void requestBothUp() { _recalMs = 1e9f; }
  void requestRecal()  { _recalMs = 2e9f; }
  void requestTestPulse(int ch) { _testCh = ch; _testMs = 1500.0f; }  // 0=LU,1=LD,2=RU,3=RD
  void requestTestOff()         { _testCh = -1; _testMs = 0; }

  void update(float dt, float rollDeg, float rollRateDps, bool imuSane,
              float sogKn, bool gpsFix, const AutotrimParams &p) {
    if (!_r || dt <= 0) return;
    _stateTimeMs += dt * 1000.0f;
    bool lu=false, ld=false, ru=false, rd=false;

    // Manuell relé-test (testmodus): driv én kanal, tidsbegrenset, hopp over normal logikk.
    if (_testCh >= 0) {
      _testMs -= dt * 1000.0f;
      bool t0=(_testCh==0), t1=(_testCh==1), t2=(_testCh==2), t3=(_testCh==3);
      if (_testMs <= 0) { _testCh = -1; t0=t1=t2=t3=false; }
      integratePosition(dt, t0, t1, t2, t3, p);
      _r->apply(t0, t1, t2, t3);
      return;
    }

    if (_recalMs > 0) {
      float cap = (_recalMs > 1.5e9f) ? p.fullStrokeMs * 1.3f : p.fullStrokeMs;
      if (_recalMs > cap) _recalMs = cap;
      lu = true; ru = true;
      _recalMs -= dt * 1000.0f;
      if (_recalMs <= 0) { _posLeftMs = 0; _posRightMs = 0; _recalMs = 0; }
      _active = Side::NONE;
      integratePosition(dt, lu, ld, ru, rd, p);
      _r->apply(lu, ld, ru, rd);
      return;
    }

    switch (_state) {
      case State::BOOT_UP: {
        lu = true; ru = true;
        // Kjør lenge nok til at planene garantert er helt oppe, uansett startposisjon.
        float bootUpMs = max((float)STARTUP_UP_MS, p.fullStrokeMs * 1.2f);
        if (_stateTimeMs >= bootUpMs) { _posLeftMs = 0; _posRightMs = 0; setState(State::STANDBY); }
        break;
      }
      case State::FAULT:
      case State::STANDBY: {
        _active = Side::NONE;
        float neutMs = constrain(p.neutralFrac, 0.0f, 1.0f) * p.fullStrokeMs;
        if (_posLeftMs  > neutMs + POS_DEADBAND_MS) lu = true;
        if (_posRightMs > neutMs + POS_DEADBAND_MS) ru = true;
        bool bypass = (p.testBypass != 0);
        bool faulted = !imuSane;
        bool wantActive = p.autoEnabled && imuSane && (gpsFix || bypass);
        if (sogKn >= p.speedOnKn)  _autoLatched = true;
        if (sogKn <  p.speedOffKn) _autoLatched = false;
        if (bypass) _autoLatched = true;     // testmodus: ignorer fartslås
        if (_state == State::STANDBY && wantActive && _autoLatched) setState(State::ACTIVE);
        if (faulted) setState(State::FAULT);
        if (_state == State::FAULT && imuSane) setState(State::STANDBY);
        break;
      }
      case State::ACTIVE: {
        bool bypass = (p.testBypass != 0);
        if (sogKn < p.speedOffKn && !bypass) _autoLatched = false;
        bool keep = p.autoEnabled && imuSane && (gpsFix || bypass) && (_autoLatched || bypass);
        if (!keep) { requestBothUp(); setState(State::STANDBY); break; }

        _settleMs -= dt * 1000.0f;
        if (_settleMs < 0) _settleMs = 0;

        // Vent til alle hold-timere er ferdige OG settle-tid er utløpt.
        // Garanterer at plan slutter å bevege seg og båten stabiliserer seg
        // før neste beslutning. Ingen jaging / ping-pong mulig.
        bool holdActive = (_holdLUms > 0 || _holdLDms > 0 || _holdRUms > 0 || _holdRDms > 0);
        if (_settleMs > 0 || holdActive) break;

        float e = rollDeg - p.rollSetpointDeg;   // e>0 => styrbord lav
        float eMag = 0;
        if      (e >  p.rollDeadbandDeg) eMag = -(e - p.rollDeadbandDeg);  // styrbord lav → mer høyre
        else if (e < -p.rollDeadbandDeg) eMag = -(e + p.rollDeadbandDeg);  // babord lav  → mer venstre

        // Innenfor dødbånd: plan holder fysisk posisjon — ingen regulering.
        if (eMag == 0) break;

        // Anti-windup: ved retningsskifte starter vi fra nøytral — ingen "motbakke".
        if (eMag * _trimFrac < -1e-6f) _trimFrac = 0;

        // Diskret integrasjon: én beslutning per SETTLE-syklus.
        // Inne i dødbånd: eMag=0 → ingen endring → plan holder posisjon.
        _trimFrac += p.kP * eMag * CTRL_STEP_DT;
        _trimFrac  = constrain(_trimFrac, -p.maxDeployFrac, p.maxDeployFrac);

        float tgtLeftMs  = max(0.0f,  _trimFrac) * p.fullStrokeMs;
        float tgtRightMs = max(0.0f, -_trimFrac) * p.fullStrokeMs;
        _active = (_trimFrac >  0.02f) ? Side::LEFT
                : (_trimFrac < -0.02f) ? Side::RIGHT : Side::NONE;

        bool needMove = false;
        if (_posLeftMs  < tgtLeftMs  - POS_DEADBAND_MS) { ld = true; needMove = true; }
        else if (_posLeftMs  > tgtLeftMs  + POS_DEADBAND_MS) { lu = true; needMove = true; }
        if (_posRightMs < tgtRightMs - POS_DEADBAND_MS) { rd = true; needMove = true; }
        else if (_posRightMs > tgtRightMs + POS_DEADBAND_MS) { ru = true; needMove = true; }

        if (needMove) _settleMs = SETTLE_AFTER_MOVE_MS;
        break;
      }
    }

    // Minimum pådragstid 500 ms — unngår korte jokke-pulser.
    // Retningsskifte nullstiller motstående hold umiddelbart.
    static const float MIN_RELAY_ON_MS = 500.0f;
    const float hdt = dt * 1000.0f;
    if (lu) _holdLDms = 0; if (ld) _holdLUms = 0;
    if (ru) _holdRDms = 0; if (rd) _holdRUms = 0;
    if (lu && _holdLUms <= 0) _holdLUms = MIN_RELAY_ON_MS;
    if (ld && _holdLDms <= 0) _holdLDms = MIN_RELAY_ON_MS;
    if (ru && _holdRUms <= 0) _holdRUms = MIN_RELAY_ON_MS;
    if (rd && _holdRDms <= 0) _holdRDms = MIN_RELAY_ON_MS;
    if (_holdLUms > 0) { lu = true; _holdLUms -= hdt; }
    if (_holdLDms > 0) { ld = true; _holdLDms -= hdt; }
    if (_holdRUms > 0) { ru = true; _holdRUms -= hdt; }
    if (_holdRDms > 0) { rd = true; _holdRDms -= hdt; }

    integratePosition(dt, lu, ld, ru, rd, p);
    _r->apply(lu, ld, ru, rd);
  }

  State state()      const { return _state; }
  Side  activeSide() const { return _active; }
  float posLeftFrac(const AutotrimParams &p)  const { return clampFrac(_posLeftMs  / p.fullStrokeMs); }
  float posRightFrac(const AutotrimParams &p) const { return clampFrac(_posRightMs / p.fullStrokeMs); }

private:
  static float clampFrac(float f) { return f < 0 ? 0 : (f > 1 ? 1 : f); }
  void setState(State s) {
    _state = s; _stateTimeMs = 0;
    if (s == State::ACTIVE || s == State::STANDBY) { _trimFrac = 0; _settleMs = 0; }
    _holdLUms = _holdLDms = _holdRUms = _holdRDms = 0;
  }
  void integratePosition(float dt, bool lu, bool ld, bool ru, bool rd, const AutotrimParams &p) {
    float dms = dt * 1000.0f;
    if (ld) _posLeftMs  += dms;  if (lu) _posLeftMs  -= dms;
    if (rd) _posRightMs += dms;  if (ru) _posRightMs -= dms;
    _posLeftMs  = constrain(_posLeftMs,  0.0f, p.fullStrokeMs);
    _posRightMs = constrain(_posRightMs, 0.0f, p.fullStrokeMs);
  }

  Relays *_r = nullptr;
  State   _state = State::BOOT_UP;
  Side    _active = Side::NONE;
  float   _stateTimeMs = 0, _posLeftMs = 0, _posRightMs = 0;
  float   _trimFrac = 0;   // signert trimposisjon: + = babord deployert, - = styrbord deployert
  float   _settleMs = 0;  // nedtelling etter pådrag — ingen ny beslutning mens > 0
  bool    _autoLatched = false; float _recalMs = 0;
  int     _testCh = -1; float _testMs = 0;
  float   _holdLUms = 0, _holdLDms = 0, _holdRUms = 0, _holdRDms = 0;
};

// ============================================================================
//  BLE  (fra ble_iface.h/.cpp) — NimBLE 2.x GATT
// ============================================================================
namespace {
  NimBLECharacteristic *chParams = nullptr;
  NimBLECharacteristic *chTele   = nullptr;
  NimBLECharacteristic *chCmd    = nullptr;
  AutotrimParams       *gParams  = nullptr;
  volatile bool    gParamsDirty = false;
  volatile uint8_t gLastCmd     = CMD_NONE;
  volatile bool    gConnected   = false;

  class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override    { gConnected = true; }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override { gConnected = false; NimBLEDevice::startAdvertising(); }
  };
  class ParamsCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
      NimBLEAttValue v = c->getValue();
      Serial.printf("BLE params write: len=%d (want %d)\n", (int)v.length(), (int)sizeof(AutotrimParams));
      if (v.length() == sizeof(AutotrimParams) && gParams) {
        AutotrimParams tmp; memcpy(&tmp, v.data(), sizeof(tmp));
        bool valid = params_valid(tmp);
        Serial.printf("  magic=0x%04X ver=%d valid=%d mountOff=%.2f\n",
                      tmp.magic, tmp.version, (int)valid, tmp.mountingOffsetDeg);
        if (valid) { *gParams = tmp; gParamsDirty = true; }
      }
    }
  };
  class CmdCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
      NimBLEAttValue v = c->getValue();
      if (v.length() >= 1) gLastCmd = (uint8_t)v[0];
    }
  };
  ServerCB serverCB; ParamsCB paramsCB; CmdCB cmdCB;
}

class BleIface {
public:
  void begin(AutotrimParams *params) {
    gParams = params;
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(9);   // dBm
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCB);
    NimBLEService *svc = server->createService(UUID_SVC);

    chParams = svc->createCharacteristic(UUID_CHAR_PARAMS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    chParams->setCallbacks(&paramsCB);
    chParams->setValue((uint8_t*)gParams, sizeof(AutotrimParams));

    chTele = svc->createCharacteristic(UUID_CHAR_TELEMETRY, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    chCmd = svc->createCharacteristic(UUID_CHAR_COMMAND, NIMBLE_PROPERTY::WRITE);
    chCmd->setCallbacks(&cmdCB);

    svc->start();
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(UUID_SVC);
    adv->enableScanResponse(true);
    NimBLEDevice::startAdvertising();
  }

  void publishTelemetry(const Telemetry &t) {
    if (!chTele) return;
    chTele->setValue((uint8_t*)&t, sizeof(t));
    if (gConnected) chTele->notify();
  }
  bool consumeParamsDirty() { if (gParamsDirty) { gParamsDirty = false; return true; } return false; }
  Command takeCommand() { uint8_t c = gLastCmd; gLastCmd = CMD_NONE; return (Command)c; }
  bool connected() const { return gConnected; }
};

// ============================================================================
//  MAIN  (fra main.cpp) — scheduler
// ============================================================================
static AutotrimParams params;
static Imu      imuDev;          // NB: ikke 'imu' — kolliderer med imumaths-navnerommet
static Gps      gps;
static Relays   relays;
static Control  control;
static BleIface ble;

static uint32_t tImu = 0, tCtrl = 0, tTele = 0, tDbg = 0;

static void handleCommands() {
  if (ble.consumeParamsDirty()) params_save(params);
  Command c = ble.takeCommand();
  // Manuell relé-test: virker KUN når testmodus (testBypass) er på — sikkerhet.
  if (c >= CMD_TEST_LU && c <= CMD_TEST_OFF) {
    if (params.testBypass) {
      if (c == CMD_TEST_OFF) control.requestTestOff();
      else                   control.requestTestPulse((int)c - (int)CMD_TEST_LU);
    }
    return;
  }
  switch (c) {
    case CMD_BOTH_UP:       control.requestBothUp(); break;
    case CMD_RECAL_UP:      control.requestRecal();  break;
    case CMD_SAVE_PARAMS:   params_save(params);     break;
    case CMD_SET_ROLL_ZERO: imuDev.captureZero();    break;
    default: break;
  }
}

static void publishTelemetry() {
  Telemetry t{};
  t.millisUp     = millis();
  t.rollDeg      = imuDev.rollDeg() + params.mountingOffsetDeg;
  t.rollRateDps  = imuDev.rollRateDps();
  t.accX         = imuDev.accX();
  t.accY         = imuDev.accY();
  t.sogKn        = gps.sogKn();
  t.gpsFix       = gps.hasFix() ? 1 : 0;
  t.sats         = gps.sats();
  t.state        = (uint8_t)control.state();
  t.activeSide   = (int8_t)control.activeSide();
  t.posLeftFrac  = control.posLeftFrac(params);
  t.posRightFrac = control.posRightFrac(params);
  t.relayBits    = relays.bits();
  t.faults       = (imuDev.ok() ? 0 : FAULT_IMU)
                 | (gps.hasFix() ? 0 : FAULT_GPS_FIX)
                 | (imuDev.sane() ? 0 : FAULT_IMU_SANITY);
  ble.publishTelemetry(t);
}

void setup() {
  Serial.begin(115200);

  // Auto-recovery: slett stale NimBLE NVS hvis forrige boot krasjet, start om
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC ||
      resetReason == ESP_RST_INT_WDT ||
      resetReason == ESP_RST_TASK_WDT) {
    Serial.println("KRASJ oppdaget — sletter NVS og starter om...");
    delay(100);
    nvs_flash_erase();
    ESP.restart();
  }

  relays.begin();        // 1) trygt: alle reléer av
  params_load(params);   // 2) parametere
  params.testBypass = 0; //    sikkerhet: testmodus ALLTID av ved boot
  ble.begin(&params);    // 3) BLE
  gps.begin();
  bool imuOk = imuDev.begin();
  control.begin(&relays); // 4) kontroll (starter i BOOT_UP -> begge opp 3 s)

  Serial.printf("Autotrim v1 klar. IMU=%s\n", imuOk ? "OK" : "FEIL");

  uint32_t now = millis();
  tImu = tCtrl = tTele = tDbg = now;
}

void loop() {
  uint32_t now = millis();

  gps.poll();
  handleCommands();

  if (now - tImu >= IMU_PERIOD_MS) {
    float dt = (now - tImu) / 1000.0f;
    tImu = now;
    imuDev.update(dt, params);
  }
  if (now - tCtrl >= CTRL_PERIOD_MS) {
    float dt = (now - tCtrl) / 1000.0f;
    tCtrl = now;
    bool imuSane = imuDev.ok() && imuDev.sane();
    control.update(dt, imuDev.rollDeg() + params.mountingOffsetDeg, imuDev.rollRateDps(), imuSane,
                   gps.sogKn(), gps.hasFix(), params);
  }
  if (now - tTele >= TELE_PERIOD_MS) {
    tTele = now;
    publishTelemetry();
  }

  if (now - tDbg >= 500) {                 // 2 Hz USB-debug
    tDbg = now;
    const char* st[] = {"BOOT_UP","STANDBY","ACTIVE","FAULT"};
    Serial.printf("roll:%+6.1f rate:%+6.1f | SOG:%4.1f fix:%d sats:%2d | %-7s rel:0x%X\n",
                  imuDev.rollDeg() + params.mountingOffsetDeg, imuDev.rollRateDps(), gps.sogKn(), gps.hasFix(), gps.sats(),
                  st[(uint8_t)control.state() & 3], relays.bits());
  }
}
