// ============================================================================
//  Autotrim — STANDALONE BLE-TEST (+ GPS + IMU + USB-debug)   [Arduino IDE]
// ----------------------------------------------------------------------------
//  Setter opp samme BLE-tjeneste/UUID-er som fastvaren (ble_iface.cpp), leser
//  GPS (UART2) og IMU (I2C), sender telemetri over notify, og skriver en
//  debuglinje over USB-serieporten 2x/sekund. Rører IKKE reléer.
//
//  BLE: bruk nRF Connect / web-dashbordet. USB: Serial Monitor @ 115200.
//
//  KREVER BIBLIOTEKER: "NimBLE-Arduino" 2.x, "Adafruit BNO055" (+ Unified Sensor + BusIO)
//  Board = "ESP32 Dev Module".
// ============================================================================
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <NimBLEDevice.h>

// ---- UUID-er og navn (MA matche config.h) ----
#define BLE_DEVICE_NAME     "Autotrim"
#define UUID_SVC            "9a8b0001-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_PARAMS    "9a8b0002-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_TELEMETRY "9a8b0003-7b2c-4f3d-9e6a-2b1c3d4e5f60"
#define UUID_CHAR_COMMAND   "9a8b0004-7b2c-4f3d-9e6a-2b1c3d4e5f60"

// ---- IMU (verifiserte fortegn fra benk-test) ----
static const int PIN_I2C_SDA = 21, PIN_I2C_SCL = 22, PIN_BNO_RST = 4;
static const uint8_t BNO_ADDR = 0x28;
static const float ROLL_SIGN = -1.0f, GYRO_SIGN = 1.0f;

// ---- GPS (TBS M10N, UART2 @ 38400) ----
static const int PIN_GPS_RX = 16, PIN_GPS_TX = 17;
static const uint32_t GPS_BAUD = 38400;
#define GPSSER Serial2

// ---- Telemetri-struct IDENTISK med firmware/src/types.h (39 byte) ----
#pragma pack(push, 1)
struct Telemetry {
  uint32_t millisUp; float rollDeg; float rollRateDps; float accX; float accY;
  float sogKn; uint8_t gpsFix; uint8_t sats; uint8_t state; int8_t activeSide;
  float posLeftFrac; float posRightFrac; uint8_t relayBits; uint16_t faults;
};
#pragma pack(pop)

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
static bool imuOk = false;

// GPS-tilstand (parses fra NMEA)
static char gline[120]; static uint8_t glen = 0;
static char gRmc = 'V', gFixQ = '0';
static int  gSats = 0; static float gSog = 0.0f;

NimBLECharacteristic *chParams = nullptr, *chTele = nullptr, *chCmd = nullptr;
volatile bool gConnected = false;
static uint32_t tNotify = 0, tDebug = 0;

// ---------- NMEA-parsing ----------
static String nmeaField(const char* s, int idx) {
  int f = 0; String out = "";
  for (const char* p = s; *p && *p != '*'; ++p) {
    if (*p == ',') { f++; continue; }
    if (f == idx) out += *p; if (f > idx) break;
  }
  return out;
}
static void parseLine(const char* s) {
  if (strlen(s) < 6 || s[0] != '$') return;
  const char* t = s + 3;
  if (!strncmp(t, "GGA", 3)) {
    String q = nmeaField(s, 6), n = nmeaField(s, 7);
    if (q.length()) gFixQ = q[0]; if (n.length()) gSats = n.toInt();
  } else if (!strncmp(t, "RMC", 3)) {
    String st = nmeaField(s, 2), sp = nmeaField(s, 7);
    if (st.length()) gRmc = st[0]; if (sp.length()) gSog = sp.toFloat();
  }
}
static void pollGps() {
  while (GPSSER.available()) {
    char c = (char)GPSSER.read();
    if (c == '\n' || c == '\r') {
      if (glen > 0) { gline[glen] = '\0'; parseLine(gline); glen = 0; }
    } else if (glen < sizeof(gline) - 1) {
      gline[glen++] = c;
    }
  }
}

// ---------- NimBLE 2.x callbacks ----------
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override    { gConnected = true;  Serial.println("BLE: TILKOBLET"); }
  void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override { gConnected = false; Serial.println("BLE: frakoblet"); NimBLEDevice::startAdvertising(); }
};
class CmdCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
    NimBLEAttValue v = c->getValue();
    if (v.length() >= 1) Serial.printf("BLE: kommando mottatt = %u\n", (uint8_t)v[0]);
  }
};
class ParamsCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
    Serial.printf("BLE: PARAMS skrevet, %u byte\n", (unsigned)c->getValue().length());
  }
};
ServerCB serverCB; CmdCB cmdCB; ParamsCB paramsCB;

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n=== Autotrim BLE-test (GPS + IMU + USB-debug 2Hz) ==="));

  // --- GPS ---
  GPSSER.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  Serial.printf("GPS: UART2 @ %lu baud\n", (unsigned long)GPS_BAUD);

  // --- IMU ---
  if (PIN_BNO_RST >= 0) { pinMode(PIN_BNO_RST, OUTPUT); digitalWrite(PIN_BNO_RST, LOW); delay(5); digitalWrite(PIN_BNO_RST, HIGH); delay(30); }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); Wire.setClock(400000);
  imuOk = bno.begin(OPERATION_MODE_ACCGYRO);
  if (imuOk) { delay(20); bno.setExtCrystalUse(true); Serial.println(F("IMU OK")); }
  else        Serial.println(F("IMU mangler - roll sender 0"));

  // --- BLE: samme oppsett som ble_iface.cpp ---
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCB);
  NimBLEService *svc = server->createService(UUID_SVC);

  chParams = svc->createCharacteristic(UUID_CHAR_PARAMS, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  chParams->setCallbacks(&paramsCB);
  uint8_t dummy[52] = {0}; dummy[0] = 0x70; dummy[1] = 0xA7; dummy[2] = 1;
  chParams->setValue(dummy, sizeof(dummy));

  chTele = svc->createCharacteristic(UUID_CHAR_TELEMETRY, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  chCmd = svc->createCharacteristic(UUID_CHAR_COMMAND, NIMBLE_PROPERTY::WRITE);
  chCmd->setCallbacks(&cmdCB);

  svc->start();
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_SVC);
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.println(F("BLE annonserer som 'Autotrim'."));
  Serial.println(F("(GPS: fix krever fri sikt mot himmel; kaldstart kan ta noen min.)"));
  tNotify = tDebug = millis();
}

void loop() {
  pollGps();                              // les GPS kontinuerlig

  // bygg gjeldende telemetri
  Telemetry t = {};
  t.millisUp = millis();
  float amag = 0;
  if (imuOk) {
    imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    t.accX = a.x(); t.accY = a.y();
    amag = sqrtf(a.x()*a.x() + a.y()*a.y() + a.z()*a.z());
    t.rollDeg     = atan2f(a.y(), a.x()) * 57.2957795f * ROLL_SIGN;
    t.rollRateDps = g.z() * GYRO_SIGN;
  }
  t.sogKn  = gSog;
  t.gpsFix = (gRmc == 'A' && gSats >= 4) ? 1 : 0;
  t.sats   = (uint8_t)gSats;
  t.state  = 1;                           // STANDBY (test)

  if (millis() - tNotify >= 200) {        // 5 Hz BLE-notify
    tNotify = millis();
    chTele->setValue((uint8_t*)&t, sizeof(t));
    if (gConnected) chTele->notify();
  }

  if (millis() - tDebug >= 500) {         // 2 Hz USB-debug
    tDebug = millis();
    Serial.printf("roll:%+6.1f deg  rate:%+6.1f dps  |a|:%4.1f  ||  GPS RMC:%c fixQ:%c sats:%2d SOG:%5.1f kn  BLE:%s\n",
                  t.rollDeg, t.rollRateDps, amag, gRmc, gFixQ, gSats, gSog,
                  gConnected ? "til" : "fra");
  }
}
